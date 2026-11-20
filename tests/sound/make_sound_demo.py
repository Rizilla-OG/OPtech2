#!/usr/bin/env python3
"""Writes a listening demo of the sound formats OPtech2 loads, and a console
script that plays them one after another with a label on screen:

   1  8 bit 11 kHz mono        (stock Quake II quality, sounds dull)
   2  16 bit 22 kHz mono       (stock Quake II's best)
   3  16 bit 48 kHz mono
   4  16 bit 96 kHz mono
   5  24 bit 48 kHz mono       (new: was noise under OpenAL)
   6  32 bit float 48 kHz mono (new: was refused)
   7  16 bit 44.1 kHz stereo   (ping-pong left / right)
   8  24 bit stereo, extensible WAV header (new: was refused), pan sweep
   9  30 second piece          (long sound)

Each mono clip is the same melody, so the formats can be compared by ear.

Files: <game dir>/sound/optech2/demo/*.wav and <game dir>/optech2_sound_demo.cfg.
Play it in the game console:  exec optech2_sound_demo.cfg
(tools/install_test_content.py installs it with the test maps.)

Usage:
  make_sound_demo.py <game dir>     e.g. ~/.local/share/OPtech2/baseq2
"""

import math
import struct
import sys
from pathlib import Path

PCM, IEEE_FLOAT, EXTENSIBLE = 1, 3, 0xFFFE
NOTES = [523.25, 659.25, 783.99, 1046.50]  # C5 E5 G5 C6
GAP_SECONDS = 1.2


def tone(frequency, time, length):
    """A plucked note with a few harmonics, fading out over its length."""
    if time < 0 or time > length:
        return 0.0
    envelope = min(1.0, time / 0.01) * math.exp(-3.0 * time / length)
    value = 0.0
    for harmonic, strength in ((1, 1.0), (2, 0.45), (3, 0.25), (5, 0.12), (8, 0.06)):
        value += strength * math.sin(2 * math.pi * frequency * harmonic * time)
    return 0.35 * envelope * value


def melody(time):
    """The four notes one after another, then all together as a chord."""
    value = sum(tone(frequency, time - index * 0.35, 0.6) for index, frequency in enumerate(NOTES))
    value += 0.5 * sum(tone(frequency, time - 1.5, 1.2) for frequency in NOTES)
    return value


def ping_pong(time):
    """(left, right): notes alternating between the ears."""
    left = right = 0.0
    for index, frequency in enumerate(NOTES * 2):
        note = tone(frequency, time - index * 0.3, 0.4)
        if index % 2 == 0:
            left += note
        else:
            right += note
    return left, right


def pan_sweep(time, length):
    """(left, right): a held chord moving from left to right and back."""
    value = 0.3 * sum(math.sin(2 * math.pi * frequency * time) for frequency in NOTES[:3])
    value *= min(1.0, time / 0.05, (length - time) / 0.2)
    position = 0.5 - 0.5 * math.cos(2 * math.pi * time / length)  # 0 = left, 1 = right
    return value * math.cos(position * math.pi / 2), value * math.sin(position * math.pi / 2)


