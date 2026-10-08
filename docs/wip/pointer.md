# A system pointer

Status: **owner direction with accepted decisions, 2026-10-08.** Not scheduled
and not assigned; nothing here authorizes code. A milestone starts with a
proposal that settles the [open questions](#open-questions).

## Today

The [mouse driver](../devices/mouse.md) delivers relative PS/2 motion, buttons
and wheel counts to the active space's
[pointer session](../devices/mouse.md#userspace-pointer-sessions). A session is
exclusive: Quake and `mousetest` take the whole pointer, nothing draws a cursor,
and no other program uses the mouse.

## Direction

Mouse support across all of Pyxis (owner, 2026-10-08):

- **One visible cursor** that the kernel moves and routes. Clicks, motion and
  the wheel go to the surface under the cursor.
- **Clicking a tab** in the space bar switches to that space, alongside
  Super+Left/Right.
- **Selecting text and copying it** in terminals, through the
  [clipboard](clipboard.md).
- **Scrolling** with the wheel once scrollback exists, which arrives with the
  [multiplexer](terminal-applications.md).
- **Pointer lock for games.** A program such as Quake may ask for the pointer to
  be locked: hidden cursor and relative motion only. Every other program gets an
  ordinary cursor with positions.

## Owner decisions

Accepted 2026-10-08:

1. **Where the work lives.** The kernel draws the cursor, routes pointer input
   to the surface under it and handles tab-bar clicks. Selection and copy work
   in the kernel's local terminals as well as in the multiplexer. Scrollback and
   wheel scrolling live in the multiplexer.
2. **Programs supply their own cursors from the start.** No single cursor
   graphic is built in as the only option: a program, a game for example, can
   set the cursor image for its surface. VirtIO GPU's hardware cursor layer can
   show it; other displays need the presenter to draw it.
3. **Super+Esc always unlocks a locked pointer,** whatever the program asked
   for. After that, the program cannot lock the pointer again on its own; only
   the user's click on its surface allows a new lock, as browsers do.

## Devices

- **PS/2,** implemented. The ThinkPad's touchpad and TrackPoint arrive as a
  relative PS/2 mouse without a wheel; touchpad scrolling would need the
  Synaptics absolute mode, which is not supported.
- **USB HID mice,** planned (owner, 2026-10-08). USB already has xHCI,
  enumeration and USB 2 hubs. A mouse adds the HID boot protocol and interrupt
  transfers, which the bulk-only storage work did not need. A USB keyboard would
  follow almost for free.
- **Bluetooth** is on the [investigation list](later-os-directions.md#bluetooth),
  for the owner's Logitech MX Master 3S.

## Open questions

- Cursor images: format, size limits, hotspot, and who may set the cursor for
  which surface.
- The coordinates programs receive: surface pixels, and what happens on resize.
- How the pointer interacts with [space layers](space-layers.md): which layer
  receives it, and selection on the terminal layer while graphics is hidden.
- Software cursor cost in the presenter on the boot framebuffer and Bochs.
- How Quake's existing pointer session becomes a lock request.
