#!/usr/bin/env python3
"""Convert the owner's mono PCM8 power-overlay sound to 48 kHz stereo PCM16."""
import argparse
import wave
from pathlib import Path


SOURCE_RATE = 44100
OUTPUT_RATE = 48000


def read_sound(source):
    with wave.open(str(source), 'rb') as sound:
        if (sound.getnchannels() != 1 or sound.getsampwidth() != 1
                or sound.getframerate() != SOURCE_RATE
                or sound.getcomptype() != 'NONE'):
            raise ValueError('expected uncompressed mono unsigned PCM8 at 44100 Hz')
        frames = sound.getnframes()
        samples = sound.readframes(frames)
    if not frames or len(samples) != frames:
        raise ValueError('expected nonempty, complete WAV sample data')
    return samples


def write_header(samples, output):
    frames = (len(samples) * OUTPUT_RATE + SOURCE_RATE - 1) // SOURCE_RATE
    lines = ['/* Generated from owner audio; see assets/ui/power/README.md. */',
             '#ifndef KERNEL_UI_SOUND_H', '#define KERNEL_UI_SOUND_H',
             '#include <stdint.h>', '',
             f'#define AUDIO_UI_SOUND_FRAMES {frames}', '',
             'static const int16_t audio_ui_sound_pcm[AUDIO_UI_SOUND_FRAMES * 2] = {']
    row = []
    for frame in range(frames):
        index, fraction = divmod(frame * SOURCE_RATE, OUTPUT_RATE)
        left = samples[index] - 128
        right = samples[min(index + 1, len(samples) - 1)] - 128
        # Convert unsigned PCM8 to signed PCM16 and interpolate without gain.
        weighted = (left * (OUTPUT_RATE - fraction) + right * fraction) * 256
        value = (weighted + OUTPUT_RATE // 2) // OUTPUT_RATE
        row.extend((str(value), str(value)))
        if len(row) == 12:
            lines.append('  ' + ', '.join(row) + ',')
            row = []
    if row:
        lines.append('  ' + ', '.join(row) + ',')
    lines.extend(['};', '', '#endif', ''])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines), encoding='ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        write_header(read_sound(args.source), args.output)
    except (OSError, EOFError, ValueError, wave.Error) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
