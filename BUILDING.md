# Building Doom 3 BFG Edition on Windows

This tree builds with Visual Studio 2022 (toolset v143) and the Windows 10/11 SDK.
The June 2010 DirectX SDK is not used. Release x64 is the configuration exercised
by GitHub Actions.

You need your own copy of Doom 3 BFG Edition from Steam for the game data. No
game data is in this repository.

## Install

1. Visual Studio 2022 Community.
2. Workload **Desktop development with C++**.
3. A Windows 10 or Windows 11 SDK (the installer component is enough; version 10.0
   selects whichever SDK is installed).
4. Steam copy of Doom 3 BFG Edition, installed and updated.

No separate DirectX SDK, Bink SDK, Steamworks SDK, MFC, or ATL. The C++ desktop workload and a Windows 10/11 SDK are enough.

## Build in Visual Studio

1. Open `neo\doom3.sln`.
2. If Visual Studio asks to retarget the projects, choose the v143 toolset and the
   installed Windows 10 SDK.
3. Configuration **Release**, platform **x64**.
4. Build Solution.

The executable is:

```
build\x64\Release\Doom3BFG.exe
```

Debug and Retail x64 are in the solution as well. Retail adds the `ID_RETAIL`
preprocessor define. Win32 is still present; x64 is the configuration to use on
current 64-bit Windows.

## Build from a command prompt

From a **x64 Native Tools Command Prompt for VS 2022**, at the repository root:

```
msbuild neo\doom3.sln /m /p:Configuration=Release /p:Platform=x64
```

## Game data and launch

Steam installs the game here by default:

```
C:\Program Files (x86)\Steam\steamapps\common\Doom 3 BFG Edition
```

That folder contains a `base` directory. The engine reads `base` under
`fs_basepath` (the current directory if you do not set it).

From the repository root:

```
build\x64\Release\Doom3BFG.exe +set fs_basepath "C:\Program Files (x86)\Steam\steamapps\common\Doom 3 BFG Edition" +set com_skipIntroVideos 1
```

If that path is different on your machine, use the folder that contains `base`,
not the `base` folder itself.

`com_skipIntroVideos 1` skips the startup Bink movie. This source drop does not
include the Bink decoder, so the intro video cannot play.

You can instead copy `Doom3BFG.exe` into the Steam install folder and start it
from that directory. The default base path is the current working directory, so
`base` is found without `fs_basepath`. Saves still go to your Windows Saved Games
folder.

The first thing the game loads from `base` is `default.cfg`. If that file is
missing, the engine stops with `Couldn't load default.cfg`.

Release writes `Doom3BFG.pdb` next to the executable. On startup the game also
writes `Doom3BFG.log` in that same folder (console text, flushed every line).
An unhandled crash writes `crash.txt` and `crash.dmp` there too: exception
code, faulting module and offset, and a DbgHelp stack. Those three files are
the ones to keep if the process dies before a window appears.

## x64 pointer-width audit

The engine was read for the usual 32-bit assumptions: pointers stored in or cast through `int`, `long`, `unsigned`, `DWORD`, or `uint32`; pointer differences stored in `int`; `sizeof(void*) == 4` in structs, unions, and file or network formats; 32-bit offset and size math; Win32 handles (`HANDLE`, `HWND`, `HINSTANCE`, `_findfirst`); `printf` formats; inline assembly and x87/SSE assumptions; and alignment masks. The sweep covered `neo/idlib`, `framework`, `renderer`, `ui`, `sound`, `swf`, `sys`, `d3xp`, `cm`, `aas`, and `doomclassic` (it is linked into the executable).

Fixed, including the earlier startup and Lost Mission crashes:

