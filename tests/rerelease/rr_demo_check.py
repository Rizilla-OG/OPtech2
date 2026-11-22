#!/usr/bin/env python3
"""Re-release demo check for OpenTech2: the re-release's own demos
(demo1.dm2, demo2.dm2 in its pak, protocol 2022) play for 15 s each with
no parse errors. They run behind the menu at startup, so a change to how
entity updates are read that only fits our protocol breaks the game right
as it starts.

Usage:
  rr_demo_check.py
Needs the re-release data. Hidden software renderer, temporary HOME.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_game_check  # noqa: E402  (shares paths)

OUTPUT_DIR = rr_game_check.REPO_DIR / "build" / "rr-demo-check"
DEMOS = ("demo1", "demo2")


def main():
    if not (rr_game_check.RERELEASE_DATA_DIR / "baseq2").exists():
        print(f"skip: no re-release data in {rr_game_check.RERELEASE_DATA_DIR}")
        return 0
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    for demo in DEMOS:
        with tempfile.TemporaryDirectory(prefix="optech2-rr-demo-") as home:
            user_dir = Path(home) / ".local" / "share" / "OpenTech2" / "baseq2"
            user_dir.mkdir(parents=True)
            (user_dir / "c.cfg").write_text(f'demomap {demo}.dm2\ndelay 15000 "quit"\n')
            environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            log_path = OUTPUT_DIR / f"{demo}.log"
            with open(log_path, "w") as log_file:
                subprocess.run(["stdbuf", "-oL", str(rr_game_check.CLIENT_BINARY),
                                "-datadir", str(rr_game_check.RERELEASE_DATA_DIR),
                                "+set", "vid_renderer", "soft", "+set", "s_initsound", "0", "+exec", "c.cfg"],
                               env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=60, check=False)
            log = log_path.read_text(errors="replace")
        maps = [line.split(":", 1)[1].strip() for line in log.splitlines() if line.startswith("Map:")]
        bad = [line.strip() for line in log.splitlines()
               if "ERROR" in line or "Illegible" in line or "U_REMOVE" in line or "bad " in line.lower()]
        report(bool(maps) and not bad,
               f"{demo} played ({', '.join(maps) or 'no map'})" + (f": {bad[0][:90]}" if bad else ""))

    passed = all(results)
    print(f"\nRe-release demo check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
