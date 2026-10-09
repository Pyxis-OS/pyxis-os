# Clipboard

Status: **assigned docs-only proposal, 2026-10-09**. The five owner decisions
below are accepted; every recommendation after them is proposed, not agreed or
implemented. No code, interface declarations or placeholder APIs are part of
this PR. The first implementation task waits for owner authorization.

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

## Proposed ownership before Continuum

**Recommendation:** Caelum owns one volatile store per space and one shared
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

Alternatives: a persistent service per space plus one shared service keeps
storage in userspace but adds service bootstrap, failure/restart policy and a
kernel-local selection bridge before the first paste. Making the session or
launcher the store owner loses data at their normal handoff/exit and is not
recommended. The accepted two-layer decision does not settle this owner choice.

## Proposed authority and activation

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

**Recommendation:** fresh operation-specific activation, modeled on
[pointer lock](../interfaces/pointer.md#relative-lock-and-user-escape), authorizes
one Copy or Paste attempt for one layer and UI owner. It expires after a
proposed five seconds, and is consumed even on refusal. Repeats, held keys,
selection dragging, pointer warp, polling and synthetic application events
create none. Focus/layer/input reset, owner exit or session replacement cancels
unused activation. Already captured data is never silently retargeted.

Cross-space Paste is an explicit shared-layer action at the destination. It
captures the shared item's current generation once and grants only that item's
requested representation, not access to the source space's roots, namespace,
terminal or future clipboard contents. It works after the source space becomes
inactive. A source disappearing does not revoke a completed independent item.
Local fallback is never substituted for an empty/denied shared paste or vice
versa. Refusal inserts nothing and leaves the old store intact.

## Proposed keys and graphics integration

**Recommendation for shown local terminals and mux:** exact Ctrl+Shift+C/V for
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

The following graphical routing is proposed for the later SDL2 task, outside
round one. Graphical Ctrl+C/V (and Ctrl+Shift+C/V, without Alt/Super) remain
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

## Proposed owned text handoff

Freeze the completed selection and revalidate its source/view identity before
publication. Local TTY staging follows BSP allocation and output-lock ownership;
no allocation under the output lock or borrowed cell pointer survives Copy.
Mux constructs its own owned snapshot from its selected pane rows. Mutation
between sizing and capture either retries a still-valid selection within bounds
or refuses; it never publishes mixed cells. Publication replaces the current
item atomically only after complete validation/admission. Failure preserves it.

**First-task recommendation:** printable ASCII glyph indices 0x20..0x7e map
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

## Proposed safe terminal paste

Paste is a bounded input transaction with an owned text snapshot, fixed
receiver/process/session epoch and progress. It is not a sequence of keyboard
reports or an unowned string pushed into the console FIFO. Require a live,
explicitly opted-in bracketed-paste receiver; default refusal is safe for older
or raw readers. Ordinary console READ/WRITE or a program emitting a mode escape
alone does not identify a safe input consumer. The receiver opt-in and end of its
input ownership must have an explicit lifetime, including process exit/handoff.
Recommendation for task 2: the receiver must hold READ on the destination
input and WRITE on its matching terminal output, be in that terminal's space,
and opt in while its line reader is active. Registration is exclusive and
process-owned; a competing opt-in is refused busy. Copying a grant does not
transfer the registration. Explicit release, process exit/stop, terminal hangup
or input handoff ends it. No general foreground-input arbitration is added
outside this paste interval. This is new behavior requiring task-2 approval.

For task 2, stock libterm line readers opt in while editing. A paste
interval reserves delivery to that receiver; other readers cannot consume its
payload. Ending the receiver or changing focus/layer discards unsent transaction
data and prevents it appearing as ordinary input to a successor. Implement this
receiver-bound delivery before promising arbitrary terminal-session paste.

Validate text before admitting a transaction: UTF-8 without NUL; normalize CRLF
and lone CR to LF, allow LF/Tab, and refuse other ASCII controls/DEL. Initial
ASCII line readers also refuse non-ASCII text rather than feeding multibyte
glyphs to them. The clipboard itself is unchanged on refusal. This prevents
embedded bracket-end escapes, Ctrl+B and byte 3 from escaping the transaction.
Older/raw readers get no unframed fallback.

Admission also requires empty ordinary input, including mux outer staging,
destination-pane pending bytes and the destination FIFO, with an opted-in reader
at a complete decoding boundary. If earlier input or a partial key remains, refuse
busy; do not drop it, wait and silently paste into a later line, or reinterpret
an already queued Enter as a fresh submission. Bracket begin/end are framing,
not displayed text. The single-line reader inserts printable ASCII, maps each
newline/tab to a space and reports line-capacity exhaustion without submission.
It waits for a fresh physical Enter after the closing frame; Enter pressed
during the transaction is suppressed, not queued for later submission. No paste
byte triggers Ctrl+C interruption, history, or mux command routing. Supporting
arbitrary terminal-session readers comes after this scoped safe reader.

The current local console and session input queues are 4 KiB, and mux stages
256 bytes per pane. Do not enlarge/drop into those queues to fit a clipboard
item. The sender retains its snapshot across short acceptance/backpressure,
uses native readiness and advances only accepted progress. Mux receives the
explicit Paste action, then sends to the fixed pane directly, bypassing ordinary
outer-byte command routing. Bracket framing and payload stay ordered as one
paste transaction even across short transfers. A second Paste is refused busy;
ordinary typing is suppressed until completion/cancellation rather than mixed
into it. Closing the destination cancels; focus/layer change or mux view invalidation
(history movement, layout, pane closure or resize) cancels remaining delivery
and releases receiver state, never retargeting another pane/program. Even a
layout change that keeps the pane alive cancels initially.
Partial content already inserted may remain visible; cancellation is reported
and never submits it. Cancellation/teardown must also terminate framing state.

## Proposed size and lifetime bounds

These are proposed admission settings, not new ABI constants or immutable
architecture. Measure peak staging/retained memory before increasing them.

| Resource | Text phase (tasks 1–2) recommendation | Later typed-object recommendation |
| --- | --- | --- |
| Current item | One per local layer and one shared | Same; no history manager |
| UTF-8 text form | 64 KiB, excluding a string terminator | Same |
| Typed item total | Same text, stored once | 8 MiB including primary, text and FILE-backed logical bytes |
| Retained storage | 8 MiB global, including old/paste/staging bytes | 64 MiB global; 16 MiB per receiving/publishing space |
| Retained item versions | Current slots and staging; no general snapshot handles | 128 globally, at most four distinct retained versions per receiving space |
| UI paste transactions | Deferred to task 2: one active per space | Same initially |
| Activation | One attempt, five-second expiry | Same initially |
| Persistence | RAM only; clear explicitly or at reboot | Same initially |

Current entries, staging overlap and active paste snapshots count together;
replacing a full item may need both old and new storage. Admission failure is
visible, preserves the previous item and does not truncate. Shared/local copies
are independent publications, not aliases that keep changing. No idle timeout
silently loses copied data. Clear requires the same chosen-layer authority and
fresh action as Copy; no additional key is assigned in the first task. Local
state clears when its space is actually retired; this milestone adds no space-teardown operation. Shared state survives
source program/space exit until replacement, clear or reboot. Active snapshots
retain their bytes through replacement/clear, but new requests see the new item.

Task 1 exposes no general-purpose retained snapshot handles; clipboard storage
has current slots and bounded staging. Task 2 adds active UI paste snapshots.
For later FILE grants, recommendation: charge each retained item's logical
bytes, even with nonresident backing, to the global budget once and to each
receiving/publishing space that retains it. Same-space handle copies do not
allocate another item or evade its charge. Cross-space transfers must admit
the destination's charge before installing a grant. Current, staged and old
referenced versions count; no forced revocation of admitted snapshots to make
room for another copy. A limit refuses the new operation and preserves existing
items. The later-column values and transfer accounting need owner approval and
source review before task 4; they are proposed tuning, not existing FILE rules.

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
publication are later tasks; task 1 stores owned text only.

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

## Proposed task breakdown

1. **Owned terminal Copy and stores.** Both kernel-owned layers for completed
   local-TTY/Caelum and mux selection snapshots, terminal Copy gestures,
   explicitly delegated controller grants, one-attempt Copy activation,
   64 KiB owned ASCII/UTF-8 text and the 8 MiB aggregate bound. Publication
   supplies `text/plain` and its mandatory text form once, atomically. Refusal
   preserves the previous item. No Paste, graphical clipboard, FILE payloads,
   converters or remote bridge. Capture selection/idle baselines before code;
   qualify both layers, Copy without selection, non-ASCII refusal, selected-pane
   identity, source mutation/exit, empty text, replacement and admission limits
   in matched QEMU. Inspect stored snapshots with existing read-only debugger
   tools; do not add a test-only clipboard command or placeholder paste API.
2. **Safe terminal Paste.** Receiver registration and lifetime, bracket parsing,
   ordinary-input admission boundary, control/newline safety, fixed local/mux
   destination, backpressure and cancellation. Opted-in stock libterm readers
   only; no raw-reader fallback. Settle this new receiver contract before code,
   then qualify multiline paste not executing commands, competing opt-in,
   partial transfers, focus/view changes, source exit and replacement mid-paste.
3. **SDL2 text adapter.** Local/shared graphical action routing and native
   Set/Get/Has integration, one owned text representation and a real consumer.
   No autonomous/background access or implied menu-pointer permission.
4. **Typed objects and FILE retention.** Exact-match non-text representations
   with mandatory text form; immutable/source-independent backing, capability
   attenuation, global/per-space retained admission and path examples qualified.
5. **Trusted converters and closure.** One-step bounded userspace conversion,
   explicit cancellation/failure and authority checks; rewrite implemented
   contracts into references and carry deferred limits into technical debt.

These tasks each need explicit authorization. Remote clipboard bridging, USB
input, arbitrary converter plugins, history, persistence, Unicode terminal
layout and Continuum are outside this milestone. If task 2 needs broader reader
ownership than the scoped paste transaction, bring that contract to the owner
before implementing it.

## Owner decision round one

These three questions are **pending**, with recommended defaults. Only an
explicit owner answer records acceptance; merging this docs proposal does not
approve unanswered recommendations or authorize implementation.

1. **Who owns storage and gates actions before Continuum?** Default: the kernel
   owns both volatile stores and verifies focus/owner/one-attempt activation;
   trusted startup delegates separate local/shared grants and UI controllers.
   Alternative: independently started userspace store services plus a kernel
   activation/selection bridge; never make the transient session/launcher own
   clipboard lifetime.
2. **Which terminal gestures and source text?** Default: Ctrl+Shift+C/V local
   and Super+Shift+C/V shared; completed selection's pane for Copy, focused live
   pane for later Paste; reject non-ASCII glyphs, LF-join physical rows and trim
   trailing spaces. Ctrl+C/V and mux's prefix keep their current meanings.
   Alternative: different explicit chords or a verified complete font mapping
   before any selection export. Later graphics routing is outside this round.
3. **What is the first task boundary and its limits?** Default: task 1 is
   Copy/storage only, both layers with 64 KiB owned text, an 8 MiB aggregate bound
   and five-second single-attempt activation. Safe Paste is task 2 after its
   receiver contract is accepted. Alternative: combine text Copy/Paste in one
   larger task after first settling the receiver/admission contract. Neither
   choice is authorization to start code.

Next rounds cover safe receiver-bound Paste, SDL2 graphical activation/menu
approval, retained FILE admission and trusted converter execution limits. Their
recommended contracts above remain proposals until the owner answers them.
