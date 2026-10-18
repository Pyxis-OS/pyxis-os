# Print Screen key

Status: **assigned and implementation authorized by the owner, 2026-10-18.**
The contracts below are accepted; qualification is pending.

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

## Saver contract (accepted owner defaults, 2026-10-18)

The session starts one saver in each opted-in space with that space's CAPTURE
and home grants. A private, space-bound notification capability stays out of
ordinary children. The system consumes the key's press, repeats and release.
One save runs at a time: another press reports busy. Missing permission or saver
reports unavailable, without using another space's grants. A space change before
capture cancels the request; an already captured image finishes in its initiating
space's home.

The saver returns a request-matched basename or failure status through the same
private capability. The kernel formats navigation feedback and expires it after
two seconds. Late completions cannot replace newer feedback, and closing the last
saver grant removes its registration. This grants no arbitrary bar drawing.

Names use UTC wall time and the immutable space name. Missing wall time fails
visibly. The saver creates `home://screenshots` on first use, encodes into a
reserved temporary file, then publishes with no-replace rename. Only confirmed
collisions advance the suffix. It reports denied access, full storage and
uncertain publication honestly, without overwriting or publishing a successful
prefix. RAM-backed home works with its ordinary reboot-loss semantics; saving
adds no crash-durability guarantee.

## Delivery

- [ ] Implement private delivery, saver, naming and feedback; qualify the physical
  binding in QEMU and decode the saved PNG on the host.

## Related

- [Screen capture](../interfaces/screen-capture.md) and its LAN visibility debt.
- The Copilot key row in [everyday gaps](everyday-gaps.md) (space switcher).
