#!/usr/bin/env python3
"""Sound format check for OpenTech2: writes WAV files in formats beyond what
stock Quake II shipped (stock: 8/16 bit mono, 11-44 kHz, a few seconds),
plays each with the "play" command and reads "soundlist" back, for both
sound backends (OpenAL and SDL). For each file it checks that it loaded and
that the length the engine reports matches the file.

Usage:
  sound_check.py                    both backends
  sound_check.py --backend sdl      one backend: openal or sdl

No sound comes out: OpenAL runs on its null driver, SDL on its dummy driver,
the video on SDL's dummy driver. Uses a temporary HOME. Logs land in
build/sound-check/.
"""

import argparse
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "sound-check"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
GAME_TIMEOUT_SECONDS = 60

PCM, IEEE_FLOAT = 1, 3
# name: (format, bits, rate, channels, seconds)
TEST_SOUNDS = {
    "mono8_11k": (PCM, 8, 11025, 1, 1.0),
    "mono16_22k": (PCM, 16, 22050, 1, 1.0),
    "mono16_48k": (PCM, 16, 48000, 1, 1.0),
    "mono16_96k": (PCM, 16, 96000, 1, 1.0),
    "stereo16_44k": (PCM, 16, 44100, 2, 2.0),
    "mono24_48k": (PCM, 24, 48000, 1, 1.0),
    "stereo24_48k": (PCM, 24, 48000, 2, 1.0),
    "float32_48k": (IEEE_FLOAT, 32, 48000, 1, 1.0),
    "long_3min_44k": (PCM, 16, 44100, 1, 180.0),
}
BACKENDS = {
    "openal": {"settings": ["+set", "s_openal", "1"], "environment": {"ALSOFT_DRIVERS": "null"}},
    "sdl": {"settings": ["+set", "s_openal", "0"], "environment": {"SDL_AUDIODRIVER": "dummy"}},
}
LENGTH_TOLERANCE = 0.05  # seconds


def write_wav(path, value_format, bits, rate, channels, seconds):
    """A 440 Hz tone (left) / 660 Hz (right), half volume."""
    frames = int(rate * seconds)
    sample_bytes = bits // 8
    data = bytearray()
    for frame in range(frames):
        for channel in range(channels):
            value = 0.5 * math.sin(2 * math.pi * (440 if channel == 0 else 660) * frame / rate)
            if value_format == IEEE_FLOAT:
                data += struct.pack("<f", value)
            elif bits == 8:
                data += struct.pack("<B", int(128 + value * 127))
            elif bits == 16:
                data += struct.pack("<h", int(value * 32767))
            elif bits == 24:
                data += int(value * 8388607).to_bytes(3, "little", signed=True)
    fmt = struct.pack("<HHIIHH", value_format, channels, rate, rate * channels * sample_bytes,
                      channels * sample_bytes, bits)
    riff = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"RIFF" + struct.pack("<I", len(riff)) + riff)


def run(backend, data_dir):
    log_path = OUTPUT_DIR / f"{backend}.log"
    with tempfile.TemporaryDirectory(prefix="optech2-sound-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        for name, (value_format, bits, rate, channels, seconds) in TEST_SOUNDS.items():
            write_wav(game_dir / "sound" / "optech2" / f"{name}.wav", value_format, bits, rate, channels, seconds)
        script = [f'delay {2000 + index * 200} "play optech2/{name}.wav"' for index, name in enumerate(TEST_SOUNDS)]
        script += ['delay 5000 "soundlist"', 'delay 5500 "quit"']
        (game_dir / "sound.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", **BACKENDS[backend]["environment"])
        settings = ["+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "s_initsound", "1",
                    *BACKENDS[backend]["settings"]]
        with open(log_path, "w") as log_file:
            try:
                subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(data_dir), *settings,
                                "+exec", "sound.cfg"], env=environment, stdout=log_file, stderr=subprocess.STDOUT,
                               timeout=GAME_TIMEOUT_SECONDS, check=False)
            except subprocess.TimeoutExpired:
                pass
    return log_path.read_text(errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--backend", choices=list(BACKENDS), help="test only this backend")
    arguments = parser.parse_args()

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []
    for backend in BACKENDS:
        if arguments.backend and backend != arguments.backend:
            continue
        log = run(backend, arguments.datadir)
        backend_line = re.search(r"(OpenAL|SDL)[^\n]*(initialized|started)[^\n]*", log, re.IGNORECASE)
        print(f"{backend}: {backend_line.group(0).strip() if backend_line else 'backend start line not found'}")
        # soundlist: "L(16b)   size(1 ch) name -6.0 dB 1.0s:begin..attack..fade..end"
        listed = {match.group(4): (int(match.group(1)), int(match.group(3)), float(match.group(6)), float(match.group(5)))
                  for match in re.finditer(r"\((\s*\d+)b\)\s+(-?\d+)\((\d) ch\) optech2/(\w+)\.wav\s+(\S+) dB ([\d.]+)s", log)}
        # every file holds the same half volume tone: wider formats must measure like 16 bit
        reference_volume = listed.get("mono16_22k", (0, 0, 0, None))[3]
        for name, (_value_format, bits, _rate, channels, seconds) in TEST_SOUNDS.items():
            if name not in listed:
                problem = re.search(rf"[^\n]*optech2/{name}[^\n]*", log)
                results.append(False)
                print(f"  FAIL  {name:<15} not loaded ({problem.group(0).strip() if problem else 'no message'})")
                continue
            loaded_bits, loaded_channels, loaded_seconds, volume = listed[name]
            passed = abs(loaded_seconds - seconds) <= max(LENGTH_TOLERANCE, seconds * 0.01) and loaded_channels == channels
            if bits > 8 and reference_volume is not None:
                passed = passed and abs(volume - reference_volume) <= 0.5
            results.append(passed)
            print(f"  {'ok' if passed else 'FAIL':<6}{name:<15} {loaded_bits:2d} bit, {loaded_channels} ch, "
                  f"{loaded_seconds:.1f} s, {volume:.1f} dB (file: {bits} bit, {channels} ch, {seconds:.1f} s)")

    passed = all(results) and bool(results)
    print(f"\nSound check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