- `_findfirst` is an `intptr_t` on x64. `Sys_ListFiles` keeps that handle intact.
- DLL `HINSTANCE` values passed through `idSys` are `intptr_t`.
- File tell, seek, and length use `SetFilePointerEx` and `GetFileSizeEx`. A file larger than 2GB reports length -1 instead of a wrapped size.
- GUI expression ops (`wexpOp_t`) store an `idWinVar*` in `intptr_t`. That was the Lost Mission crash in `idWindow::EvaluateRegisters`.
- `idVecX::tempPtr` and `idMatX::tempPtr` align their static scratch buffers with `uintptr_t`. The old `(int)` cast dropped the high half of the address. The constraint solver uses those temps.
- LCP and frame-allocator alignment tests mask the full address. Only the low bits matter, and the mask no longer truncates first.
- The AAS travel-time check subtracts the two pointers. It used to subtract them after casting each to `unsigned int`.
- Window procedures return `LRESULT`. The early console returns its `HBRUSH` without cutting it down to 32 bits.
- GUI transition offsets use `offsetof` and stay `int`. A window object is far smaller than 2GB, so the offset fits.
- The debug map-file symbolizer stores a module base as `uintptr_t`. The x86 prologue walk and the `ID_WIN_X86_ASM` blocks are not compiled for x64.
- Classic Doom colormaps and translation tables align with `uintptr_t`. The old `(int)` alignment dropped the high half of the allocation. Savegame indexes that lived in pointer fields are read back with `intptr_t`. The cvar "not registered yet" marker is `0xFFFFFFFF` widened through `uintptr_t`, not a real `idCVar*`.

`C4311`, `C4312`, and `C4302` (pointer truncated to 32 bits, or a 32-bit int widened to a pointer) are enabled, and the engine projects treat warnings as errors. `C4244`, `C4267`, `C4477`, and `C4838` stay disabled. Turning those on fails the build on the existing float-to-int, `size_t`-to-int, `printf`, and narrowing conversions. Those sites were read and left alone when they were numeric, not pointer-width bugs.

Still true after the audit:

- Resource, zip, and `idFile` offsets are 32-bit because that is the file format. The Steam resource files are under 2GB. A modded file past 2GB will not load.
- SWF `Length` and `Tell` cast a pointer difference to `uint32`. The shipped Flash files are small.
- Event integer and float arguments are stored as 32-bit values. Entity, string, and trace arguments keep the full pointer.
- Vertex-cache handles pack fields into a `uint64` and unpack them through `int`. Each field mask fits in 31 bits.
- x64 compiles float and double as SSE2, not 80-bit x87. See the math note below.
- Release `crash.txt` uses DbgHelp. The debug-only `.map` parser still stores per-symbol addresses from the map file as `int`, and it is not in the Release executable.
- A heap pointer-range check that cast pointers to `int` is inside a block comment and is not compiled.

## Known limitations

- **No Steam.** No overlay, achievements, leaderboards, matchmaking, or roaming
  profiles. The multiplayer session code has no Steam backend.
- **No Bink.** Intro and menu videos that are `.bik` files do not play. Use
  `+set com_skipIntroVideos 1`.
- **Stencil shadows.** The "depth fail" path (Carmack's Reverse) was removed in
  the 2012 GPL drop. Shadows are wrong or missing when the camera is inside a
  shadow volume. That is unchanged here.
- **x64 math.** The 32-bit build used the x87 unit with 80-bit precision. MSVC
  x64 compiles float and double as SSE2 (24-bit and 53-bit). Physics and other
  edge cases can diverge slightly from the original 32-bit executable. Denormals
  are still flushed (DAZ/FTZ) the way the engine requested.
- **Sound.** Output is XAudio2 2.9, which is part of Windows 8 and later, not
  XAudio2 2.7 from the old DirectX SDK. `s_device -1` is the Windows default
  endpoint. `s_device N` picks an endpoint from `listDevices` (WASAPI order).
  Classic Doom music inside BFG still goes through this XAudio2 device.
- **Input.** Keyboard and mouse still use DirectInput 8. Gamepads use XInput
  1.4 (`xinput1_4.dll` on Windows 10/11). Raw Input is not used.
- **OpenGL.** The renderer still creates a legacy compatibility context. A
  driver that exposes that context is required. Core-profile-only setups will
  not render.
- **TypeInfo.exe** was referenced by the old project files and is not in this
  source drop. The game system include is a stub. Class save and restore are
  the functions already in the source, not a generated reflection dump.
- This repository's CI compiles Release x64. It does not launch the game.
