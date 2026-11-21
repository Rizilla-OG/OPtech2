# Contributing to OpenTech2

Thanks for helping. OpenTech2 is a hobby project, so small, focused changes are the easiest to take in.

## Before you start

- For anything bigger than a small fix, open an issue first so we can agree on the approach.
  The [roadmap](https://rizilla-og.github.io/OPtech2/roadmap.html) shows what is planned and in which order.
- Security problems: don't open a public issue, see [SECURITY.md](SECURITY.md).

## Build and test

```
cmake --workflow --preset default        # Linux and macOS; output in build/release/
python3 tests/run_all.py --headless      # the checks that need no window or sound device
python3 tests/run_all.py                 # all checks (opens a game window now and then)
```

The checks need the Quake II game data (they look in the usual Steam folder). Each one is a script under
`tests/` with its own description at the top. A change that alters behaviour comes with a check, or an
update to one.

## Code style

- C in the id / Yamagi style: tabs, Allman braces, `/* comments */`. Enable the pre-commit hook once per
  clone, it formats the lines you change: `git config core.hooksPath .githooks`
- Mark changes to code we inherited (Yamagi, id) with a short comment saying why:
  `/* optech2: what this changes and why */`. New files don't need it.
- Don't reformat code you aren't changing; keep diffs to inherited files small.
- id's re-release files in `src/game/rerelease/` stay byte-identical to id's source; our build glue lives
  in `optech2_prelude.h`.
- After changing a struct the savegame stores, run `python3 tools/gen_save_fields.py`.

## Changes and pull requests

- Add a line to the `Unreleased` section of [CHANGES.md](CHANGES.md) in the same commit, written for
  players and modders: what changed and why it matters.
- One topic per pull request, with a short description of what you tested.
- By contributing you agree your work is released under the GNU GPL version 2 or later, like the rest of
  the engine.
