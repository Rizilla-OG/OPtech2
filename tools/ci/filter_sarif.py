#!/usr/bin/env python3
"""Drops code scanning results in bundled third-party code from SARIF files.

The libraries under VENDORED_PATHS are bundled unmodified (Recast/Detour,
Lua, unzip, stb, pl_mpeg, volk, glad, minimp3, HandmadeMath): their warnings are upstream's, not ours. CodeQL can't skip
them for C/C++ (paths-ignore only works for interpreted languages), so the
CodeQL workflow analyses everything and this filters the SARIF before upload.

Usage:
  filter_sarif.py FILE_OR_FOLDER...   rewrite each .sarif in place
"""

import json
import sys
from pathlib import Path

VENDORED_PATHS = (
    "src/common/nav/recast/",
    "src/game/lua/",
    "src/common/unzip/",
    # single-file libraries bundled next to our code
    "src/client/refresh/vk/volk/",
    "src/client/refresh/gl1/glad-gles1/",
    "src/client/refresh/gl3/glad/",
    "src/client/refresh/gl3/glad-gles3/",
    "src/client/refresh/gl4/glad/",
    "src/client/cinema/pl_mpeg.h",
    "src/client/refresh/files/stb_image.h",
    "src/client/refresh/files/stb_image_resize.h",
    "src/client/refresh/files/stb_truetype.h",
    "src/client/sound/header/stb_vorbis.h",
    "src/client/sound/header/minimp3.h",
    "src/client/refresh/files/HandmadeMath.h",
    "src/client/vid/header/stb_image_write.h",
)


def is_vendored(result):
    for location in result.get("locations", []):
        uri = location.get("physicalLocation", {}).get("artifactLocation", {}).get("uri", "")
        uri = uri.replace("\\", "/")
        if any(path in uri for path in VENDORED_PATHS):
            return True
    return False


def filter_file(path):
    sarif = json.loads(path.read_text(encoding="utf-8"))
    kept = dropped = 0
    for run in sarif.get("runs", []):
        results = run.get("results", [])
        remaining = [result for result in results if not is_vendored(result)]
        dropped += len(results) - len(remaining)
        kept += len(remaining)
        run["results"] = remaining
    path.write_text(json.dumps(sarif), encoding="utf-8")
    print(f"{path}: kept {kept}, dropped {dropped} in bundled libraries")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    files = []
    for argument in sys.argv[1:]:
        target = Path(argument)
        files += sorted(target.rglob("*.sarif")) if target.is_dir() else [target]
    if not files:
        sys.exit("no .sarif files found")
    for path in files:
        filter_file(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
