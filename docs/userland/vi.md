# vi

The image includes BusyBox vi at `boot://vi.pxe`, with its GPL-2.0-only license
at `boot://share/licenses/busybox/LICENSE`. The shell resolves `vi` to it. It is
the first modal editor, ahead of [Neovim](../wip/neovim-libuv.md); Kilo remains
available. The [recipe notes](../../ports/busybox/README.md) record the source
pin, both patches and the adapter.

```text
vi hello.c
vi -R host://notes.txt
vi -c 'set ts=4' home://a.txt home://b.txt
```

Relative paths use the inherited working directory. vi draws on the named
`input`/`output` console grants through libterm, as Kilo does. It holds
[Ctrl+C passthrough](foreground-interruption.md) for the whole session, so the
shell cannot terminate it and discard unsaved edits. It runs on the framebuffer
console and through the [remote terminal](remote-terminal.md).

## Supported editing

The build enables:

- counts, operators with motions, registers and marks;
- dot repeat and undo with its queue;
- BRE search with `/`, `?`, `n` and `N`, and `:s` substitution;
- `:set` options, multiple files with `:n`, `:e`, `:r` and `:w NAME`;
- read-only viewing (`-R`) and startup commands from `-c` or `EXINIT`.

Arrows, Home/End, Delete and Page Up/Down come from libterm's key decoder, and
a standalone Escape is recognized after 100 ms. The screen size is re-read
at each redraw.

The packaged [session configuration](session-configuration.md) sets `EXINIT`
to `set ts=3 et ai`:

- tab stops every three columns;
- Tab inserts spaces;
- new lines keep the previous line's indent.

There is no `shiftwidth`, so `>>` and `<<` shift by one tab stop. `-c` commands
run after `EXINIT`, so `vi -c 'set noet' Makefile` types real tabs for one
session. Tabs already in a file stay tabs.

## How the port maps onto Pyxis

Only `editors/vi.c` is built. A recipe-local `libbb.h`, feature header and
adapter replace BusyBox's Kconfig and libbb. The adapter covers only the
helpers vi references, so another BusyBox applet would extend it. The
patch changes only the Unix-specific parts:

| Upstream | Pyxis |
| --- | --- |
| termios raw/cooked mode, `VERASE` | libterm passthrough; console input is already raw. BS and DEL both erase. |
| `poll` on stdin | A timed libterm key read that keeps the key for the next read |
| stdout drawing, alternate screen | Buffered output to the named console, flushed before waiting for input. The screen is cleared on start and exit. |
| `fstat` size and `S_ISREG` | Read to EOF. Opening a directory fails with its native error. |
| `access(W_OK)` and mode bits | `[Readonly]` when the file cannot be opened for WRITE now |
| `stat` before `:w NAME` | Refuse unless opening NAME reports ENOENT; `:w!` overrides |
| `~/.exrc` owner/mode check | Not read; `EXINIT` from session configuration still works |

Userland libc gained `ftruncate` over native `FILE_RESIZE`, plus `memrchr`,
`strchrnul` and `stpcpy`. A save keeps upstream's order: open without
truncation, write, then truncate to the bytes written. Fastfetch now uses the
libc `memrchr` instead of its bundled fallback.

Patch 0002 fixes an upstream defect: the per-file read-only bit was never
cleared, so after one read-only file every later `:n`/`:e` file was also
treated as read-only.

