# OPtech2 changes

Changes made on top of Yamagi Quake II Remaster. Upstream history is in `CHANGELOG`.

## Unreleased

- Forked from Yamagi Quake II Remaster (511b7b0e).
- Project named OPtech2 (Open Tech 2).
- One-command build: `cmake --workflow --preset default` (Vulkan renderer off).
- Screenshots from the GL3 renderer are captured at the end of the next frame, before the buffer swap. Before, they could come out black or stale.
- `teleport x y z pitch yaw roll`: the cheat command can now also set the view direction.
- Screenshot regression check: `python3 tests/screenshots/screenshot_check.py` (add `--update` to save new references).
