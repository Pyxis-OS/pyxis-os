#!/usr/bin/env python3
"""Convert the owner's bounded RGBA icon sheet to four 24x24 ink masks."""
import argparse
import struct
import zlib
from pathlib import Path


def masks(source):
    data = source.read_bytes()
    if len(data) > 65536 or data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('expected a bounded PNG sheet')
    offset, pixels, header, ended = 8, bytearray(), None, False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError('truncated PNG chunk')
        length = struct.unpack_from('>I', data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 8 + length
        if end + 4 > len(data):
            raise ValueError('truncated PNG payload')
        payload = data[offset + 8:end]
        crc = struct.unpack_from('>I', data, end)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError('PNG CRC mismatch')
        if kind == b'IHDR':
            if header is not None or offset != 8 or length != 13:
                raise ValueError('invalid PNG header')
            header = struct.unpack('>IIBBBBB', payload)
        elif kind == b'IDAT':
            pixels.extend(payload)
        elif kind == b'IEND':
            if length or end + 4 != len(data):
                raise ValueError('invalid PNG end')
            ended = True
        elif not kind[0] & 32:
            raise ValueError('unsupported critical PNG chunk')
        offset = end + 4
    if not ended or header != (192, 24, 8, 6, 0, 0, 0):
        raise ValueError('expected noninterlaced 192x24 8-bit RGBA sheet')
    stride = 192 * 4
    expected = (stride + 1) * 24
    decoder = zlib.decompressobj()
    raw = decoder.decompress(pixels, expected + 1)
    if len(raw) != expected or not decoder.eof or decoder.unused_data:
        raise ValueError('invalid PNG pixel extent')
    result = [[0] * 24 for _ in range(4)]
    previous = bytearray(stride)
    for y in range(24):
        start = y * (stride + 1)
        mode = raw[start]
        if mode > 4:
            raise ValueError('invalid PNG filter')
        row = bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = row[i - 4] if i >= 4 else 0
            up = previous[i]
            corner = previous[i - 4] if i >= 4 else 0
            predict = 0
            if mode == 1:
                predict = left
            elif mode == 2:
                predict = up
            elif mode == 3:
                predict = (left + up) // 2
            elif mode == 4:
                p = left + up - corner
                distances = (abs(p - left), abs(p - up), abs(p - corner))
                predict = (left, up, corner)[distances.index(min(distances))]
            row[i] = (row[i] + predict) & 255
        for x in range(96):
            pixel = bytes(row[x * 4:x * 4 + 4])
            if pixel == b'\x00\x00\x00\xff':
                result[x // 24][y] |= 1 << (x % 24)
            elif pixel != b'\xff\x00\xff\xff':
                raise ValueError('volume slots must contain black ink or magenta key')
        previous = row
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    rows = masks(args.source)
    lines = ['/* Generated from owner artwork; see assets/ui/volume/README.md. */',
             '#ifndef KERNEL_VOLUME_ICONS_H', '#define KERNEL_VOLUME_ICONS_H',
             '#include <stdint.h>', '', 'static const uint32_t volume_icon_masks[4][24] = {']
    for icon in rows:
        lines.append('  {')
        for start in range(0, 24, 6):
            lines.append('    ' + ', '.join(f'UINT32_C(0x{v:06x})' for v in icon[start:start + 6]) + ',')
        lines.append('  },')
    lines.extend(['};', '', '#endif', ''])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines))


if __name__ == '__main__':
    main()
