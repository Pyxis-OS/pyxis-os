# Screenshots

Status: **owner request, 2026-10-07; assigned to Codex 2.** The route is decided;
the task starts with a short proposal that settles the
[open questions](#open-questions-for-the-proposal). Nothing here authorizes code
before that proposal is accepted.

## Goal

Capture what the screen shows into an image file and get it onto the host. QEMU
checks use the monitor's `screendump` today; a native machine such as the
ThinkPad has no way to capture its screen, so graphical results there are
described from photos.

## Owner decisions

Accepted 2026-10-07:

- **Route.** A `screenshot` command in Pyxis writes an image file, and
  the host fetches it with the existing
  [pyxis-remote download](../userland/remote-terminal.md#explicit-file-transfer).
  There is no kernel network responder. Revisit one only if screenshots of a
  frozen system become necessary; the [UDP kernel log](../development/remote-debugging.md)
  already covers text from a stuck system.
- **Format: PNG.** This brings in the zlib and libpng
  [ports](application-ports.md#libraries-and-terminal-tools), which later
  graphical work can reuse. The screenshot command is their first consumer.

## Open questions for the proposal

- **Authority.** A new right on the display capability or a separate grant, and
  what a caller may capture: its own space's shown layer, or the whole screen
  including the space bar and other spaces.
- **zlib and libpng.** Pinned upstream revisions, the build options and
  compression level, and what libc they need.
- **Consistency.** Capture from the presenter's staged frame or from the space's
  surfaces, and whether a capture can tear against a writer.
- **Convenience.** Whether `pyxis-remote` gets a one-step command that runs the
  capture and downloads the file.

## Tasks

1. [ ] Proposal settling the open questions.
2. [ ] zlib and libpng ports.
3. [ ] The command and reference documentation. Check in QEMU against
   `screendump`, and ask the owner for a native ThinkPad capture.
