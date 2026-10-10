# Clipboard

Caelum retains one immutable UTF-8 `text/plain` item per space and one shared
item. An explicit Copy replaces the chosen layer atomically; failure leaves the
old item intact. Each item owns its bytes independently of its source selection
or program. Local and shared publications are independent. Stores are volatile,
with no history or persistence; reboot loses them.

Terminal delivery serves local terminals, Caelum log Copy and the
[multiplexer](../userland/multiplexer.md). Eligible graphics owners also use
bounded UTF-8 text through the SDL2 adapter below. FILE representations,
converters and host/remote bridging remain later
[milestone tasks](../wip/clipboard.md).

## Gestures and selection

Exact Ctrl+Shift+C/V selects local Copy/Paste; exact Super+Shift+C/V selects the
shared layer. Left/right modifiers work; lock modifiers are ignored and extra
Control/Alt/Super modifiers prevent recognition. The command press, repeats and
matching release are consumed before text translation, including when modifiers
are released first. Ctrl+C, Ctrl+V and mux's Ctrl+B retain their ordinary roles.
Super+arrows and Super+Esc retain kernel priority.

Copy needs a completed selection. A click clears selection; only entering a
different cell while the left button is held creates one. Pending/dragging,
cleared or absent selections refuse Copy. Mux uses the selected pane, while
Paste uses the focused live pane; keyboard focus changes do not retarget an
already admitted operation. Paste from a history view refuses without changing
view or focus. Caelum's log can be copied but has no application paste receiver.

Export encodes retained terminal scalars as UTF-8, including U+FFFD replacement
cells, under the existing 64 KiB byte limit. Over-limit Copy refuses the whole
operation. Unicode Copy does not broaden safe terminal Paste: it still refuses
non-ASCII items. Physical rows are joined with
LF and each selected row loses trailing spaces, including intentional ones.
There is no inferred soft wrap or reconstruction of original tabs. A real
selection that trims to nothing publishes an empty item; no selection refuses.
Styles, headings and dividers are excluded. Owned capture and validation happen
before publication, without allocation under the terminal output lock.

## Controller authority

`clipboard_local` and `clipboard_shared` are separate named grants, with
PUBLISH and PASTE rights. A local object names one space and checks the caller's
actual space. Shared authority is independently requested/delegated. It confers
no source roots, namespaces, FILE rights or future items. There is no general
unactivated store-read operation or discoverable clipboard namespace binding.

