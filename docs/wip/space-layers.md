# Graphics and terminal layers in a space

Status: **owner idea with accepted decisions, 2026-10-07; assigned to Codex 1.**
The task starts with a short proposal that settles the
[open questions](#open-questions-for-the-proposal). Nothing here authorizes code
before that proposal is accepted.

## Today

Each space owns a local TTY and at most one
[graphics session](../interfaces/graphics.md). The program decides which one is
on screen: PRESENT selects its buffer, and RELEASE or exit selects the TTY again.
The TTY keeps receiving and rasterizing output while the graphics buffer is shown,
so both surfaces already exist side by side.

## Idea

The user chooses the visible layer instead. In a space with a presented graphics
session, Super+Up shows the graphical program and Super+Down shows its terminal.
The space's tab ends with `+` while the graphical program is shown and `−` while
the terminal is shown; a space without a graphics session has no marker.

This is not a nested space. Both layers belong to the same space, processes and
grants. It lets Quake run while its terminal output is visible a keypress away,
and it is how graphical programs coexist with the planned
[terminal multiplexer](terminal-applications.md): they sit on the layer above
it instead of in a pane.

## Owner decisions

Accepted 2026-10-07:

1. **A hidden program keeps running.** Switching to the terminal only takes
   keyboard and pointer input away from it, releasing held keys and buttons as a
   focus loss does. Its output stays live. Input goes to the terminal layer.
2. **The marker** is `+` for the graphical layer and `−` for the terminal.
3. **The user's choice wins.** After Super+Down, the program's PRESENT does not
   bring it back; only Super+Up, or the session ending, changes the layer. A
   graphics session newly started from the terminal is shown, because the user
   just launched it.

## Open questions for the proposal

- **Focus events.** Today a space switch is a focus loss, and Quake then blocks
  and excludes the time from game time. Decision 1 needs a hidden program to keep
  running. Settle how a hidden layer differs from an unselected space, and what
  Quake, Doom and Mandelbrot do in each.
- **Terminal input while hidden.** When the shell is waiting for the graphical
  program, where typed input goes, and whether it is queued for later.
- **Edge states.** An acquired but never-presented session, RELEASE or exit while
  hidden, a space switch while hidden, and display resizing while hidden.
- **The bar.** Where the marker fits within the existing tab width.
- **Presentation cost.** The presenter copies only the shown surface; confirm
  that a hidden graphics buffer adds no copying.

## Tasks

1. [ ] Proposal settling the open questions.
2. [ ] Implementation, with the [graphics](../interfaces/graphics.md),
   [keyboard](../devices/keyboard.md), [mouse](../devices/mouse.md) and
   [space bar](../userland/init.md#space-bar) references updated, and any
   Quake/Doom/Mandelbrot changes the focus rule needs. Check in QEMU and ask the
   owner for a native ThinkPad check with Quake.
