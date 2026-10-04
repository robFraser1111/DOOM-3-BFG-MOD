/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

===========================================================================
*/

/*
The 2012 GPL drop includes #include "TypeInfo.h" from the game system, and the
Visual Studio projects tried to run a TypeInfo.exe pre-build step that is not
shipped. Nothing in this tree references a symbol from that generated header.
This stub keeps the includes valid so the game links without the missing tool.
*/
#ifndef __TYPEINFO_H__
#define __TYPEINFO_H__
#endif
