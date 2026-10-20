# Remote Neovim message scrolling

## Cause and change

The Pyxis Neovim profile advertises scroll regions and insert/delete lines.
The host renderer ignored `CSI r` and `CSI L/M`, while the local TTY implemented
them. Neovim moved earlier message lines using those controls, then wrote later
lines at absolute positions. The client overwrote text that should have moved.
A raw machine-mode capture contains every message line, including the lines
missing from the interactive tmux pane. The failure is in client presentation.

The client now tracks the scroll region, applies its vertical movement bounds
and line operations, and handles region LF/reverse index. Application screen
transitions and applied geometry reset the region; repeated screen selection
leaves it alone. Blank rows use current colours without glyph attributes.
The protocol and Neovim profile are unchanged. The host alternate screen remains
connection-owned, with existing application screen/cursor and host scrollback
limits; this is not full VT compatibility. Clipped pending-resize output cannot
recover hidden rows until applied geometry and repaint.

## Correctness qualification

Baseline client: `d3a65acd4206f97ed699dd45d89fbe66dceb99d6`; candidate:
the client changes in this PR. Target kernel and clean archive are frozen main
`e61886f38bd3ccfc4142e42e421996c1d4e3336c`, userland
`c3be6fe69cd6a252955cebea7e6023423aa8ffb3`, ports
`e9b0eb4a1854d0e38313341aa46074c2fa1c8ae2`. The intervening parent changes are
documentation-only. No compiler container or dependency-pin change is needed.

QEMU 10.2.2, Q35/KVM, four CPUs, 2 GiB, Fedora 44 OVMF and modern VirtIO-net;
ordinary Remote is forwarded only to localhost. The host client runs in isolated
tmux 3.7c, 80×24, with an 80×20 remote viewport. `make -C tools remote` passes.
The target uses the unchanged native Neovim bundle:

```text
nvim -u NONE -n
:echo "LINE-01\nLINE-02\nLINE-03\nLINE-04\nLINE-05\nLINE-06\nLINE-07\nLINE-08\nLINE-09\nLINE-10\nLINE-11\nLINE-12"
:set shell=shell shellcmdflag=-c noshelltemp
:!lua -e 'for i=1,12 do print("CMD"..i) end'
```

The shell options explicitly restore the bundled defaults skipped by `-u NONE`.
The baseline loses `LINE-01/02` and `CMD1` from the visible grid. The corrected
client displays all twelve lines of both workloads and returns to the shell.
The raw message stream includes `CSI 11M` inside a selected region.

Interactive Lua output also checks region-local LF, multiple-line deletion and
insertion, reverse index, up/down margin bounds, malformed/out-of-range regions,
LF below a region at the physical bottom, and application screen-transition
resets/idempotence. A one-row viewport clears only its row during a capped line
operation, preserving a sentinel on an outside host row. Sentinel rows outside
the region remain visible. Raw captures
and displayed grids are under `/shared/present/remote-message-scroll/`.

These are QEMU correctness observations only, collected during the horse CPU-load
hold. No timings, performance comparison or native run is reported.
