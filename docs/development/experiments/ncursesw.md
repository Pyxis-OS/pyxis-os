# ncursesw source and build probe

Gate 3 of [terminal UI](../../wip/terminal-ui.md), 2026-10-23. No target library
or runtime success is claimed yet. No native/PXE action.

## Inputs and source verification

- Pyxis `d4b38c6f651a83361321b4634c981239fe914b0c`, userland
  `c0a2a6223233d9403c1c24c034e062cd5c9da1b4`, ports
  `f40995577fc56f86e4309d0b83d997d47f649305`.
- Builder `git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-3d69e83`;
  current SDK/runtime built with `make -j16 sdk`. No compiler changes.
- [ncurses 6.6 release](https://ftp.gnu.org/gnu/ncurses/ncurses-6.6.tar.gz),
  [detached signature](https://ftp.gnu.org/gnu/ncurses/ncurses-6.6.tar.gz.sig),
  and [GNU keyring](https://ftp.gnu.org/gnu/gnu-keyring.gpg), fetched only through
  `https://repo.internal/repository/raw-gnu/gnu/` (existing cache).
- Archive SHA-256:
  `355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11`.
  `gpgv --keyring gnu-keyring.gpg ncurses-6.6.tar.gz.sig ncurses-6.6.tar.gz`
  reports a good Thomas E. Dickey signature, signing fingerprint
  `19882D92DDA4C400C22C0D56CC2AF4472167BE03`.
- Preserve the release's permissive notice (COPYING). No source fallback to
  upstream was used.

## Probe result and implementation boundary

After teaching config.sub the target name, run the cross-configure with the
SDK-only include paths and target compiler, `--enable-term-driver`, static
wide-character libraries, no C++/Ada bindings, programs, tests, manuals,
home-terminfo lookup or SIGWINCH. Configuration reaches:

```text
configure: error: No termio/termios/sgtty found
```

This is a genuine missing Unix interface, not a reason to claim termios exists.
Source inspection identifies the native adaptation points: internal TTY/mode
storage, tinfo driver mode ownership and geometry, input/readiness and Escape
deadlines, native resize delivery into KEY_RESIZE, flush/loss handling and output
failure propagation. There is no Pyxis backend upstream. Signal handlers and
Unix ioctls must not become target dependencies. Disable the optional extended
colour-pair API (which unconditionally includes search.h); the accepted profile
is 16 colours. Inventory further portable libc needs after the native boundary
compiles, without importing host headers or asserting missing features exist.

An exploratory compiler inventory bypassed the Unix configure guard and used
an internal mode declaration; it still fails in the Unix driver assumptions.
It is not a runnable shim or a proposed implementation. Only the first configure
failure and the source-inspected boundary above are conclusions of this probe.
Raw inputs, command script and logs are under
`/shared/present/terminal-ui/ncursesw-probe/`.

The WIP records one unresolved policy: native cbreak/raw mode semantics. Stop
for that decision before implementation. Library size, runtime performance,
QEMU/debugger qualification and native evidence remain unmeasured. Existing
raw/lifecycle measurements are context, not an ncurses baseline.