Trusted space startup requests separate `SPACE_CREATE_CLIPBOARD_LOCAL` and
`SPACE_CREATE_CLIPBOARD_SHARED` flags. Terminal CONTROL by itself grants neither.
Boot init explicitly requests both for stock local interactive spaces;
session/shell and mux pane shells forward each separately on local foreground
startup. [Configuration](../userland/init.md#boot-configuration) may withhold either.
Background/service/remote launches receive neither. Pane shells receive no
terminal CONTROL merely from these stores, and streams or receiver registration
grant no clipboard access.

A copied path such as `home://notes.txt` is only text. Paste does not open it or
transfer source roots or FILE rights; any later command resolves it using the
destination program's own delegated namespace and authority.

A shown terminal controller must also own the acquired terminal spatial queue.
A fresh gesture supplies one operation/layer/owner/view-bound action with a
five-second expiry. Polling, selection movement, repeats and synthetic events
create no action. Focus/layer/input reset, owner loss or view replacement
invalidates it. Every attempt consumes the matching action, including refusal;
controllers explicitly decline an action when their own preflight fails.

Native controller actions arrive in the existing waitable terminal spatial
queue with generation, mapping identity, action ID, operation and layer. The
clipboard grant selects the store; the supplied identity cannot select another
space or UI owner. PUBLISH receives owned text, PASTE names a matching-space
terminal attachment with injection authority, CLEAR removes the current item,
and REFUSE consumes an unused action without changing data. Focus-only
cancellation uses the acquired terminal controller without resetting
spatial identity/buttons; layout/history/resize boundaries still advance the view.
No first-delivery Clear key is assigned. The trusted kernel local-terminal handler performs the
user's gestures directly and lends no clipboard authority to its application.

## Graphics and SDL2

The same caller must own the shown, presented display and focused keyboard
session in its actual space; no pointer grant is needed. Fresh exact physical
Ctrl+C/V or Ctrl+Shift+C/V arms local Copy/Paste and retains the application's
key events. Exact Super+Shift+C/V is a shared system command. Repeats, Ctrl+X,
menu clicks, polling and synthetic SDL events create no activation. Both layers
require their own grant; there is no fallback between stores.

Each action binds its operation/layer/owner, keyboard acquisition, display mapping
and geometry to an opaque ID with a five-second expiry. Matched Get/Set/refusal
attempts consume it; Has does not. Overlapping unconsumed gestures cancel rather
than replacing permission. Focus/overlay/layer loss, reset, queue loss, release/
exit and resize/remap revoke it. Admission synchronizes physical input; an
incomplete scan boundary refuses busy before exposing text or availability.

[clipboard.h](../../include/abi/clipboard.h) adds graphics PUBLISH, READ, HAS and
REFUSE requests using the action ID from the 40-byte native keyboard event.
PUBLISH copies and validates complete scalar UTF-8, excluding NUL, before atomic
replacement; it preserves bytes and line endings. READ supplies a 64 KiB reply
buffer and receives raw bytes from one retained immutable snapshot, with length
in `reply_size`; authorized empty/missing data returns zero. HAS returns only
an eight-byte boolean under a live Paste action, without extending it or reading
contents. REFUSE spends a matching preflight failure. An acquired keyboard owner
may also use `KEYBOARD_CLIPBOARD_REFUSE` / `keyboard_clipboard_refuse()` to
decline its matching action when the store grant is absent; this grants no
read, publication or store metadata. Libpyxis supplies
`clipboard_graphics_publish/read/has/refuse()` helpers. The existing 64 KiB text
and 8 MiB aggregate payload bounds apply. Terminal Paste converts a separate
charged snapshot under its ASCII/control/CR-normalization rules; richer text
refuses before any insertion.

SDL installs all three clipboard hooks even without grants, avoiding upstream's
private-cache success fallback. Set returns 0 only for native publication;
refusal returns negative with an SDL error. Get returns an SDL-allocated,
NUL-terminated string: empty plus an error on refusal, empty without a new error
for authorized empty data; allocation failure may return NULL. Call `SDL_free()`.
Has returns false when unarmed/denied/empty; an armed non-consuming boolean is
advisory across replacement. No app/menu/background read or cached-self exception.

The adapter translates shared actions into Ctrl+C/V command-event modifier
snapshots without faking global Control state or inserting character text.
Private SDL queue-node identity arms only upon single-event delivery. Peek,
batch delivery, stale/synthetic events, callbacks, filtering/discard and queue
failure cannot lend or refresh permission. A later delivered event retires an
unused action; calls must handle the delivered command before polling onward.
Applications that inspect global modifiers need the event snapshot adaptation,
as applied to DevilutionX's text editor. See the
[SDL port](../../ports/sdl2/README.md) and its explicitly opt-in
[manual exercise](../../ports/sdl2/manual/README.md); the tool is absent from
ordinary images.

## Safe receiver

Only stock libterm line readers opt in. Registration uses terminal input READ
and matching output WRITE in the process's own space. It is exclusive and
process-owned, with a fresh epoch; copying grants or inheriting streams does
not transfer it. Raw programs such as vi, less and Links refuse Paste until they
implement a receiver. Registration failure leaves ordinary editing available.

Libterm releases on every editing return before input handoff. Release,
stop/exit and terminal hangup invalidate the epoch. Focus/layer/view changes
cancel a transaction without retargeting it; a still-editing receiver may accept
a later fresh gesture once eligibility returns.

Terminal END_INPUT is graceful EOF, not hangup. Registration and ordinary
receiver reads preserve queued input, then report zero-byte EOF; new paste
admission refuses closed input. An already admitted transaction retains its
framing/deadline until consumed or released, and cannot submit an unfinished
line at EOF.

Admission requires a live receiver at a complete decoding boundary and no older
input. It checks the native destination FIFO and competing reads; mux also
checks its outer staged bytes, partial key/prefix/confirmation state and the
focused pane's pending bytes. Earlier input is preserved and the attempt refuses
busy: nothing is flushed, deferred or saved for a later line/program. Native
receiver reads atomically declare the parser boundary and close it whenever an
ordinary byte is handed to userspace.

One admitted transaction pins the item, receiver epoch, input session and live
pane/view. Its records are BEGIN, short DATA, then END or CANCEL, independent of
the ordinary FIFO. Only the registered process can consume them. Existing
process-aware `wait_many` READABLE readiness drives progress. Other readers
retain ordinary input semantics outside the admitted interval.

The complete text is validated before admission: no NUL, printable ASCII/LF/Tab
only after CRLF/lone CR normalization. Other controls, DEL and non-ASCII refuse.
Libterm inserts ASCII at the editing cursor and maps LF/Tab to spaces. Capacity
exhaustion is reported while remaining records are consumed; framing never
submits a line, enters history, interrupts a process or routes through mux's
prefix parser. Subsequent store replacement does not change an active snapshot.

Completion requires receiver-consumed END/CANCEL and acknowledgement after
leaving paste mode. Ordinary typing is suppressed until that boundary, not
buffered for later. Kernel keyboard routing retains physical Enter freshness,
including controller-visible input queued before acknowledgement: an Enter
pressed/held before or during Paste needs release and a new press after
completion. Physical Ctrl+C
cancels before its normal interrupt/line-cancel effect; unmodified Escape
cancels, while Super+Esc retains its kernel priority. Cancellation may leave an
editable inserted prefix but never submits it. Libterm reports inserted bytes,
cancellation and native status; nonquiet editors show a cancellation notice.

## Bounds and qualification

Text is at most 64 KiB. Store-owned current, staging and active-snapshot payload
allocations share an 8 MiB global admission budget. Replacement may temporarily
retain both old and new items; failure preserves the old one. One active Paste
per space is permitted. Its separate five-second total deadline runs from
admission through completion and never restarts after progress. The 4 KiB native
input queues and 256-byte mux staging queues are unchanged.

Admission and resume also fence the PS/2 controller and incomplete scan decoder
at a bounded BSP input boundary. An incomplete admission refuses busy; incomplete
resume keeps typing suppressed through bounded retry. Local kernel gestures are
one-shot native intents pinned to the gesture's receiver epoch, not requests
saved until a later reader becomes eligible.

Cancellation discards unsent payload and terminates native framing independently
of FIFO capacity. It occurs on deadline, explicit release/cancellation, receiver
loss, focus/layer loss or mux view invalidation, including history movement,
layout, pane closure and resize. No stale frame/data transfers to a successor.
Blocked unrelated output still has no newly guaranteed cleanup time.

See the [matched qualification record](../development/clipboard-first-delivery-qualification.md)
for measured and manually checked revisions/configuration, and
[initial delivery limits](../technical-debt.md#initial-clipboard-delivery-limits)
for deferred consumers, fidelity and persistence work.
