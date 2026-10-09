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
- **Selections** come from the [system pointer](../interfaces/pointer.md) in
  local terminals and mux. Highlighting a selection does not publish clipboard
  data or supply a Copy/Paste operation.

## Proposed terminal selection handoff

On an eventual explicit Copy action, the local terminal or mux selection owner
would freeze selected text into an owned snapshot that survives source exit,
then publish it to the chosen per-space or shared store. Both sources should
converge on the accepted typed-object/text-form/converter direction above.
The export interface, Copy/Paste gestures, store choice and cross-space authority
remain proposal decisions; the pointer milestone supplies none of those operations.

Proposed text extraction joins selected physical rows with line feeds, omits
unselected cells and styling, and trims terminal padding at row ends. It does
not infer soft wraps or expand the terminal to Unicode widths. Retained cells
currently hold 8-bit glyph indices, not decoded UTF-8. The proposal must settle
the glyph encoding and its text representation before labeling a snapshot
`text/plain`; arbitrary terminal bytes must not be advertised as UTF-8.
