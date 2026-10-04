# Small vi port investigation

Status: investigation complete. BusyBox vi is the selected candidate, and the
[decisions](#agreed-decisions) below were agreed on 2026-10-04. The port itself
has not started. This compares small vi implementations as the first modal
editor, ahead of [Neovim](neovim-libuv.md). Kilo stays the current editor.

## Candidates and evidence

| Candidate | Pinned revision | License |
| --- | --- | --- |
| [BusyBox][bb] `editors/vi.c` | `f96d33d28a1f70fda5f27d221d5012b1ac0b7dad` (2026-10-04) | GPL-2.0-only |
| [toybox][tb] `toys/pending/vi.c` | `b7ec52ac35e075caffca5d330995d44e8dbfc8c3` (0.8.14) | 0BSD |
| [neatvi][nv], as a standalone reference | `26e9cadbf3828807a35bbe71f19e323e97b27439` (2026-09-28) | ISC |

How the evidence was gathered:

- **Host builds (measured).** Each candidate was built natively on Fedora
  x86-64 with glibc. BusyBox and toybox used `allnoconfig` with only vi
  enabled. BusyBox was built in three vi feature sets. neatvi used its default
  Makefile. `nm -u` on the vi object, and on the linked binary, lists the
  imported libc and platform symbols.
- **Pyxis compile probe (measured, BusyBox only).** BusyBox `vi.c` was
  cross-compiled with the proposed feature set below, minus screen-size queries
  (one more adapter helper). It used the SDK exported from Pyxis `68729a3`
  (userspace `2b23085`). A scratch header stood in for
  `libbb.h` and declared only the libbb helpers vi references. The probe was
  never linked, packaged or booted.
- **Source inspection** covers terminal escapes, file I/O and process use.
  Recent maintenance is commits touching the vi source since 2022-01-01: 20 for
  BusyBox (latest 2026-08-31) and 27 for toybox (latest 2026-03-04).

Host object sizes come from each project's own flags, so read them as
approximate. BusyBox `vi.o` with every feature enabled has 21.7 KB of text.
Toybox `vi.o` has 12.5 KB. neatvi's objects total 147 KB of text, and its
stripped binary is 180 KB with LSP support.

## Relevant Pyxis surface

Inspected in userspace `2b23085`:

- **Terminal input.** libterm already provides raw, non-echoing byte input. It
  offers `term_read_key` with a 100 ms escape-sequence gap (a standalone Escape
  returns 27) and `term_read_key_timeout`. `term_size` reports dimensions.
  [Passthrough](../userland/foreground-interruption.md) keeps Ctrl+C away from
  an editor holding unsaved edits, as Kilo does.
- **Terminal output.** It supports a [VT subset](../userland/terminal.md#tty-output-controls):
  absolute and relative cursor movement, erase line/screen, cursor visibility,
  and reverse/colour SGR. Other controls are ignored. That includes scroll
  regions, insert/delete line and the alternate screen.
- **libc files.** libc has `open`, `read`, `write` and `close` with
  `O_RDONLY`, `O_WRONLY`, `O_CREAT` and `O_TRUNC`. It also has stdio with
  `fseek`/`ftell`, atomic replacing `rename`, `mkdir`, `setjmp` and `getenv`.
  Native files support `FILE_SIZE` and `FILE_RESIZE`.
- **Absent.** termios, `poll`, signals, `stat`/`fstat`/`access`, `ftruncate`,
  `mmap`, `<regex.h>`, wide-character functions, `system`/fork/exec and user IDs.

## Findings

| | BusyBox vi | toybox vi | neatvi |
| --- | --- | --- | --- |
| Structure | One file; about 30 small libbb helpers | One file in `pending`, default off; toybox lib for terminal, lists and I/O | 20 files; own regex, LSP client, UTF-8 and bidi rendering |
| Terminal output needed | CUP, EL, ED, reverse SGR; alternate screen (ignored) | Insert/delete line for scrolling, bold/dim SGR, alternate screen | Scroll regions, insert/delete line, CHA |
| Search | Built-in literal search; regex optional | `regcomp`/`regexec` always required | Own regex |
| File loading | `fstat` size, `S_ISREG`, `access(W_OK)` read-only warning | `mmap(MAP_SHARED)` of the file as piece-table storage; `stat` for mode | `read` |
| Save | Write in place, then `ftruncate` | `.swp` file, then `rename` | Write with truncate |
| Text model | Bytes; optional 8-bit display | UTF-8 with `wcwidth` | UTF-8 and bidi |
| Processes | `:!cmd` via `system` (configurable off) | None | fork/exec/pipe for `!` filters and LSP; `AF_UNIX` socket |
| Completeness | Counts, operators with motions, registers, marks, undo queue, dot repeat, `:s`, `:set`, multiple files, `:r`, read-only view | Core editing; source TODOs note vertical movement losing the cursor column | Broad vi/ex plus windows, syntax colouring, keymaps |

The BusyBox Pyxis probe reached exactly the gaps the host import list predicted:

- termios state in `rawmode`/`cookmode`, including `VERASE`;
- `poll` in `mysleep`, which checks whether input is already waiting;
- `stat`, `fstat`, `S_ISREG`, `access`, `getuid` and the `S_IW*` bits, used by
  file loading, the read-only warning and `.exrc` ownership;
- `ftruncate` after a save;
- the string functions `memrchr`, `strchrnul` and `stpcpy`.

Nothing else in `vi.c` failed against the SDK headers.

**Toybox.** A port would replace its mmap storage with heap loading, which
changes the piece-table ownership the save path relies on. It would also need a
regex library before search works at all. It brings UTF-8 width handling that
the byte-oriented terminal cannot yet render truthfully. Toybox lists the
command as unfinished, and the visible gaps would land on Pyxis users.

**neatvi.** It is the most complete editor here and avoids system regex. Its
renderer, however, depends on terminal operations Pyxis ignores. Running it
correctly needs either renderer patches throughout or new
[terminal-session work](storage-and-terminal-agenda.md). Its process and LSP
features would need separate disabling, and its UTF-8/bidi model exceeds the
current terminal.

## Recommendation: BusyBox vi

BusyBox vi is the easiest to port without reshaping Pyxis around it:

- its screen model is full-line redraws within the existing VT subset;
- every missing facility sits in a few small, identifiable functions;
- its Kconfig switches can omit regex, signals and shell escapes without
  patching code.

GPL-2.0-only is acceptable under the existing
[ports licensing](../../ports/LICENSING.md) precedent set by Doom and Quake.
It must stage its license, and the recipe and patches give corresponding source.

### Proposed first-port scope

**Build.** A recipe compiles only `editors/vi.c` with a port-local GPL-2.0 libbb
adapter. That follows the precedent of Quake's `sys_pyxis.c`. The adapter
supplies the referenced helpers (allocation, `full_read`/`full_write`, option
parsing, list and string helpers) instead of building BusyBox's own Kbuild and
libbb. It stages `bin/vi.pxe` and the license, resolved by the shell as `vi`.

**Features on:** colon commands, yank/marks, search, dot repeat, read-only mode,
`:set` options, undo with its queue, verbose status, and screen-size queries.
Without signals, BusyBox re-queries the size at each redraw, which maps to
`term_size`. Without this feature it assumes 80×24.

**Features off:** regex search, signals, terminal size probing, `:!` execution
and 8-bit display. `.exrc` loading is disabled because its safety check is a
Unix owner/permission test. `EXINIT` can still provide startup commands.

**Terminal adaptation.**

- `rawmode`/`cookmode` become libterm session setup, holding passthrough for the
  whole session.
- `read_key` maps libterm keys to BusyBox key codes.
- `mysleep` becomes a timed key read that retains at most one pending key.
- Exit clears the screen and restores the cursor, since there is no alternate
  screen. Backspace accepts both BS and DEL instead of reading `VERASE`.

**File adaptation.** Load files by reading to EOF. A file that cannot be
reopened for writing reports the failure at save time, rather than predicting
it from Unix mode bits. Truthful metadata stays the separate
[metadata direction](neovim-libuv.md#proposed-bounded-native-milestones). Errors
report through vi's status line.

**Limits to state.** ASCII-only display, size changes noticed only at the next
redraw, no regex `:s` or `/`, no shell filters, and no crash-safe save beyond
what the selected save behaviour provides.

**Validation.** An ordinary build, then interactive QEMU editing on `home://`
and writable `host://`, plus a remote terminal session. Use the debugger only
if needed. Record the image size change.

## Agreed decisions

Agreed on 2026-10-04:

1. **Candidate.** BusyBox vi. Toybox vi and neatvi are not pursued.
2. **Save behaviour.** Add a libc `ftruncate` over native `FILE_RESIZE` and keep
   upstream's write-then-truncate order. Truncating before writing, as Kilo
   does, and a temporary file followed by atomic `rename` were not chosen.
3. **String functions.** Add `memrchr`, `strchrnul` and `stpcpy` to userland
   libc, per the [portability rule](../userland/libc-portability.md), rather
   than rewriting the call sites in the port patch. `stat`, `fstat` and
   `access` stay patched out until truthful metadata exists. No fake mode bits
   or user IDs.

The libc additions belong in userland and land before the ports recipe that
uses them.

[bb]: https://git.busybox.net/busybox/tree/editors/vi.c?id=f96d33d28a1f70fda5f27d221d5012b1ac0b7dad
[tb]: https://github.com/landley/toybox/blob/b7ec52ac35e075caffca5d330995d44e8dbfc8c3/toys/pending/vi.c
[nv]: https://github.com/aligrudi/neatvi/tree/26e9cadbf3828807a35bbe71f19e323e97b27439
