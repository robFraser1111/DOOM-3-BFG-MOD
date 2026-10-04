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
