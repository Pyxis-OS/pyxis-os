#!/usr/bin/env python3
"""Generate the Bizcat-geometry glyphs for box drawing, block elements and braille.

The output, kernel/fb/bizcat_generated.inc, is committed, so building the
kernel needs no Python and the glyph bytes are reviewable. Regenerate with:

    python3 scripts/bizcat-glyphs.py

Nothing is drawn by hand. Bizcat's cell is 8x16 with 2-pixel strokes through
the centre (columns 3-4, rows 7-8). Light strokes use that band, heavy strokes
widen it to four pixels and double lines use two 2-pixel strokes with a
2-pixel gap. Corners drop the outer pixel, as Bizcat's own corners do. The
script checks that the light box characters Bizcat already has come out
byte-identical, so the generated and original glyphs stay one family.

Slot layout, which lib/terminal_text.c must match: slot 0x100 onward holds
U+2500-257F, then U+2580-259F, then U+2800-28FF.
"""
import re
import sys
import unicodedata
from pathlib import Path

WIDTH, HEIGHT = 8, 16
ROOT = Path(__file__).resolve().parent.parent
FONT_SOURCE = ROOT / 'kernel/fb/font.c'
OUTPUT = ROOT / 'kernel/fb/bizcat_generated.inc'

# Slot order: must match the ranges in lib/terminal_text.c.
RANGES = [(0x2500, 0x257f), (0x2580, 0x259f), (0x2800, 0x28ff)]
FIRST_SLOT = 0x100

# Bizcat slots that already hold these box characters.
BIZCAT_BOX = {
    0x2500: 0x01, 0x2502: 0x00, 0x250c: 0x05, 0x2510: 0x06, 0x2514: 0x04,
    0x2518: 0x03, 0x251c: 0x09, 0x2524: 0x0a, 0x252c: 0x07, 0x2534: 0x08,
    0x253c: 0x02,
}

NONE, LIGHT, HEAVY, DOUBLE = 0, 1, 2, 3

# Inclusive pixel ranges of each stroke, per weight.
VERTICAL_BANDS = {LIGHT: [(3, 4)], HEAVY: [(2, 5)], DOUBLE: [(1, 2), (5, 6)]}
HORIZONTAL_BANDS = {LIGHT: [(7, 8)], HEAVY: [(6, 9)], DOUBLE: [(5, 6), (9, 10)]}
CENTER_COLUMNS = VERTICAL_BANDS[LIGHT][0]
CENTER_ROWS = HORIZONTAL_BANDS[LIGHT][0]

WEIGHTS = {'LIGHT': LIGHT, 'SINGLE': LIGHT, 'HEAVY': HEAVY, 'DOUBLE': DOUBLE}
DIRECTIONS = {'UP': 'u', 'DOWN': 'd', 'LEFT': 'l', 'RIGHT': 'r',
              'VERTICAL': 'ud', 'HORIZONTAL': 'lr'}


def blank():
    return [[False] * WIDTH for _ in range(HEIGHT)]


def fill(grid, rows, columns):
    for y in rows:
        for x in columns:
            grid[y][x] = True


def rows_of(first, last):
    return range(first, last + 1)


def encode(grid):
    """One byte per row; bit 0 is the leftmost pixel, as in Bizcat."""
    return bytes(sum(1 << x for x in range(WIDTH) if row[x]) for row in grid)


def flip_horizontal(grid):
    return [row[::-1] for row in grid]


def flip_vertical(grid):
    return grid[::-1]


def parse_arms(name):
    """Map a BOX DRAWINGS name such as 'DOWN LIGHT AND RIGHT HEAVY' to arm weights."""
    segments = name.split(' AND ')
    parsed = []
    for segment in segments:
        weight, arms = None, ''
        for word in segment.split():
            if word in WEIGHTS:
                weight = WEIGHTS[word]
            else:
                arms += DIRECTIONS[word]
        parsed.append((weight, arms))
    default = next((weight for weight, _ in parsed if weight), None)
    result = {arm: NONE for arm in 'udlr'}
    for weight, arms in parsed:
        for arm in arms:
            result[arm] = weight or default
    return result


def vertical_stop(arms, direction, stroke):
    """Row where a vertical stroke ends: up arms stop there, down arms start."""
    up = direction == 'u'
    left, right = arms['l'], arms['r']
    if not left and not right:
        # A stub or straight line meets the opposite arm at the centre.
        return CENTER_ROWS[0] if up else CENTER_ROWS[1]
    horizontal = [band for arm in 'lr' for band in HORIZONTAL_BANDS.get(arms[arm], [])]
    if DOUBLE not in (left, right):
        span = (min(b[0] for b in horizontal), max(b[1] for b in horizontal))
        return span[1] if up else span[0]
    near, far = HORIZONTAL_BANDS[DOUBLE][0 if up else 1], HORIZONTAL_BANDS[DOUBLE][1 if up else 0]
    # A single stem runs through the gap and joins both strokes.
    joined = arms[direction] == DOUBLE and (left if stroke[1] < 4 else right)
    chosen = near if joined else far
    return chosen[1] if up else chosen[0]


