# Remote desktop

Status: **candidate direction, 2026-10-08.** Not scheduled and not assigned;
nothing here authorizes code. The owner asked how far Pyxis is from viewing and
controlling its screen from another machine, VNC style. A milestone starts with
a proposal that settles the [open questions](#open-questions).

## Goal

A VNC viewer on the development machine shows the Pyxis screen live and, in a
later stage, sends keyboard and pointer input to it. The server speaks RFB, the
[Remote Framebuffer protocol (RFC 6143)](https://www.rfc-editor.org/rfc/rfc6143),
so any existing viewer works and Pyxis needs no host-side client of its own.

## What already exists

- **[Screen capture](../interfaces/screen-capture.md).** One request returns the
  whole shown screen as an immutable FILE of native 32-bit pixels, with channel
  shifts and the geometry generation. In the screenshot qualification, a capture
  frame took a median 2.2 ms in the presenter at 1280x800 in nested QEMU; that
  is a debugger-profiled observation, not native cost.
- **Capture authority.** Spaces with `screenshot = true`, including Remote,
  already receive the `screen_capture` grant.
- **TCP** through lwIP, used by the [remote terminal](../userland/remote-terminal.md)
  and its file transfer.
- **zlib and mbedtls ports.** RFB's zlib-based encodings and a TLS-wrapped
  connection could reuse them.

## What is missing

1. **An RFB server, view-only first.** A native userland program: accept a
   connection, negotiate, then repeatedly capture, find changed tiles by comparing
   against the previous frame, and send them raw or zlib-compressed. No kernel
   change is needed for this stage. A native server is preferred over porting
   libvncserver, which expects threads; a Pyxis process has one thread today.
2. **Remote input.** RFB sends key events as X11 keysyms and pointer events as
   absolute positions with a button mask (the wheel arrives as buttons 4 and 5).
   Injecting them is new authority: events from the network would enter the same
   routing as the PS/2 keyboard and mouse. Absolute positions need the kernel-owned
   pointer position from task 1 of the [system pointer](pointer.md) milestone;
   today's pointer reports only relative counts.
3. **Cheaper frames.** Each capture copies the whole screen into a new FILE, and
   the presenter does not track which regions changed, so the server must diff
   whole frames itself. A capture stream or shared pixels would avoid the copy,
   but Pyxis has no memory shared between processes yet (see
   [desktop graphics](desktop-graphics.md)). Presenter damage tracking would remove
   the diff; the [capture debt](../technical-debt.md#screen-capture-memory-and-consistency-limits)
   notes that a pending capture must then still force a full composition.
4. **Authentication.** RFB's own VNC authentication is a DES challenge with an
   8-character password and is weak. A first slice would be LAN-only, like the
   remote terminal, whose unauthenticated access is accepted for bring-up. TLS
   through mbedtls (VeNCrypt) or an SSH tunnel would come later.

## Bandwidth

A raw 1280x800 frame is 4,096,000 bytes. The one native TCP measurement on record,
in the [RTL8111 qualification](../development/rtl8111-qualification.md), sent
29.2 MiB/s (about 245 Mbit/s) from the ThinkPad to the desktop: a single sample,
transmit only, without CPU count or repeats. At that rate, full raw frames would
arrive at roughly seven per second. Tile diffs and zlib reduce this a lot for
terminal-like screens, much less for games. The owner expects to improve
networking toward gigabit; that work is its own track, and this direction only
records the dependency.

## Possible stages

1. **View-only server.** Native RFB server in userland over the capture grant,
   with raw and zlib encodings and tile diffing. LAN-only, no input. Measure frame
   rate and CPU against a matched static and a moving scene.
2. **Remote input.** After pointer task 1: a remote input source for keys and
   absolute pointer positions, with its own authority decision.
3. **Efficiency and security.** Capture without whole-frame copies, damage
   tracking, TLS, and clipboard sync once the [clipboard](clipboard.md) exists.

## Open questions

- Which process holds the capture grant for the server, and whether a remote
  viewer may watch every space or only the one it was started from. Capture today
  observes whatever any space shows.
- What authority injects remote input: a separate grant, the remote daemon, or
  part of the Remote space's existing grants. How it interacts with the
  [pointer lock](pointer.md) and Super+Esc, and whether remote input counts as
  user activation.
- How X11 keysyms map onto Pyxis key events and the shared US
  [key layout](../development/sdk.md); non-US layouts and dead keys.
- Frame pacing: poll on a timer, capture on a geometry change, or wait for a
  presenter signal.
- Whether the remote terminal and the RFB server share one listener and one
  authentication story.
