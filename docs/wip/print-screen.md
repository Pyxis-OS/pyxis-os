# Print Screen key

Status: **implemented for review, 2026-10-18.** The contracts below are accepted.
See [the interface](../interfaces/screen-capture.md#print-screen-delivery) and
[usage](../userland/screenshot.md#print-screen).

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
Availability begins at NEXT/readiness participation, not at initial grant delivery.
One keyboard save runs globally at a time: another press reports busy without
dropping the original completion. Missing permission or saver reports unavailable,
without using another space's grants. Print Screen under the power overlay is
consumed without saving. A space change before actual frame selection cancels
the request; an already selected image finishes in its initiating
space's home.

The saver returns a request-matched basename or failure status through the same
private capability. The kernel formats navigation feedback and expires it after
two seconds. A 30-second policy budget bounds queued/delivered requests; timeout
or final SERVE close reports failure, disarms the saver and releases busy
admission. NEXT/readiness re-arms it. Late completions cannot replace a newer
admitted request. An already-admitted storage operation can still complete after
timeout or saver death; the failure message says to check screenshots. This
grants no arbitrary bar drawing.

Names use UTC wall time and the immutable space name. Missing wall time fails
visibly with a distinct no-clock reason. The saver creates `home://screenshots` on first use, encodes into a
reserved temporary file, then publishes with no-replace rename. Only confirmed
collisions advance the suffix, up to 1,000 destination candidates. Definite
failures remove the confirmed temporary; uncertain publication preserves it.
It reports denied access, full storage and
uncertain publication honestly, without overwriting or publishing a successful
prefix. RAM-backed home works with its ordinary reboot-loss semantics; saving
adds no crash-durability guarantee.

## Delivery

- [x] Implement private delivery, saver, naming and feedback; qualify the physical
  binding in QEMU and decode the saved PNG on the host.

QEMU 10.2.2, nested KVM/Q35, one/four CPUs and 2 GiB: Print Screen published a
1280×800 RGB PNG, transferred with `xfer` and decoded on the host. Same-second
saves selected suffixes; Read-only reported unavailable. Mandelbrot keyboard
ownership and Mousetest relative lock survived capture. An existing file at
`home://screenshots` was preserved on WRONG_TYPE refusal. A switch after image
selection completed in the initiating space's home.

The review rerun observed actual frame selection with IF=0, matching space and
generation; a busy rejection followed by successful completion with the same
feedback token; and no new request while the power overlay was shown. Manual
capture still succeeded. GDB observed request id/deadline zero, one SERVE grant,
and null saver/capture/backing slots after completion on one and four CPUs.

Pre-selection switch cancellation, nonparticipating/stalled saver handling,
saver death, failed NEXT copyout, absent clock, storage failures and collision
exhaustion remain source-reviewed without injected failures. No native hardware
or durability claim; the [native check](../technical-debt.md#print-screen-native-qualification)
is pending. Raw evidence stays at `/shared/present/print-screen/`.

## Related

- [Screen capture](../interfaces/screen-capture.md) and its LAN visibility debt.
- The Copilot key row in [everyday gaps](everyday-gaps.md) (space switcher).