def horizontal_stop(arms, direction, stroke):
    """Column where a horizontal stroke ends: left arms stop there, right arms start."""
    leftward = direction == 'l'
    up, down = arms['u'], arms['d']
    if not up and not down:
        return CENTER_COLUMNS[0] if leftward else CENTER_COLUMNS[1]
    vertical = [band for arm in 'ud' for band in VERTICAL_BANDS.get(arms[arm], [])]
    if DOUBLE not in (up, down):
        span = (min(b[0] for b in vertical), max(b[1] for b in vertical))
        return span[1] if leftward else span[0]
    near, far = VERTICAL_BANDS[DOUBLE][0 if leftward else 1], VERTICAL_BANDS[DOUBLE][1 if leftward else 0]
    joined = arms[direction] == DOUBLE and (up if stroke[1] < 8 else down)
    chosen = near if joined else far
    return chosen[1] if leftward else chosen[0]


def draw_arms(arms):
    grid = blank()
    for direction in 'ud':
        weight = arms[direction]
        for band in VERTICAL_BANDS.get(weight, []):
            stop = vertical_stop(arms, direction, band)
            rows = rows_of(0, stop) if direction == 'u' else rows_of(stop, HEIGHT - 1)
            fill(grid, rows, rows_of(*band))
    for direction in 'lr':
        weight = arms[direction]
        for band in HORIZONTAL_BANDS.get(weight, []):
            stop = horizontal_stop(arms, direction, band)
            columns = rows_of(0, stop) if direction == 'l' else rows_of(stop, WIDTH - 1)
            fill(grid, rows_of(*band), columns)
    cut_corners(grid, arms)
    return grid


def cut_corners(grid, arms):
    """Drop the outer pixel of a two-arm corner, nesting double lines."""
    vertical = [arm for arm in 'ud' if arms[arm]]
    horizontal = [arm for arm in 'lr' if arms[arm]]
    if len(vertical) != 1 or len(horizontal) != 1:
        return
    down, right = vertical[0] == 'd', horizontal[0] == 'r'
    columns = VERTICAL_BANDS[arms[vertical[0]]]
    rows = HORIZONTAL_BANDS[arms[horizontal[0]]]
    # Outermost stroke first: leftmost when the arm runs right, topmost when
    # the stem runs down.
    columns = columns if right else columns[::-1]
    rows = rows if down else rows[::-1]
    for (first_column, last_column), (first_row, last_row) in zip(columns, rows):
        x = first_column if right else last_column
        y = first_row if down else last_row
        grid[y][x] = False


def dashed(vertical, weight, dashes):
    """Centered dashes with a one-pixel gap that tiles across cells."""
    grid = blank()
    length = HEIGHT if vertical else WIDTH
    period = length / dashes
    on = [any(i * period + 0.5 <= p + 0.5 < (i + 1) * period - 0.5
              for i in range(dashes)) for p in range(length)]
    for band in (VERTICAL_BANDS if vertical else HORIZONTAL_BANDS)[weight]:
        for p in range(length):
            if on[p]:
                if vertical:
                    fill(grid, [p], rows_of(*band))
                else:
                    fill(grid, rows_of(*band), [p])
    return grid


def diagonal(rising):
    """A two-pixel diagonal from corner to corner, via distance to the line."""
    grid = blank()
    for y in range(HEIGHT):
        line = WIDTH * (1 - (y + 0.5) / HEIGHT) if rising else WIDTH * (y + 0.5) / HEIGHT
        for x in range(WIDTH):
            grid[y][x] = abs(x + 0.5 - line) < 1.0
    return grid


ARC_RADIUS = 4


def arc_down_right():
    """Quarter ring joining a stem running down to an arm running right."""
    grid = blank()
    center_x, center_y = CENTER_COLUMNS[1], CENTER_ROWS[0] + 1  # edge coordinates 4 and 8
    cx, cy = center_x + ARC_RADIUS, center_y + ARC_RADIUS
    for y in range(HEIGHT):
        for x in range(WIDTH):
            px, py = x + 0.5, y + 0.5
            if px <= cx and py <= cy:
                grid[y][x] = abs(((px - cx) ** 2 + (py - cy) ** 2) ** 0.5 - ARC_RADIUS) <= 1.0
    fill(grid, rows_of(cy, HEIGHT - 1), rows_of(*CENTER_COLUMNS))
    fill(grid, rows_of(*CENTER_ROWS), rows_of(cx, WIDTH - 1))
    return grid


