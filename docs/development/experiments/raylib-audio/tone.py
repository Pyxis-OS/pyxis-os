#!/usr/bin/env python3
"""Author the manual fixture: mono 440 Hz, 22050 Hz S16, peak 8192."""
import math
from pathlib import Path
import struct
import sys
import wave

if len(sys.argv) != 2:
    raise SystemExit("usage: tone.py OUTPUT.wav")
output = Path(sys.argv[1])
with wave.open(str(output), "wb") as target:
    target.setnchannels(1)
    target.setsampwidth(2)
    target.setframerate(22050)
    target.writeframes(b"".join(struct.pack("<h", round(8192 * math.sin(
        2 * math.pi * 440 * frame / 22050))) for frame in range(22050)))
