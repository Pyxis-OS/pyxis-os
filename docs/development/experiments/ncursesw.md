# Native ncursesw qualification

Gate 3 of [terminal UI](../../wip/terminal-ui.md), 2026-10-23. QEMU evidence;
no native run or compiler change. Raw inputs, build logs, ANSI captures,
screenshots and debugger transcripts remain in
`/shared/present/terminal-ui/ncursesw-probe/` and `ncursesw-qemu/` beside it.

## Inputs and baseline

- Initial Pyxis `d4b38c6f`, userland `c0a2a622`, ports `f4099557`.
  Integration rebased onto Pyxis `5e389f24`, with userland `6e0c684` and ports
  `921eb28` (the corresponding PRs retain exact revisions).
- Builder `git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-3d69e83`;
  SDK-only headers/runtime, `make -j16 sdk` and `make -j16 image DISTRIBUTION=full`.
  Recipe: `lua ports/build.lua ncursesw --sdk build/sdk --work build/ports/ncursesw`.
- [ncurses 6.6 archive](https://ftp.gnu.org/gnu/ncurses/ncurses-6.6.tar.gz),
  [signature](https://ftp.gnu.org/gnu/ncurses/ncurses-6.6.tar.gz.sig),
  [GNU keyring](https://ftp.gnu.org/gnu/gnu-keyring.gpg), fetched only through
  the existing `https://repo.internal/repository/raw-gnu/gnu/` cache.
  SHA-256 `355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11`.
  `gpgv --keyring gnu-keyring.gpg ncurses-6.6.tar.gz.sig ncurses-6.6.tar.gz`
  verifies Thomas E. Dickey's signature, fingerprint
  `19882D92DDA4C400C22C0D56CC2AF4472167BE03`. Retain upstream COPYING.

The unmodified cross-configure, apart from accepting the Pyxis target name,
fails with `No termio/termios/sgtty found`. Thus there is no runnable previous
ncurses baseline. Adapt the native driver boundary; no Unix headers, host
libraries or successful terminal stubs are substituted. Existing raw/lifecycle
cost measurements are context, not an ncurses performance comparison. The new
library and validation helper add no always-on kernel path.

## Builds and QEMU

The static library, whole-archive target closure and ordinary full image build.
The development archive is about 3.4 MiB including debug data; the demo P1F is
about 355 KiB. Both `ncursesw-dev` and `ncurses-demo` have ordinary package
recipes. The embedded entry needs no filesystem grant; the demo declares
MEMORY MANAGE and precise read/sleep clock authority, with no launcher.

Q35/KVM, invariant host CPU/KVM clock, 4 GiB, one and four CPUs, modern VirtIO
GPU/RNG/network. These are nested-VM correctness/deadline observations, not
native latency or CPU-cost measurements. Interactive `ncurses-demo --cooperate`
and ordinary private SDK callers exercise actual CONSOLE/terminal-session
objects; no kernel probes or injected debugger calls.

| Check | Observed result |
| --- | --- |
| Raw/cbreak, one/four CPUs | Raw Ctrl+C reads 3. Cbreak's opted-in first press shows -3 and continues; second stops the command and returns a usable shell. |
| Local/Remote presentation | Framebuffer, local mux and plain/remote-mux sessions render ASCII borders and green/default colours. Split/resize redraws; tiny panes show `small`; endwin/re-entry and normal exit work. |
| Incremental input, one/four CPUs | Nonblocking Escape-prefix returns ERR in under 1 ms; resize returns KEY_RESIZE (410); completion decodes KEY_UP (259). Standalone Escape expires as 27; both Backspace encodings decode KEY_BACKSPACE. An 80 ms input timeout takes 80 ms, without a second wait. |
| EOF/hangup, one/four CPUs | Waiting input returns ERR/EIO at the frontend's 300 ms closure. Hangup makes refresh/endwin report ERR; clean EOF still permits output restoration. |
| Backpressure, one/four CPUs | A reader delayed 300 ms drains 173,087 bytes; writes resume and refresh/endwin succeed (303–307 ms overall). No successful redraw after output failure. |
| Descriptor replacement | Raw and flushinp refuse with EBADF; the replacement file's first byte remains unread. Non-console input refuses with ENOTTY; an unembedded terminal name returns NULL. |

GDB observes the owned raw handle becoming nonzero in raw and zero in cbreak.
On SCREEN release, each input/output snapshot drops its storage count from two
to one (the remaining descriptor), clears the backend pointer and leaves no raw
grant. This checks mode/snapshot cleanup without calling functions in the guest.

Libc conversion checks: `A` plus U+00E9 round-trips as two scalars/three bytes;
NULL destinations count the full string; a two-byte output limit encodes only
`A` and leaves the next byte untouched. Invalid overlong UTF-8 returns SIZE_MAX
with EILSEQ. C-locale wcwidth returns 0/1/-1/-1 for NUL/ASCII/newline/U+00E9;
zero sleep succeeds without waiting.

## Limits

Only the embedded `pyxis` entry and the documented 16/default-colour,
single-cell repertoire are supported. Wide APIs do not extend libc's ASCII
C-locale width/classification or the renderer. External/HOME terminfo lookup,
mouse protocols, Unix signals/tty controls, menu/form/panel and C++/Ada bindings
are excluded. The caller excludes concurrent SCREEN/descriptor destruction;
saved-object validation refuses sequential fd reuse, but is not an I/O lease.
Native demonstration is gate 5; hexcurse and its file/save checks remain gate 4.