def box_drawing(code):
    name = unicodedata.name(chr(code)).removeprefix('BOX DRAWINGS ')
    if ' ARC ' in name:
        grid = arc_down_right()
        if name.endswith('ARC DOWN AND LEFT'):
            return flip_horizontal(grid)
        if name.endswith('ARC UP AND RIGHT'):
            return flip_vertical(grid)
        if name.endswith('ARC UP AND LEFT'):
            return flip_vertical(flip_horizontal(grid))
        return grid
    if 'DIAGONAL' in name:
        rising = diagonal(True) if 'UPPER RIGHT TO LOWER LEFT' in name else None
        falling = diagonal(False) if 'UPPER LEFT TO LOWER RIGHT' in name else None
        grid = blank()
        for part in (rising, falling):
            if part:
                grid = [[a or b for a, b in zip(r1, r2)] for r1, r2 in zip(grid, part)]
        return grid
    if 'DASH' in name:
        match = re.fullmatch(r'(LIGHT|HEAVY) (DOUBLE|TRIPLE|QUADRUPLE) DASH (HORIZONTAL|VERTICAL)', name)
        dashes = {'DOUBLE': 2, 'TRIPLE': 3, 'QUADRUPLE': 4}[match[2]]
        return dashed(match[3] == 'VERTICAL', WEIGHTS[match[1]], dashes)
    return draw_arms(parse_arms(name))


def block_element(code):
    grid = blank()
    quadrants = {
        0x2596: 'ld', 0x2597: 'rd', 0x2598: 'lu', 0x2599: 'lu ld rd',
        0x259a: 'lu rd', 0x259b: 'lu ru ld', 0x259c: 'lu ru rd', 0x259d: 'ru',
        0x259e: 'ru ld', 0x259f: 'ru ld rd',
    }
    half_width, half_height = WIDTH // 2, HEIGHT // 2
    if code == 0x2580:
        fill(grid, range(half_height), range(WIDTH))
    elif 0x2581 <= code <= 0x2588:
        fill(grid, range(HEIGHT - (code - 0x2580) * 2, HEIGHT), range(WIDTH))
    elif 0x2589 <= code <= 0x258f:
        fill(grid, range(HEIGHT), range(WIDTH - (code - 0x2588)))
    elif code == 0x2590:
        fill(grid, range(HEIGHT), range(half_width, WIDTH))
    elif code in (0x2591, 0x2592, 0x2593):
        for y in range(HEIGHT):
            for x in range(WIDTH):
                light = x % 2 == 0 and y % 2 == 0
                grid[y][x] = {0x2591: light, 0x2592: (x + y) % 2 == 0, 0x2593: not light}[code]
    elif code == 0x2594:
        fill(grid, range(2), range(WIDTH))
    elif code == 0x2595:
        fill(grid, range(HEIGHT), [WIDTH - 1])
    else:
        for quadrant in quadrants[code].split():
            rows = range(half_height) if quadrant[1] == 'u' else range(half_height, HEIGHT)
            columns = range(half_width) if quadrant[0] == 'l' else range(half_width, WIDTH)
            fill(grid, rows, columns)
    return grid


def braille(code):
    """Two columns of four 2x2 dots; bit n of the offset into U+2800 is dot n+1."""
    grid = blank()
    code -= 0x2800
    dots = [(0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (1, 2), (0, 3), (1, 3)]
    for bit, (column, row) in enumerate(dots):
        if code >> bit & 1:
            x, y = column * 4 + 1, row * 4 + 1
            fill(grid, [y, y + 1], [x, x + 1])
    return grid


def glyph(code):
    if code < 0x2580:
        return box_drawing(code)
    if code < 0x25a0:
        return block_element(code)
    return braille(code)


def bizcat_glyphs():
    text = FONT_SOURCE.read_text()
    body = text[text.index('bizcat_data[] = {'):text.index('};')]
    values = [int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', body)]
    return [bytes(values[i:i + 16]) for i in range(0, len(values), 16)]


def check_against_bizcat(generated):
    original = bizcat_glyphs()
    for code, slot in BIZCAT_BOX.items():
        if generated[code] != original[slot]:
            sys.exit(f'U+{code:04X} differs from Bizcat slot {slot:#04x}')


def main():
    generated = {code: encode(glyph(code))
                 for first, last in RANGES for code in range(first, last + 1)}
    check_against_bizcat(generated)

    lines = ['/* Generated by scripts/bizcat-glyphs.py; do not edit.',
             ' * Slots 0x100 onward, in the order of the ranges below. These glyphs',
             ' * are not part of Bizcat: they are drawn from its 8x16 geometry. */']
    for first, last in RANGES:
        for code in range(first, last + 1):
            data = ', '.join(f'0x{b:02x}' for b in generated[code])
            lines.append(f'  {data}, /* U+{code:04X} */')
    OUTPUT.write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
