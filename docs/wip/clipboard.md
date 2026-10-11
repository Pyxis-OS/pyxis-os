# Clipboard

Status: **terminal and SDL2 text delivery implemented** (Pyxis #619 terminal and
mux Copy and Paste, #644 SDL2). The stores, grants, gestures, activation, safe
paste receiver, bounds and SDL2 adapter are specified in the
[clipboard interface](../interfaces/clipboard.md); measured and manual evidence is
in the [first-delivery qualification](../development/clipboard-first-delivery-qualification.md)
and the [SDL2 record](../development/experiments/sdl-clipboard/README.md); limits
are in [technical debt](../technical-debt.md#initial-clipboard-delivery-limits).
FILE representations, converters and remote bridging remain proposed.

## Owner decisions

Accepted 2026-10-08:

1. **Two layers:** a clipboard local to each space, and one shared across
   spaces. This matters most once the [multiplexer](terminal-applications.md)
   exists. In [Asterism](spaces.md#asterism), each space's Continuum supervisor
   is a natural owner of the local one.
2. **Typed objects, not a text clipboard.** Every object has a type, using MIME
   types such as `text/plain`, `image/png` and `image/svg+xml`.
3. **Every object must have a text representation.** A photo pasted into a
   terminal pastes its text form, such as its path; pasted into a photo editor,
   it pastes the photo itself.
4. **The clipboard holds the object.** Copying stores the object, so it survives
   the source program exiting, unlike X11 and Wayland. Large objects can be held
   as file capabilities rather than bytes.
5. **Conversion handlers in userspace.** When a program asks for a type the
   clipboard does not hold, a converter can supply it; for example, SVG to PNG.
   Haiku's Translation Kit is a working precedent.

Accepted 2026-10-09 and implemented (details in the interface reference):

- Caelum owns one volatile store per space and one shared store, each holding one
  immutable typed item with a mandatory text form. They belong to the space
  registry and boot session, not to shell, launcher, mux, graphics owner or last
  publisher. The kernel stores and validates; it parses no image formats, opens no
  paths and runs no converters. Moving the stores into supervised services (for
  example under Continuum) needs a separate migration proposal.
- Clipboard authority is separate from console, pointer, terminal CONTROL, FILE
  and namespace authority; trusted startup delegates the local and shared grants
  explicitly, and a converter never holds a store grant.
- Gestures, one-attempt five-second activation, receiver opt-in and lifetime,
  busy refusal with pending input, framing and cancellation, 64 KiB text, 8 MiB
  aggregate storage, one Paste per space, and a separate five-second Paste
  deadline.
- SDL2: separate grants through local foreground startup, fresh physical
  activation, bounded UTF-8 Get/Set/Has, qualified with DevilutionX.
- If Paste needs broader reader ownership than the scoped transaction, bring that
  contract to the owner before code.

## Proposed FILE and path contract

An item contains a MIME representation and a separate mandatory UTF-8 text
form. Type labels are canonical lower-case ASCII `type/subtype`, at most 127
bytes, without MIME parameters; text encoding is fixed UTF-8. A label is
descriptive metadata, not proof of content or authority. Exact-match
representations may be held as bounded bytes or as a read-only FILE capability
whose lifetime and frozen contents the store can retain. A FILE handle's numeric
value is process-local and cannot be pasted in text.

Pasting an image with text form `home://pictures/photo.png` into a terminal
inserts only that string. The destination may have a different `home://` binding
or no right to the path; paste performs no lookup, mount, provider OPEN or
implicit authority grant. An image editor requesting the image representation
receives the copied object or an attenuated READ-only grant to its frozen
snapshot, never a source directory, WRITE, launcher or namespace grant. A
navigator must use the same distinction; [file operations between
navigators](terminal-applications.md#operations-between-navigators) are proposed
separately. A file-list or path-list type needs its own object contract first.

READ alone does not freeze a mutable file, and a copied
[provider FILE](../interfaces/userspace-services.md#file-opens-and-snapshots)
fails when its provider exits. Recommendation: copy mutable or source-provider
data into store-owned immutable backing before publication, or retain known
immutable native backing, or a trusted immutable provider whose lifetime is
independent of the source app, its execution group and source-space teardown.
FILE metadata, MIME and READ authority do not authenticate immutability;
unsupported sources need complete independent staging, and an unsupported
lifetime is rejected. A staged read of a concurrently modified file is not a
coherent snapshot: require a frozen source or refuse. Provider failure reports
unavailable without a path lookup; the text form remains. Only complete,
validated staging becomes the current item.

### Proposed typed-object bounds

Admission policy, not ABI; measure peak staging and retained memory before
increasing. Item total 8 MiB including primary, text and FILE-backed logical
bytes; retained storage 64 MiB global and 16 MiB per receiving or publishing
space; 128 retained item versions globally and at most four distinct versions per
receiving space; no history manager; current limits (one item per layer, one
active Paste per space, five-second activation, RAM only) otherwise unchanged.
Charge each retained item's logical bytes, even with nonresident backing, to the
global budget once and to each space that retains it. Same-space handle copies do
not allocate again; a cross-space transfer admits the destination's charge before
installing a grant; current, staged and old referenced versions count; admitted
snapshots are never revoked to make room. A limit refuses the new operation and
preserves existing items. These values and the accounting need owner approval and
source review before the typed-object task.

## Proposed converter authority and execution

Userspace converters remain accepted. Recommendation: trusted, explicitly
packaged handlers registered by trusted startup, exact MIME matches preferred,
and at most one conversion step per request initially. No MIME-to-executable
lookup through a pasted path, environment variable, caller-supplied command or
untrusted namespace, and no recursive chain or plugin discovery. The text form is
supplied at publication; terminal paste never launches a converter.

A trusted userspace coordinator receives only the selected input snapshot,
ordinary launcher/group-creation authority and explicit READ grants to packaged
handler images. Restricting launches to those images is coordinator policy;
current launcher rights do not restrict executable names. A converter gets a
READ-only input, private memory, a bounded result channel and the clock it needs;
no clipboard store grant, destination input, roots, namespace, network, terminal,
general launch authority or unrelated FILE grants. The coordinator keeps
termination and observation authority in an execution group, validates type and
length, and owns completed output before delivery. Converters cannot replace the
current clipboard as a side effect; converted output belongs to that paste
request, and a failed conversion preserves current data.

Conversion policy: one request per destination, five-second deadline, output
limited to the requested representation bound, explicit busy, unsupported,
failed and timed-out diagnostics, and no insertion on failure. A graphics request
does not silently fall back to text if its type cannot be produced. Cancellation
terminates the converter group and discards late output; a
[CALL deadline](../interfaces/endpoints.md) alone does not stop delivered work,
and [execution groups](../interfaces/execution-groups.md) provide termination
authority, not a cleanup deadline or CPU/memory quota. Only trusted packaged
handlers are admitted in this milestone; hostile plugin containment,
supervision and restart, and conversion caches are separate work. Settle retained
result accounting and cleanup limits before authorizing converter code.

## Tasks

1. [x] **First delivery: terminal Copy + Paste** (#619).
2. [x] **SDL2 text adapter** (#644).
3. [ ] **Typed objects and FILE retention.** Exact-match representations and
   mandatory text form; immutable, source-independent backing, capability
   attenuation, retained admission and concrete path examples qualified.
4. [ ] **Trusted converters and closure.** One-step bounded userspace conversion,
   explicit cancellation and failure, authority checks; rewrite implemented
   contracts into references and carry deferred limits into technical debt.

Later tasks need separate decisions and assignment. Remote clipboard bridging,
USB input, arbitrary plugins, history, persistence, Unicode terminal layout and
Continuum are outside this milestone.
