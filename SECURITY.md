# Security Policy

## Supported versions

OpenTech2 is in development. Only the latest release and the `main` branch get fixes.

| Version                          | Supported |
| -------------------------------- | --------- |
| Latest release (M2 preview)      | Yes       |
| `main`                           | Yes       |
| Older releases                   | No        |

## What counts

OpenTech2 is a game engine: the client (`OpenTech2` / `OpenTech2.exe`), the dedicated server
(`OpenTech2-server`), the renderers and the game library (`game.so` / `game.dll`). Report it if
the engine or the game library can:

- crash or run code through a crafted file: a map (`.bsp`, `.ent`), model (`.md2`, `.iqm`, ...),
  texture, sound, video, save, demo, `.cfg`, or pack (`.pak`, `.pk3`)
- let a server crash, freeze or take over a client that connects to it, or a client do that to a
  server (network messages, downloads, configstrings, packet splitting)
- write or read files outside the game's own folders (for example through a download or a save name)
- ship anything harmful in a release archive

Not security issues (open a normal issue instead): gameplay bugs, ordinary crashes with no outside
input, and anything that needs you to run a mod's own game library: a mod's `game.so` / `game.dll`
is native code and runs with your rights by design, so only install mods you trust. Bugs that are
also in [Yamagi Quake II Remaster](https://github.com/yquake2/yquake2remaster) are welcome here too,
but please also report them upstream.

## How to report

Please **don't** open a public issue for a security problem.

Use GitHub's private reporting: go to the repository's **Security** tab and click
**Report a vulnerability**.

Include the version (the release name, or `OpenTech2 v...` in the console when the game starts),
your system, steps to reproduce, and any file that triggers it.

## What happens next

- You'll get a reply within about a week.
- If it's real, the fix ships in the next release and `CHANGES.md` notes it.
- You'll be credited unless you'd rather not be.

This is a hobby project, so there's no bug bounty.