def long_piece(time):
    """30 seconds of slow arpeggios over changing chords."""
    chords = [(261.63, 329.63, 392.00), (220.00, 261.63, 329.63), (174.61, 220.00, 261.63), (196.00, 246.94, 293.66)]
    chord = chords[int(time // 7.5) % len(chords)]
    step = time % 0.5
    frequency = chord[int(time / 0.5) % 3] * (2 if int(time / 1.5) % 2 else 1)
    pad = 0.08 * sum(math.sin(2 * math.pi * note * time) for note in chord)
    fade = min(1.0, time / 1.0, (30.0 - time) / 2.0)
    return fade * (pad + 0.7 * tone(frequency, step, 0.5))


def encode(value, value_format, bits):
    value = max(-1.0, min(1.0, value))
    if value_format == IEEE_FLOAT:
        return struct.pack("<f", value)
    if bits == 8:
        return struct.pack("<B", int(128 + value * 127))
    if bits == 16:
        return struct.pack("<h", int(value * 32767))
    return int(value * 8388607).to_bytes(3, "little", signed=True)


def write_wav(path, value_format, bits, rate, channels, seconds, signal, extensible=False):
    """signal(time) -> value (mono) or (left, right) (stereo)."""
    data = bytearray()
    for frame in range(int(rate * seconds)):
        sample = signal(frame / rate)
        for value in (sample if channels == 2 else (sample,)):
            data += encode(value, value_format, bits)
    block = channels * bits // 8
    if extensible:
        guid_tail = bytes.fromhex("000000001000800000aa00389b71")
        fmt = struct.pack("<HHIIHHHHI", EXTENSIBLE, channels, rate, rate * block, block, bits, 22, bits,
                          0x3 if channels == 2 else 0x4) + struct.pack("<H", value_format) + guid_tail
    else:
        fmt = struct.pack("<HHIIHH", value_format, channels, rate, rate * block, block, bits)
    riff = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"RIFF" + struct.pack("<I", len(riff)) + riff)


# (file, label, format, bits, rate, channels, seconds, signal, extensible)
CLIPS = [
    ("1_8bit_11k", "1/9  8 bit 11 kHz mono - stock Quake II quality", PCM, 8, 11025, 1, 3.0, melody, False),
    ("2_16bit_22k", "2/9  16 bit 22 kHz mono - stock best", PCM, 16, 22050, 1, 3.0, melody, False),
    ("3_16bit_48k", "3/9  16 bit 48 kHz mono", PCM, 16, 48000, 1, 3.0, melody, False),
    ("4_16bit_96k", "4/9  16 bit 96 kHz mono", PCM, 16, 96000, 1, 3.0, melody, False),
    ("5_24bit_48k", "5/9  24 bit 48 kHz mono - new (was noise)", PCM, 24, 48000, 1, 3.0, melody, False),
    ("6_float_48k", "6/9  32 bit float 48 kHz mono - new (was refused)", IEEE_FLOAT, 32, 48000, 1, 3.0, melody, False),
    ("7_stereo_44k", "7/9  16 bit 44.1 kHz stereo - left / right ping-pong", PCM, 16, 44100, 2, 3.0, ping_pong, False),
    ("8_stereo24_ext", "8/9  24 bit stereo, extensible WAV header - new - pan sweep", PCM, 24, 48000, 2, 4.0,
     lambda time: pan_sweep(time, 4.0), True),
    ("9_long_30s", "9/9  30 second piece - long sound", PCM, 16, 44100, 1, 30.0, long_piece, False),
]


def write_demo(game_dir):
    game_dir = Path(game_dir)
    lines = ['echo "OPtech2 sound demo: 9 clips, about 70 seconds"']
    time_ms = 1500
    for file_name, label, value_format, bits, rate, channels, seconds, signal, extensible in CLIPS:
        write_wav(game_dir / "sound" / "optech2" / "demo" / f"{file_name}.wav", value_format, bits, rate, channels,
                  seconds, signal, extensible)
        lines.append(f'delay {time_ms} "echo {label}"')
        lines.append(f'delay {time_ms + 400} "play optech2/demo/{file_name}.wav"')
        time_ms += int((seconds + GAP_SECONDS) * 1000) + 400
    lines.append(f'delay {time_ms} "echo OPtech2 sound demo finished. soundlist shows how each one loaded."')
    (game_dir / "optech2_sound_demo.cfg").write_text("\n".join(lines) + "\n")
    return len(CLIPS), time_ms / 1000


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    count, seconds = write_demo(sys.argv[1])
    print(f"wrote {count} clips and optech2_sound_demo.cfg ({seconds:.0f} s) into {sys.argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
