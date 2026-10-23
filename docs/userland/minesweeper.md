# Minesweeper

[Andrew Hamel's Raylib Minesweeper](https://github.com/AndrewHamel111/raylib-minesweeper)
is an opt-in public program package. Configure a network repository with
[`pkg`](../interfaces/packages.md#package-client), then launch it in an
application space:

```text
pkg install minesweeper
minesweeper
```

Left click reveals a cell; right click toggles a flag on an unrevealed cell.
Flagged cells cannot be revealed. `R` starts a new game at any time, and Escape
exits. The board has 15×15 cells and 22 mines; the first click can lose. Reveal
all safe cells to win. The end screen shows elapsed time and restart instructions.

The game draws a fixed 600×600 logical canvas through raylib's SDL2 software
renderer, capped at 60 FPS. Output resizing preserves that square canvas with
aspect-fit scaling and letterbox; pointer coordinates follow the displayed board.
It requires display, keyboard, pointer, memory and clock READ/PRECISE/SLEEP
grants. The bundled flag PNG is read through the private read-only `app://`
root. Missing display initialization or texture data produces an error and exits.

There are no saves, scores, configuration files or audio. The package includes
the unchanged flag PNG and upstream MIT license, plus raylib/rlsw and SDL2
notices, provenance and the MPL-covered Pyxis SDL backend source. Unused
upstream WAV files are omitted. Games are installed through the
repository and are outside the boot archive.

The [port recipe](../../ports/minesweeper/README.md) records source and adaptation
details; [package instructions](../../packages/README.md#game-packages) cover
building and publishing the package for a system.

[Package/gameplay qualification](../development/experiments/minesweeper/README.md)
covers installation, input, win/loss/reset and display release. Chronium also
qualified native vela drawing, clicks, reset and exit on 2026-10-23.