Patch 0001 adapts GNU regex calls to the
[libc POSIX interface](libc-portability.md#regular-expressions-and-utf-8-conversion).
Search uses bounded, NUL-terminated copies; substitution captures count bytes
in the original line. Compile failures report `regerror`, preserve the cursor
and avoid freeing an unsuccessfully compiled pattern. Global substitutions
keep line anchors and advance after zero-length matches.

## Limits

- **Display:** ASCII only. Control characters display as `^X`, and bytes above
  127 display as `.`.
- **Search:** BRE, with ASCII-only character classes/case folding and the
  [pinned TRE back-reference limits](../technical-debt.md#regex-character-classes-and-back-references).
  Owner decision, 2026-10-07: regex matching stops at an embedded NUL within
  each copied search/substitution slice. The renderer remains ASCII even when
  the regex engine consumes a whole UTF-8 code point.
- **Shell:** there is no `:!` and no shell filters.
- **Screen size:** a change takes effect at the next redraw, since there is no
  resize notification.
- **Saves:** not atomic. A short write leaves the file overwritten and
  truncated at that point, and a crash between the write and the resize can
  leave old trailing bytes.
- **Input EOF and failures:** input EOF ends vi as upstream does, losing unsaved
  edits; this comes from source inspection and was not exercised. Allocation or
  terminal output failure also exits and loses unsaved edits.

See [technical debt](../technical-debt.md#vi-port-limits) for revisit points.

## Why BusyBox

The selection compared BusyBox `f96d33d`, toybox `b7ec52a` and neatvi `26e9cad`
using host import analysis and a compile probe against the Pyxis SDK.

- **BusyBox** was chosen. It draws with cursor positioning, line/screen erase
  and reverse video, all inside the existing [VT subset](terminal.md#tty-output-controls).
  Its missing facilities sat in a few small functions, and Kconfig disables
  unsupported signals and `:!` cleanly. Regex now uses the libc interface.
- **toybox** was rejected. Its vi is unfinished upstream, keeps the file
  memory-mapped as piece-table storage, always requires regex and assumes UTF-8
  widths.
- **neatvi** was rejected. It needs scroll regions and insert/delete line,
  which the terminal ignores, and uses fork/exec and sockets for filters and LSP.

The owner agreed on 2026-10-04 to use BusyBox, keep upstream's
write-then-truncate save via a libc `ftruncate`, and add the string functions to
libc. `stat`, `fstat` and `access` stay out of libc until truthful file metadata
exists.

## Regex validation (2026-10-07)

Ordinary SDK/image builds passed with the existing compiler. The complete patch
series applied to the exact BusyBox pin; vi/less and their adapter compiled
without warnings. Interactive QEMU 10.2.2/OVMF used nested KVM, four CPUs,
512 MiB, virtio-net/rng, no raw disks, the 1280×800 framebuffer and an 80×24
remote terminal. A Linux BusyBox vi built from the same pin used the same
240-line file and `EXINIT` options. The current-line captures agreed:

| Command sequence | Matched line on Linux and Pyxis |
| --- | ---: |
| `/fo\+` from the beginning | 4 |
| `/^#include` after that match | 120 |
| `n`, wrapping forward | 2 |
| `N`, wrapping backward | 120 |
| `?fo\+` | 5 |

`:%s/old/new/g` made 475 substitutions on 238 lines in both editors. The saved
240-line results compared byte-for-byte, with SHA-256
`2132d572b1a4de6d44a5f007b6d29d9e2d0b9115de0969f7694863f1921e4c53`.
Search/substitution also worked on the framebuffer. `?[` at the first character
and a malformed substitution both reported `Missing ']'` without moving the
cursor or crashing. Pyxis's empty-match progress checks gave one prefix for
`:s/^/X/g`, and `_a_b_c_` for `:s/x*/_/g` on `abc`, including the terminal empty
match. These guarded cases fix the newly enabled upstream loop; they were not
run through the looping Linux path. GDB stopped in the adapted `char_search`
with the `fo\+` pattern and forward/full range. All validation processes stopped;
fixtures, capture files and screenshots remain uncommitted.

## Initial port validation

The validation build used pyxis-os sources identical outside `docs/` to
`aef3c26`, userland `877d04f` and ports `55b6f8e`, with `make -j16 image` and no
compiler warnings in vi. It booted four CPUs under
nested KVM with patched QEMU 10.2.2, virtio-net and a virtio-fs export. The
following was exercised:

- **Remote terminal (100×29):**
  - editing, search, `:w` on `host://`; the file shrank from 33 to 25 bytes
    with no stale tail;
  - the `:q` guard, then `:w NAME` creating a file and refusing to replace it
    on the second try;
  - Ctrl+C delivered to vi as input;
  - `[Readonly]` and the denied `:w!` on `boot://`;
  - `-R`, ordered `-c` commands and `:n` across files;
  - yank/put with counts, dot repeat, undo, `:s`, `:set` and `:features`;
  - a `:/pattern/` address and `:list`, plus fastfetch using libc `memrchr`.
- **Large file:** the 1.67 MB, 43,261-line `boot://share/hwdata/pci.ids` opened
  in under 0.5 s, including the polling interval, and `G` reached the end.
- **Framebuffer console (QEMU `sendkey`):** full-screen drawing, arrows, `A`,
  Escape and `:wq` to `host://`, with a clean prompt after exit.

`vi.pxe` is 96,720 bytes; Kilo's is 68,960.
