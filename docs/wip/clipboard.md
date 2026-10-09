# Clipboard

Status: **first terminal delivery authorized, 2026-10-09**. Rounds one and two
are accepted, not yet implemented. The owner authorized Copy + Paste for local
terminals and mux into opted-in stock libterm line readers. This proposal update
records the contract and limits before code; implementation branches from fresh
main after this PR merges. Graphics, FILE and converter contracts remain proposed.

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

## Accepted round one

Accepted 2026-10-09:

1. **Ownership and authority:** kernel-owned volatile per-space and shared
   stores, separate explicitly delegated local/shared grants and one-attempt
   activation.
2. **Terminal gestures and text:** Ctrl+Shift+C/V local, Super+Shift+C/V shared;
   Copy from the completed selection's pane and Paste into the focused live
   pane. ASCII-only export, physical rows joined with LF, trailing spaces trimmed.
   Ctrl+C/V and mux's Ctrl+B retain their ordinary meanings.
3. **First delivery, changed:** Copy **and Paste** for the local terminal and
   mux into stock libterm line readers. This may be two PRs back to back, but
   Copy alone does not complete the delivery. SDL2, FILE and converters stay
   later tasks. Round two settles the receiver contract below.

## Existing boundary

[Pointer selection](../interfaces/pointer.md#terminal-control-and-selection)
provides highlights, not publication or text export. The local TTY retains
visible byte-indexed glyph cells under its output lock; mux owns selected
live/history cells and their row identities. Neither retains decoded Unicode,
original tabs or soft-wrap provenance. The handoff direction remains: an
explicit Copy freezes owned text, then publishes it to one selected store.

Native [libterm](../userland/terminal.md) currently does not parse bracketed
paste. Its unknown CSI markers would not stop LF from submitting a shell line.
Mux also interprets Ctrl+B in its ordinary input stream. The
[remote client](../userland/remote-terminal.md#dropping-a-host-file) enables
host bracketed paste to recognize upload candidates, then forwards ordinary
bytes; this is not guest-side newline safety. Clipboard paste needs explicit
receiver and framing work, not just two escape sequences around existing input.

## Accepted ownership before Continuum

Caelum owns one volatile store per space and one shared
store. Each holds one current immutable typed item and its mandatory text form.
The stores belong to the space registry and boot session, respectively, not to
shell, session launcher, mux, graphics owner or last publisher. Shell/launcher
exit leaves them intact. Empty and explicitly cleared are valid states.

The kernel handles storage, bounded publication, references and native
input/focus validation. It does not parse image formats, open paths or run
converters. Trusted startup delegates clipboard grants explicitly; userspace
controllers own UI and conversion orchestration. Continuum can later supervise
those controllers without changing the stored-item or grant contract. Replacing
the kernel stores with supervised services would require a separate migration
proposal, not an implicit ownership change.

## Authority and activation

Clipboard authority is separate from console READ/WRITE, pointer INPUT,
terminal CONTROL, FILE READ and namespace LOOKUP. Input ownership supplies
eligibility, not ambient clipboard access. No generally discoverable namespace
binding gives every program shared read/publish access; existing
[namespace lookup](../interfaces/namespaces.md#binding-and-lookup) returns a
binding's fixed grants, not per-application approval.

| Actor | Space-local layer | Shared layer |
| --- | --- | --- |
| Kernel local terminal handler | Copy its completed selection; paste to the shown eligible receiver after a gesture | Same operations only for the explicit shared gesture |
| Trusted acquired mux controller | Copy its completed pane selection; paste to its focused pane after a gesture | Same, with separately delegated shared authority and the shared gesture |
| Eligible graphics owner | Explicitly delegated local copy/paste grant plus fresh operation-specific activation | Separate shared copy/paste grant plus the explicit shared gesture |
| Background app, ordinary pane child, remote client | No UI/store access merely from its streams, selection or paths | No implicit grant or cross-space access |
| Converter | Only the selected input snapshot and its bounded result channel | No store grant |

Trusted boot/init/session/shell startup forwards grants deliberately where it
forwards the corresponding UI ownership. Pane children get no mux-controller
clipboard authority; a graphics child may receive its own attenuated graphics
clipboard grant. Session handoff currently drops arbitrary resources, so this
must be implemented explicitly. The kernel-local handler needs no userspace
controller grant, including Copy from Caelum's visible log selection. Caelum has
no application paste destination.

A local grant names one space; use checks the caller's actual space. Shared
program/controller authority is independent and can be withheld by startup.
The trusted kernel terminal handler remains the user's explicit shared-transfer
path; it does not lend that authority to the terminal's application. Graphics
eligibility requires the same caller to own both the shown display and focused
keyboard session; mismatched owners refuse. The current
space/layer and acquired UI owner are checked from kernel state, never from an
untrusted space ID in an IPC payload. Copies of grants retain their ordinary
[capability transfer](../interfaces/processes.md#objects-capabilities-and-handles)
semantics; copying one does not transfer UI ownership or bypass activation.

The accepted one-attempt activation is operation-specific and follows
[pointer lock](../interfaces/pointer.md#relative-lock-and-user-escape): one
Copy or Paste attempt for one layer and UI owner, consumed even on refusal.
Its five-second expiry was accepted in round-two decision 3. Repeats,
held keys, selection dragging, pointer warp, polling and synthetic application events
create none. Focus/layer/input reset, owner exit or session replacement cancels
unused activation. Already captured data is never silently retargeted.

Cross-space Paste is an explicit shared-layer action at the destination. It
captures the shared item's current generation once and grants only that item's
requested representation, not access to the source space's roots, namespace,
terminal or future clipboard contents. It works after the source space becomes
inactive. A source disappearing does not revoke a completed independent item.
Local fallback is never substituted for an empty/denied shared paste or vice
versa. Refusal inserts nothing and leaves the old store intact.

## Accepted terminal gestures

For shown local terminals and mux: exact Ctrl+Shift+C/V for
local Copy/Paste; exact Super+Shift+C/V for shared Copy/Paste. Either left/right
modifier works, lock modifiers are ignored, and extra Control/Alt/Super modifiers
outside the selected chord prevent recognition. Consume the command key press,
repeats and matching release, even if modifiers are released first; one fresh
press performs one action. Intercept before terminal text translation, since
Ctrl+Shift+C currently becomes the same byte 3 as Ctrl+C. Deliver a native action
to the acquired terminal controller, rather than inventing a control byte in
mux's byte stream. Plain Ctrl+C, Ctrl+V and mux's Ctrl+B prefix keep their
application meanings. Existing Super+arrows and Super+Esc keep their priority.

Copy in mux uses the completed selection's pane, even if a later heading click
changed keyboard focus. Paste uses the currently keyboard-focused pane. Both
capture pane/session/view identity at the action; neither follows subsequent
focus. Copy while dragging, stale/cleared selection, or Copy with no selection
refuses without overwriting the previous item. Explicit history movement clears
selection as today. Paste to a non-live history view refuses without changing
history or keyboard focus.

## Proposed graphics activation

This routing remains proposed for the later SDL2 task, outside round one. Graphical Ctrl+C/V (and Ctrl+Shift+C/V, without Alt/Super) remain
application key events. While an
eligible graphics owner is shown, a fresh physical chord may establish the
matching **local** clipboard intent without consuming the application's key.
It does not force the application to copy/paste or authorize shared access.
Pointer activation alone is not a clipboard operation: menu-button clipboard
requests will need an explicit native approval interaction; general clicks do
not authorize reading another program's saved text.

The shared Super+Shift chords remain system actions in graphics too. A native
client receives a layer-specific action; if it cannot handle it, nothing is
copied or inserted. For SDL2, the adapter would translate that action into the
application's conventional Ctrl+C/V request while retaining its one-attempt
shared-layer context privately. Such translation does not manufacture fresh
native activation. No global remapping of ordinary application keys is proposed.

## Owned text handoff

Freeze the completed selection and revalidate its source/view identity before
publication. Local TTY staging follows BSP allocation and output-lock ownership;
no allocation under the output lock or borrowed cell pointer survives Copy.
Mux constructs its own owned snapshot from its selected pane rows. Mutation
between sizing and capture either retries a still-valid selection within bounds
or refuses; it never publishes mixed cells. Publication replaces the current
item atomically only after complete validation/admission. Failure preserves it.

The accepted ASCII-only export maps printable glyph indices 0x20..0x7e
exactly to UTF-8 bytes; reject the entire Copy if a selected cell is outside that
range. Bizcat has no declared in-tree glyph-to-Unicode table, so high bytes must
not be labeled UTF-8 or silently changed. A verified complete font mapping is a
later extension, not Unicode terminal decoding. Each item identifies this as a
UTF-8 `text/plain` representation, also its mandatory text form, without NUL.

Join selected physical rows with LF, exclude styling, headings, dividers and
unselected cells, and remove trailing space glyphs from each selected row.
There is no distinction between padding and intentional trailing spaces, so
this choice loses the latter too. Do not infer soft wrapping or reconstruct
tabs. An existing selection that becomes empty after trimming publishes an
empty text item; absence of a selection instead refuses.

## Accepted safe terminal paste

This receiver contract was accepted 2026-10-09 in round two. Native console
READ currently has no process-owned foreground receiver; the clipboard must not
infer one from a writer, a mode escape or a process that happens to be reading.
The first delivery adds a scoped paste receiver, not general foreground-input
arbitration. Older/raw readers receive no unframed fallback.

### Receiver opt-in and lifetime

A stock libterm line reader explicitly opts in while editing, using
READ on its terminal input and WRITE on matching terminal output in its own
space. Registration is exclusive, process-owned and names one input session
with a fresh receiver epoch. Competing registration is refused busy; grant
copies, launches and stream inheritance do not transfer it. The registration
allows safe delivery to the reader; it grants no clipboard access to that
program. The terminal handler or acquired mux controller performs the user's
paste action with its separate clipboard authority.

Libterm releases registration on every line-reader return, including submission,
EOF, cancellation and error, before the shell can hand input to a child. Explicit
release, process stop/exit and terminal hangup invalidate the epoch, discard
unsent paste data and end framing. A later receiver starts a new epoch; it
cannot receive the old transaction's payload or buffered Enter. Native operations
must enforce this lifetime rather than rely only on cooperative cleanup.

Focus/layer or mux-view changes cancel an active paste. They need not unregister
a still-editing reader; after return to a live view, a new gesture and admission
are required. During an admitted transaction, its payload is delivered only to
the registered process. Other readers cannot consume paste data. Outside that
interval, existing console READ semantics remain unchanged.

### Admission with pending input

Admit only with a live receiver at a complete decoding boundary and
no earlier ordinary input. This includes the destination FIFO, mux outer staging,
its partial key/prefix state and the destination pane's pending input. An
outstanding empty read by the registered receiver may wake for Paste; an ordinary
read by another process or an incomplete decoder makes admission busy.

If any earlier input remains, refuse busy and consume that attempt. Do not
flush it, queue a deferred paste, or wait and silently insert into a later line
or successor. The reader declares its parser boundary; the kernel validates
its registration/input state and mux validates its own staged state. Reserve
one transaction per space and pin the chosen clipboard generation, receiver
epoch, input session and focused live pane/view as one admission. If those
identities change during admission, refuse without delivering text.

Validate the complete text before admitting: UTF-8 without NUL, normalize CRLF
and lone CR to LF, allow printable ASCII/LF/Tab, and refuse non-ASCII or other
ASCII controls/DEL for these readers. Refusal changes neither store nor input.
This excludes embedded bracket markers, Ctrl+B and byte 3 from the payload.
Already queued Enter is not fresh submission; the admission boundary must not
let older staged input arrive after Paste begins.

### Framing, progress and cancellation

An owned snapshot and its begin/end framing form one receiver-bound
transaction, independent of subsequent clipboard replacement or clear. Brackets
are framing, not displayed text. Libterm inserts ASCII and maps each newline/tab
to a space, reporting line-capacity exhaustion while finishing framing. Paste
never submits a line, triggers history/interrupts or enters mux command routing.
Completion means the receiver has consumed the end/cancellation and left paste
mode, not that the sender has queued the final bytes. Until then, ordinary typing
stays suppressed. Submission needs a fresh physical Enter after that boundary;
an Enter held or pressed before/during Paste needs release and a new press.

Physical press/repeat/release handling belongs to the kernel keyboard frontend,
which retains Enter freshness and routes cancellation through native transaction
state, including through mux. Libterm cannot infer physical freshness from
ordinary logical input bytes. Ordinary typing is suppressed rather than buffered
for later. Physical Ctrl+C cancels Paste **before** following its existing interrupt
or line-cancel path; pasted control bytes never reach that path. Unmodified
Escape cancels Paste. Super+Esc retains its existing kernel-escape priority,
and mux's Ctrl+B retains its normal meaning outside the paste interval.

The existing local/session input queues are 4 KiB and mux stages 256 bytes per
pane. Keep the snapshot/progress across short acceptance and backpressure; do
not fit it by enlarging, overflowing or silently truncating those queues. Mux
sends to the fixed pane directly, bypassing outer-byte prefix routing. Native
readiness drives progress within the text/storage bounds below; current items,
staging and active snapshots all count. A second Paste is refused busy.
The five-second total transaction deadline bounds admission through completion, without restarting after a short transfer.

Cancel on release/stop/exit/hangup, focus or layer loss, or any mux view
invalidation: history movement, layout change, pane closure or resize, even if
the pane survives. Explicit cancellation or deadline expiry also ends delivery.
Stop future chunks and discard queued paste payload belonging to that epoch;
terminate decoder framing independently of ordinary FIFO capacity. Never retarget
to another pane/program or send an old closing marker to a successor. The
receiver must consume completion/cancellation before ordinary input resumes.
If the receiver disappears, epoch invalidation ends the transaction without
waiting for its acknowledgement; no framing/data transfers to the successor.
A blocked unrelated output operation has no new guaranteed cleanup time.

Cancellation may leave an already inserted prefix visible and editable, but
never submits it; normal Ctrl+C may discard the line as today. Report partial
insertion/cancellation. A fresh gesture can try again only after the reader and
queues meet admission again. This cancellation and admission contract is part of
the authorized first delivery.

## Size and lifetime bounds

The first-delivery limits are accepted 2026-10-09. The later typed-object column
remains proposed. These are admission policy, not placeholder ABI declarations;
measure peak staging/retained memory before proposing increases.

| Resource | Accepted first terminal delivery | Later typed-object recommendation |
| --- | --- | --- |
| Current item | One per local layer and one shared | Same; no history manager |
| UTF-8 text form | 64 KiB, excluding a string terminator | Same |
| Typed item total | Same text, stored once | 8 MiB including primary, text and FILE-backed logical bytes |
| Retained storage | 8 MiB global, including old/paste/staging bytes | 64 MiB global; 16 MiB per receiving/publishing space |
| Retained item versions | Current slots and staging; no general snapshot handles | 128 globally, at most four distinct retained versions per receiving space |
| UI paste transactions | One active per space | Same initially |
| Activation | One attempt, five-second expiry | Same initially |
| Persistence | RAM only; clear explicitly or at reboot | Same initially |

Current entries, staging overlap and active paste snapshots count together;
replacing a full item may need both old and new storage. Admission failure is
visible, preserves the previous item and does not truncate. Shared/local copies
are independent publications, not aliases that keep changing. No idle timeout
silently loses copied data. Clear requires the same chosen-layer authority and
fresh action as Copy; no additional key is assigned in the first delivery. Local
state clears when its space is actually retired; this milestone adds no space-teardown operation. Shared state survives
source program/space exit until replacement, clear or reboot. Active snapshots
retain their bytes through replacement/clear, but new requests see the new item.

The first delivery exposes no general-purpose retained snapshot handles; storage
has current slots, bounded staging and active UI paste snapshots.
For later FILE grants, recommendation: charge each retained item's logical
bytes, even with nonresident backing, to the global budget once and to each
receiving/publishing space that retains it. Same-space handle copies do not
allocate another item or evade its charge. Cross-space transfers must admit
the destination's charge before installing a grant. Current, staged and old
referenced versions count; no forced revocation of admitted snapshots to make
room for another copy. A limit refuses the new operation and preserves existing
items. The later-column values and transfer accounting need owner approval and
source review before the typed-object task; they are proposed tuning, not existing FILE rules.

## First-delivery limits

Accepted 2026-10-09; the code PR will record which are implemented and qualified.

- Paste goes only to opted-in stock libterm line readers. vi, less, Links and
  other raw-mode programs refuse it until they implement their own receiver.
- Multi-line text becomes one editable line: LF/Tab become spaces. Paste never
  submits it; a fresh physical Enter after receiver-consumed completion is needed.
- Copy exports ASCII only and refuses non-ASCII selections. It LF-joins physical
  rows and trims trailing spaces, including intentional trailing spaces. Original
  tabs and soft wraps are not reconstructed.
- Each layer holds one current item. Storage is RAM only, lost at reboot, with
  no clipboard history. Source exit does not discard a completed copied item.
- Text is at most 64 KiB; aggregate clipboard storage is at most 8 MiB, including
  current, staging and active paste snapshots. One Paste transaction per space;
  unused activation expires after 5 s and admitted Paste after a separate 5 s
  total deadline, without restarting on progress.
- Earlier pending input makes Paste refuse busy. It is never queued for a later
  line/program, flushed to make room, or silently retargeted after focus changes.
- No SDL2/graphics clipboard, FILE/rich representations, converters or remote/
  host clipboard bridge in this first delivery. Those remain later work.

The [technical-debt entry](../technical-debt.md#initial-clipboard-delivery-limits)
records the consequences and revisit points beyond this delivery.

## Proposed FILE and path contract

An item contains a MIME representation and a separate mandatory UTF-8 text
form. Proposed type labels are canonical lower-case ASCII `type/subtype`, at
most 127 bytes, without arbitrary MIME parameters; text encoding is fixed UTF-8.
A label is descriptive metadata, not proof of content or authority. Later
exact-match representations may be held as bounded bytes or as a
read-only FILE capability whose lifetime and frozen contents the store can
actually retain. A FILE handle's numeric value is process-local, not an object
ID that can be pasted in text. Plain text does not encode capability authority.

For example, copying an image with text form `home://pictures/photo.png` then
pasting into a terminal inserts that descriptive string only. The destination
may have a different `home://` binding or no right to that path. A later explicit
open resolves in the destination's own grants and can fail or find a different
file; paste performs no lookup, mount, provider OPEN or implicit authority grant.
An image editor requesting the image representation receives the copied object
or an attenuated READ-only grant to its frozen snapshot, never a source directory,
WRITE, launcher or namespace grant. A navigator must use the same distinction;
[file operations between navigators](terminal-applications.md#operations-between-navigators)
remain separately proposed. A file-list/path-list type needs its own object
contract before clipboard support, not automatic treatment as authority.

READ alone does not freeze a mutable file, and a copied
[provider FILE](../interfaces/userspace-services.md#file-opens-and-snapshots)
still fails when its provider exits. Recommendation: copy mutable or
source-provider data into store-owned immutable backing before publication, or
retain known immutable native backing, or a trusted immutable provider whose
lifetime is independent of the source app, its execution group and source-space
teardown. Provider failure still reports unavailable without substituting a path
lookup; the owned text form remains available. Generic FILE
metadata/MIME/READ authority does not authenticate immutability; unsupported
sources need complete independent staging.
Reject an unsupported lifetime rather than promising source-exit survival.
Do not label a staged read of a concurrently modified file as a coherent
filesystem snapshot; require a frozen source or refuse mutation uncertainty.
Only complete, validated staging becomes the current item. Rich types and FILE
publication are later tasks; the first delivery stores owned text only.

## Proposed converter authority and execution

Userspace converters remain accepted. Recommendation: trusted, explicitly
packaged handlers registered by trusted startup, exact MIME matches preferred,
and at most one conversion step per request initially. No MIME-to-executable
lookup through a pasted path, environment variable, caller-supplied command or
untrusted namespace. No recursive chain/plugin discovery. The mandatory text
form is supplied at publication; terminal paste never launches a converter.

A trusted userspace coordinator receives only the selected input snapshot for
that request, ordinary launcher/group-creation authority and explicit READ
grants to packaged native handler images. Choosing only those images is trusted
coordinator policy; current launcher rights do not restrict executable names.
It starts a converter with a READ-only input, private memory, a bounded result
channel and the clock it needs. No clipboard store grant, destination input, roots, namespace, network,
terminal, general launch authority or unrelated FILE grants reach the converter.
The coordinator retains termination/observation authority in an execution group,
validates type/length and independently owns completed output before delivery.
Converters cannot replace the current clipboard as a side effect. Converted
output belongs to that paste request; failed conversion preserves current data.

Proposed conversion policy: one request per destination, five-second deadline,
output limited to the requested representation bound, explicit busy/unsupported/
failed/timed-out diagnostics and no insertion on failure. A terminal already has
the item's text form; a graphics request does not silently fall back to text if
its requested type cannot be produced. Cancellation requests termination of
the converter group; late output is discarded. A
[CALL deadline](../interfaces/endpoints.md) by itself does not stop delivered
work. Current [execution groups](../interfaces/execution-groups.md) provide
termination authority, not a fixed cleanup deadline or CPU/memory quota. Admit
only trusted packaged handlers in this milestone; hostile plugin containment,
supervision/restart and conversion caches remain separate work. Settle retained
result accounting and actual cleanup limits before authorizing converter code.

## Proposed SDL2 first graphical consumer

After terminal text delivery, implement the native clipboard adapter in the
[SDL2 port](../development/sdl2.md). SDL2's string clipboard surface maps to
UTF-8 `text/plain` and its text form; it does not pretend to expose arbitrary
MIME objects through string functions. Set publishes copied text; Get returns
an SDL-owned NUL-terminated copy; Has reports eligibility/availability without
returning contents. Denied, expired or unsupported requests report SDL errors,
never synthesize a successful clipboard operation.

Ordinary application Ctrl+C/V supplies local intent as above. Explicit shared
system actions use the adapter's private operation/layer context. Neither
continuous polling nor SDL-generated key events refreshes native permission.
Has must not consume Paste activation merely by checking; Get/Set consume the
matching attempt. With no eligible input ownership/activation, Has returns false
and Get/Set report refusal. No background clipboard monitoring is included.
Qualify copy, paste, empty text, focus changes, source exit, shared refusal and
one-attempt consumption with a real SDL text consumer before pinning the port.
Publish its ports PR before the Pyxis gitlink; userland/native dependencies merge
first. Pointer/cursor/input and clipboard remain separate grants and protocols.

## Task breakdown

1. **First delivery: terminal Copy + Paste.** Both stores, completed
   local-TTY/Caelum and mux selection export, accepted gestures and explicit
   controller grants, followed by safe Paste into stock libterm line readers.
   Include receiver lifetime, admission, framing, backpressure and cancellation
   under the accepted round-two contract. Two PRs back to back may separate owned Copy/store
   work from receiver/Paste work for review; both form this delivery. No SDL2,
   FILE payloads, converters or remote bridge. Capture selection/idle baselines
   before code, then qualify both layers, no implicit Copy, non-ASCII refusal,
   selected/focused pane routing, empty text, source exit/mutation, replacement
   mid-paste, competing opt-in, pending input, short transfers, admission limits,
   focus/view changes and multiline paste not executing commands in matched QEMU.
2. **SDL2 text adapter.** Local/shared graphical action routing and native
   Set/Get/Has integration, owned text and a real consumer. No background access
   or implied menu-pointer permission; graphical activation awaits a later round.
3. **Typed objects and FILE retention.** Exact-match representations and
   mandatory text form; immutable/source-independent backing, capability
   attenuation, retained admission and concrete path examples qualified.
4. **Trusted converters and closure.** One-step bounded userspace conversion,
   explicit cancellation/failure and authority checks; rewrite implemented
   contracts into references and carry deferred limits into technical debt.

The first delivery is authorized 2026-10-09 after both decision rounds.
Later tasks need separate decisions/assignment. Remote clipboard bridging, USB
input, arbitrary plugins, history, persistence, Unicode terminal layout and
Continuum are outside this milestone. If Paste needs broader reader ownership
than this scoped transaction, bring that contract to the owner before code.

## Accepted round two: safe-paste receiver

All three defaults accepted 2026-10-09. The detailed contract is above.

1. **Opt-in and lifetime:** exclusive, process-owned registration by
   the stock libterm line reader with matching input/output authority; fresh
   receiver epoch; release on every return/handoff and invalidate on stop, exit
   or hangup. Paste goes only to that registered receiver; no ownership transfer
   through copied grants and no raw-reader fallback.
2. **Admission with pending input:** refuse busy unless the registered
   reader is at a clean decoder boundary and local/mux staged and queued input
   is empty. Preserve earlier input, consume the attempt, never defer it to a
   later line. Pin the item, receiver/session and focused live pane/view at
   admission; competing readers or changing identities refuse.
3. **Framing and cancellation:** receiver-bound bracketed transaction,
   ASCII insertion with LF/Tab mapped to spaces, fresh post-paste Enter and short
   transfer progress. Kernel keyboard handling owns physical freshness and
   cancellation; ordinary typing resumes only after the reader consumes end/
   cancellation. Physical Ctrl+C retains its normal effect after cancellation;
   unmodified Escape cancels. Receiver loss or focus/layer/view change discards
   remaining transaction data and ends framing without retargeting or submission.
   Accepted bounds: one transaction per space, 64 KiB text, 8 MiB aggregate storage,
   five-second unused-activation expiry and a separate five-second total Paste
   deadline from admission. The latter does not restart on partial progress.

Graphics/menu activation, retained FILE admission and converter execution limits
remain proposed for later rounds/tasks. Round one approved the first-delivery
scope; round two approved the receiver contract and first-delivery numeric limits.
