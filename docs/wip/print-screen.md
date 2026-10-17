# Print Screen key

Status: **planned by the owner, 2026-10-17; not assigned.** The decisions below
are accepted. The task still starts with a short proposal of the open details;
nothing here authorizes code.

## Goal

Pressing Print Screen saves a screenshot without typing a command, using the
existing [screen capture](../interfaces/screen-capture.md) and the
[`screenshot`](../userland/screenshot.md) PNG encoding. No editing, selection or
other interface frills (owner, 2026-10-16).

## Accepted decisions (owner, 2026-10-16 and 2026-10-17)

1. **A system binding.** Print Screen is taken by the system like
   [Ctrl+Alt+Delete](../kernel/power-overlay.md#the-chord): it works while a
   program holds keyboard capture or relative pointer lock, and programs never
   receive the key.
2. **The whole shown screen.** The shot is exactly what the screen-capture object
   observes today: navigation, the selected space's shown layer, margins and the
   visible system pointer. No space, pane or region selection.
3. **Location and name.** `home://screenshots/SPACE-YYYY-MM-DDTHH-MM-SS.png`, the
   space's name followed by an ISO 8601 date and time with dashes instead of
   colons, for example `home://screenshots/development-2026-10-17T21-42-05.png`.
4. **Collisions.** A second shot in the same second gets `-2`, then `-3` and so
   on, before `.png`. An existing file is never overwritten.
5. **One switch.** The existing per-space
   [`screenshot = true`](../userland/init.md#boot-configuration) boot setting
   enables both the command and the key. In a space without it the key saves
   nothing.
6. **Feedback.** A brief message in the navigation bar naming the saved file (or
   the failure), cleared after a second or two.

## Open details for the proposal

- How the key reaches the saver. The kernel recognizes the key; writing
  `home://` and holding CAPTURE are userspace authorities of the shown space
  (its session holds both today), so the kernel needs a small notification to
  that space. Who saves when the shown space lacks the grant, and what the bar
  says then.
- The bar message channel from the saver back to the kernel-drawn navigation.
- The time source and zone for the name, creating `home://screenshots` on first
  use, and what a live system (RAM-backed `home://`) or a full disk reports.

## Related

- [Screen capture](../interfaces/screen-capture.md) and its LAN visibility debt.
- The Copilot key row in [everyday gaps](everyday-gaps.md) (space switcher).
