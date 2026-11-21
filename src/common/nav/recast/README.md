# Recast/Detour 1.6.0

The navigation mesh builder (Recast) and path finder (Detour) from
https://github.com/recastnavigation/recastnavigation, release v1.6.0
(`v1.6.0.tar.gz`, sha256
`d48ca0121962fa0639502c0f56c4e3ae72f98e55d88727225444f500775c0074`), file for
file as released, for `src/common/nav/nav.cpp`.

Kept: `Recast/` and `Detour/` (`Include/`, `Source/`) and `License.txt`. Left
out: DetourCrowd, DetourTileCache, DebugUtils, the demo, tests and the
library's own CMake files.

Recast/Detour is under the zlib license: Copyright (c) 2009 Mikko Mononen, see
`License.txt`.

To update: replace these folders with the same ones from the new release and
note the version and hash here. Don't reformat them (the pre-commit hook skips
this folder).
