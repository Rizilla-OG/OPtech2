# Lua 5.4.8

The Lua core and standard libraries, from
https://www.lua.org/ftp/lua-5.4.8.tar.gz (sha256
`4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`), file for
file as released, for the gameplay scripts in `src/game/script/g_script.c`.

Left out, because scripts are sandboxed and don't use them: `lua.c` and `luac.c`
(the standalone programs), `linit.c` (scripts open their libraries themselves),
`liolib.c`, `loslib.c`, `loadlib.c` and `ldblib.c` (the io, os, package and
debug libraries).

Lua is free software under the MIT license: Copyright (C) 1994-2025 Lua.org,
PUC-Rio. The full notice is at the end of `lua.h`.

To update: replace these files with the same files from the new 5.4 release
and note the version and hash here. Don't reformat them (the pre-commit hook
skips this folder).
