# Clipboard

Status: **owner direction with accepted decisions, 2026-10-08.** Not scheduled
and not assigned; nothing here authorizes code. Key combinations, security and
capabilities are settled in a proper proposal before implementation.

## Owner decisions

Accepted 2026-10-08:

1. **Two layers:** a clipboard local to each space, and one shared across
   spaces. This matters most once the [multiplexer](terminal-applications.md)
   exists. In [Asterism](spaces.md#asterism), each space's Continuum supervisor
   is a natural owner of the local one.
2. **Typed objects, not a text clipboard.** Every object has a type, using MIME
   types such as `text/plain`, `image/png` and `image/svg+xml`. Ported programs
   and SDL2 already speak them.
3. **Every object must have a text representation.** A photo pasted into a
   terminal pastes its text form, such as its path; pasted into a photo editor,
   it pastes the photo itself.
4. **The clipboard holds the object.** Copying stores the object, so it survives
   the source program exiting, unlike X11 and Wayland, where the source keeps it
   until someone pastes. Large objects can be held as file capabilities rather
   than bytes.
5. **Conversion handlers in userspace.** When a program asks for a type the
   clipboard does not hold, a converter can supply it; for example, SVG to PNG.
   Haiku's Translation Kit is a working precedent for system-wide translators.

## Notes for the proposal

- **A path is not authority.** Roots differ between spaces, so a pasted path can
  name nothing, or something the receiving space cannot open. The text form is
  for display and insertion only. Access to the object itself goes through the
  typed object, which can carry a capability. The navigator's
  [operations between navigators](terminal-applications.md) face the same
  question, so both should share one answer.
- **Pasting into a terminal uses bracketed paste,** so pasted newlines do not
  run commands. [pyxis-remote](../userland/remote-terminal.md) already relies on
  it on the host side.
- **Converters run code on paste.** Which converters exist, what authority they
  receive and how a failed or slow conversion is reported belong to the
  proposal.
- **Selections** come from the [system pointer](pointer.md) in terminals.
