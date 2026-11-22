#!/bin/sh
# Runs cppcheck like CI (.github/workflows/linux_aarch64.yml) and prints one
# line per finding (file:line|rule|severity|message); no output = clean.
# Needs cppcheck 2.13 or newer.
# Usage: tools/ci/run_cppcheck.sh [-o output file] [source files...]
# With source files, only those are checked (the pre-commit hook does this);
# checks that need the whole program are then left out.
cd "$(dirname "$0")/../.." || exit 1
out=/dev/stdout
if [ "$1" = "-o" ]; then
	out="$2"
	shift 2
fi
partial=""
if [ $# -gt 0 ]; then
	partial="--suppress=unusedFunction --suppress=unmatchedSuppression"
else
	set -- src
fi
cppcheck -j "$(nproc)" --enable=all \
  --inconclusive --library=posix \
  --suppress=missingIncludeSystem --check-level=exhaustive \
  --suppress=missingInclude:src/client/input/sdl3.c \
  --suppress=constParameterCallback --inline-suppr \
  --include=tools/ci/cppcheck_shims.h \
  --suppress=uninitMemberVarNoCtor:src/game/rerelease/game.h \
  -i src/common/nav/recast -i src/game/lua -i src/common/unzip \
  -i src/client/refresh/vk/volk -i src/client/refresh/gl1/glad-gles1 \
  -i src/client/refresh/gl3/glad -i src/client/refresh/gl3/glad-gles3 \
  -i src/client/refresh/gl4/glad \
  --template='{file}:{line}|{id}|{severity}|{message}' $partial \
  -DYQ2OSTYPE=\"Linux\" -DYQ2ARCH=\"aarch64\" "$@" 2>&1 >/dev/null \
  | grep -vE "stb_|pl_mpeg|minimp3|HandmadeMath|nav/recast|^Checking|files checked|^nofile|checkersReport" > "$out"
