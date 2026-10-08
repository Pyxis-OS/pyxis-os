# Screenshots

Status: **accepted milestone, 2026-10-08; assigned to Codex 2.** The owner
merged [PR #501](https://git.internal/PyxisOS/pyxis-os/pulls/501), accepted all
three contract groups below. Tasks 2 and 3 are delivered, including the owner's
staged-output follow-up from PR #504. Later tasks remain separate assignments;
acceptance does not start them implicitly.

## Goal and completion

Capture the currently shown local screen as a PNG from a Pyxis command, then
fetch it through the existing remote terminal. This must work with the boot
framebuffer used on the ThinkPad as well as QEMU's Bochs and VirtIO displays.

The milestone finishes when the ordinary image contains the command, the
libraries are reusable development outputs, QEMU capture and download are
qualified, and the owner has checked a native ThinkPad capture. Implemented
interfaces and usage then replace this WIP document in the subsystem references.

## Accepted decisions

Accepted by the owner on 2026-10-07:

- **Route.** A `screenshot` command writes a guest image file. The host fetches
  it through [pyxis-remote download](../userland/remote-terminal.md#explicit-file-transfer).
  There is no kernel network responder. Revisit that only if frozen-system
  images become necessary; [UDP kernel logging](../development/remote-debugging.md)
  already covers text from a stuck system.
- **Format.** PNG, through new reusable zlib and libpng
  [ports](application-ports.md#libraries-and-terminal-tools). The screenshot
  command is their first consumer.

## Inspected starting point

Initial source inspection for task 1 used Pyxis `50e18a5`, userland
`2430567f519424d83ba4dcd5bf7ba3565ba97c86` and ports
`03b3ae8ed59ef733b9b21f2beda34770f5d2fb66`. No library build or capture was run
at that point; subsequent library validation is recorded in the handoff.

- [DRAW](../interfaces/graphics.md#authority-and-ownership) applies only to a
  display's owning space. Remote shells receive no display resource. A DRAW
  grant alone therefore cannot implement remote capture of the selected screen.
- [The presenter](../../kernel/space.c) copies navigation, selected content,
  margins and the visible TTY cursor into the physical driver. Only VirtIO has
  a full RAM target; boot and Bochs copy directly to device memory. There is no
  common complete staged frame to read today.
- Graphics backing stays writable during presentation. PRESENT and the internal
  `display_snapshot()` backing lease do not freeze pixel contents; the existing
  [single-buffer contract](../interfaces/graphics.md#mapping-and-presentation)
  permits tearing.
- [RAM files](../../include/kernel/object/file.h) already own their bytes and
  support explicit-offset READ. A read-only exported handle can carry an
  immutable completed image without a new user mapping or release protocol.
- The pinned libc has real `setjmp`/`longjmp`, allocation, memory functions,
  ordinary file I/O and math functions. The task-3 source audit identified
  `modf` as a concrete prerequisite for libpng's conventional floating-point
  sCAL API; a focused userland dependency adds it from the existing musl pin.

## Agreed contracts

Accepted by the owner on 2026-10-08. These contracts guide implementation;
the task checklist and handoff distinguish completed work from planned behavior.

### Authority and captured content

Use a separate global screen-capture object and named `screen_capture` grant,
with capture authority alone. Keep per-space DRAW unchanged. The new resource
permits observation of the currently selected local screen regardless of the
caller's space; it permits no drawing, graphics acquisition, input capture,
mode setting or device access.

The result includes the navigation bar, the selected space's shown layer,
clipping/background margins and the visible software cursor. It does not
capture hidden surfaces or each space separately. It follows the
[space layer](../userland/space-layers.md) actually selected by the presenter,
without adding screenshot-specific layer selection.

Add an optional per-space boot setting `screenshot = true`, defaulting to false.
Boot init delegates the resource only for opted-in spaces. Enable it
in the packaged live Development, installed `pyxis` and Remote spaces; leave
other spaces and the built-in rescue space without it. Remote service/session
handoff and shell launch preserve the optional grant through existing explicit
resource lists. Every ordinary command launched with that grant can capture the
whole screen; this is a space policy, not an executable-name restriction. Being
remote, having DRAW or having file-write authority never implies capture access.

### Consistency, ownership and capacity

Capture the next presenter composition after the request is admitted. A request
arriving during a frame waits for the following frame. Select dimensions and
channel layout at that frame's boundary, after any resize transaction. Return
those dimensions, a tightly packed native 32-bit row pitch, RGB channel shifts,
geometry generation and exact byte extent together with a READ-only FILE handle.
Do not include device pitch padding or uninitialized allocation padding.

Allocate capture backing only when requested. During composition, copy each
source chunk into the capture buffer first, then send those same bytes to the
driver. Split copies at row boundaries and map visible pixel spans into compact
rows, preserving the driver's existing device-padding handling. Physical byte
offsets cannot index the compact capture buffer. Reading a live source separately
for capture and physical output could produce different images. Complete successfully only after the driver's normal
frame submission succeeds; expose failure instead of reporting an unpresented
frame as captured.

The returned bytes remain immutable across later drawing, space/layer switches,
resizes and display failure. They describe one presenter composition, which can
already contain tearing from concurrent application or TTY writes. This does
not promise an atomic application frame, vblank or exact physical scanout timing.
It also cannot capture a panic or a system whose presenter/scheduler is stuck.

Allow one global pending/in-flight capture; concurrent admission returns BUSY
rather than building a queue. Overflow or unsupported geometry is refused,
allocation refusal returns NO_MEMORY, and an unavailable/failed backend returns
UNAVAILABLE. Do not keep retrying a failed capture automatically.

A completed FILE owns its pixels until its last reference closes, including
references explicitly copied to another process. Ordinary process cleanup closes
its handles. There is no additional retained-image quota in this milestone:
one pending request bounds capture work, while callers can retain multiple
completed files and exhaust available memory. The command closes its snapshot
promptly on success and every handled failure. Raw backing costs
`4 * width * height` bytes: about 7.91 MiB at 1920×1080 and 31.64 MiB at 3840×2160,
in addition to the driver's existing buffers. Row conversion/encoding needs
bounded row and library working storage, not another full userland RGB image.

Use a typed, parked-caller BSP request that forwards ownership to the presenter
and immediately releases the common FIFO executor. Allocation and capability
installation remain BSP/IF=0 work, outside the output lock; copying and device
waits follow the existing IF=1 frame lease. The service catalog must explicitly
allow this deferred completion path. Keep unpublished backing and the requesting
task alive until completion; check a pending stop before publication, unwind
private resources on refusal/stop/backend failure, clear all loans and presenter
references before waking the caller, and never access request storage afterward.
A small FILE constructor should adopt completed owned storage instead of
exposing FILE internals to the presenter. No new shared user mapping is needed.

### Libraries and encoding

Use the upstream releases below, checked against their upstream release
pages on 2026-10-08. These are archive pins, not moving branches or claims of
successful Pyxis builds.

| Port | Upstream source | SHA-256 |
| --- | --- | --- |
| zlib 1.3.2 | [zlib-1.3.2.tar.gz](https://zlib.net/zlib-1.3.2.tar.gz) | `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16` |
| libpng 1.6.59 | [libpng-1.6.59.tar.gz](https://prdownloads.sourceforge.net/libpng/libpng-1.6.59.tar.gz) | `86a3e4b501f7f50e392c4e456ea158893e9a595b1c63eb88c7bb9f6cf8772dad` |

Release provenance: [zlib](https://zlib.net/) and
[libpng](https://www.libpng.org/pub/png/libpng.html). Before a port build, the
owner must mirror these exact archives and provide their mirror URLs. Recipes
fetch only from those mirrors and check the pinned hashes; there is no upstream
fallback. This needs source mirrors, not a compiler-container rebuild.

Build static target archives against the existing SDK. Export zlib's core
compression/decompression and checksums; omit gzip file helpers and contrib
programs in this first profile. Keep conventional libpng read/write APIs,
standard error recovery and stdio support for reuse; omit its simplified API,
architecture-specific acceleration, shared libraries and upstream programs/tests.
Ship upstream headers and the matching generated `pnglibconf.h`, licenses,
provenance and any ordered patches. Audit the selected sources and resolve only
concrete libc needs through userland's native libc, preserving upstream behavior.

Keep both libraries in the ports development bundle, outside the base SDK.
Order zlib before libpng with an explicit zlib prefix, following the existing
explicit inter-port input pattern. Feed both prefixes into the userland build;
link the command's objects, libpng, zlib and the normal runtime in that order.
Repository changes follow [SDK and repository integration](../development/sdk-and-repositories.md):
publish dependency PRs before parent gitlink updates, with exact revisions and
merge order recorded.

Encode rows as non-interlaced 8-bit RGB, interpreting the snapshot's channel
shifts and pitch rather than assuming a device byte order. Use the conventional
libpng row API and its error recovery. Compression level 3 is the initial
command choice, with ordinary libpng filtering; it is not an ABI requirement.
Measure encoding time and file size during qualification before considering
changes to that choice.

### Command, output and host workflow

Use `screenshot PATH`: a required guest output path, resolved through the
caller's existing roots. Capture authority grants no additional path access.
Accepted owner follow-up on 2026-10-08 in
[PR #504](https://git.internal/PyxisOS/pyxis-os/pulls/504): encode into an
exclusively created sibling temporary file, finish and close its output, then
rename that completed file into place, following
[cp's staged replacement](../userland/cp.md#staged-replacement-and-authority).
A failed capture, encode or write before publication leaves an existing
destination untouched. Success replaces the destination object; older held
handles retain the old object.

Hold the destination parent through reservation, publication and cleanup. Its
LOOKUP, CREATE, WRITE_FILES and REMOVE authority is required; permission to
write the old file alone is insufficient, and there is no direct-write fallback.
As with cp, other writers must leave the temporary name/file alone. Follow
[cp's failure ownership rules](../userland/cp.md#failures-and-limits): clean up
only confirmed reservations before publication; an unconfirmed creation or
publication reports the possible names without retrying or deleting uncertain
state. Abrupt termination can leave a temporary file; there is no automatic
stale-file removal.

Keep PNG encoding in userland and use libc/libpyxis interfaces for ordinary I/O
and native publication. Propagate capture, allocation, PNG and I/O errors.
Success requires checked output completion and confirmed rename. Closing or
renaming alone makes no durability promise; document `sync PATH` and syncing
its parent directory when persistence across reboot matters. Do not claim
RAM-backed `tmp://` survives reboot.

The first workflow stays two explicit guest commands:

```sh
screenshot tmp://screen.png
xfer send tmp://screen.png
```

The host connects with an existing `--download-dir` and retains its normal
confirmation and refusal to overwrite a host name. No one-step pyxis-remote
capture command is included. File transfer's existing 16 MiB limit still applies
to the encoded PNG; an oversized image can be saved locally but its download
is refused. Do not enlarge transfer buffers as part of this milestone.

## Accepted review decisions

The owner accepted these three groups on 2026-10-08:

1. **Authority:** whole-screen capture through the separate optional grant,
   the enabled spaces, and ordinary child inheritance in those spaces.
2. **Consistency and capacity:** one presenter composition with existing tearing,
   one pending capture, immutable FILE lifetime and no new retained-image quota.
3. **Ports and workflow:** the pinned library profiles, initial compression
   choice and explicit two-command download. The owner's subsequent PR #504
   comment accepts the staged-replacement output/failure behavior above.

Changes to these contracts require discussion before implementation. The owner
authorized task 2 after accepting them. Its zlib source mirror is available;
the owner's shared SourceForge mirror now supplies the pinned libpng archive.

## Tasks

Each implementation task includes its focused reference updates and exact-head
existing CI inspection. No new tests, self-tests, boot automation or workflows
are part of this milestone.

1. [x] **Investigate and write the proposal.** Inspect display/remote authority,
   presenter ownership and library integration; record concrete proposed
   contracts and split the work. The owner merged PR #501 and accepted all three
   review groups before authorizing task 2.
2. [x] **Port zlib.** After the source mirror is available, add the pinned
   recipe, static core archive, public headers, notices and development export
   in pyxis-ports. Wire its focused ordinary build in Pyxis. Completion: a
   target archive built with the current SDK, documented profile, published
   dependency/integration PRs and exact-head CI reported.
3. [x] **Port libpng.** Depends on task 2 and its source mirror. Add the pinned
   recipe with explicit zlib input, matching configuration/header export and
   license staging; integrate the development prefixes and build order.
   Completion: the static target archive builds against exported zlib/SDK,
   unresolved symbols/configuration are inspected, and dependency/integration
   PRs are published. A successful build alone is not PNG runtime qualification.
4. [ ] **Implement native screen capture.** In Pyxis, add the separate resource,
   public request/result and asynchronous presenter handoff, owned FILE result,
   cancellation/failure cleanup and libpyxis helper in userland. This can use
   the current presenter without depending on the space-layer changes.
   Completion: ordinary kernel/runtime builds and interactive QEMU/debugger
   inspection establish authority refusal, bytes/layout, BUSY, snapshot lifetime
   and request/FILE cleanup; publish the ABI/helper dependency PRs in merge order.
5. [ ] **Delegate capture explicitly.** Depends on task 4. Add the boot setting
   and chosen packaged policy; preserve the optional resource through init,
   session, remote service and shell handoffs. Completion: interactive local and
   remote launches distinguish granted and ungranted callers while DRAW-only
   applications retain their existing behavior. Update init/resource references.
6. [ ] **Add and package the PNG command.** Depends on tasks 3–5. Add row-wise
   encoding, required output path, exclusive sibling staging and rename, checked
   cleanup/error reporting, normal
   userland/image integration and command usage. Completion: the ordinary image
   produces a host-decodable PNG and the existing `xfer send` workflow downloads
   it; existing destinations and failed writes behave as documented.
7. [ ] **Qualify and close the milestone.** Compare static QEMU PNGs against
   monitor `screendump` on boot, Bochs and VirtIO displays. Check navigation,
   TTY cursor, graphics, switched spaces, resize/snapshot retention, absent
   authority, concurrent capture and output refusal. Recheck the chosen visible
   layer after the space-layer milestone lands. Use matched ordinary boots to
   record presenter cost with capture idle/active, encoding time and file size;
   distinguish nested-VM observations from owner hardware results. Ask the owner
   for a ThinkPad boot-framebuffer capture and host download. Completion requires
   that native result or an explicitly accepted qualification limit; then move
   this document into reference docs, update inbound links and carry accepted
   remaining limits into technical debt.

## Handoff

The owner accepted all contracts on 2026-10-08 after PR #501 merged. Tasks 2
and 3 are complete. Task 3 uses Pyxis branch `ports/libpng` and these published
dependencies, which must merge before its parent integration:

- [userland PR #155](https://git.internal/PyxisOS/pyxis-userland/pulls/155),
  `47308da28c04c2e71a460bacec5767471479a3dd` (`libc/libpng-modf`): the unmodified
  musl `modf` prerequisite, public declaration and runtime inclusion.
- [ports PR #57](https://git.internal/PyxisOS/pyxis-ports/pulls/57),
  `1064c452a0236040c7d673ab51dbea3a5b81ff80` (`library/libpng`): the pinned
  library, generated configuration, explicit zlib input and staging exports.

The owner's shared `raw-sourceforge` mirror supplies the accepted libpng
archive/hash. Current LLVM 23.1.3 SDK, zlib and libpng builds passed; generated
configuration and symbol inspection preserve conventional read/write, stdio,
setjmp and floating-point support while omitting the simplified API and
architecture acceleration. Every libpng external reference is supplied by
zlib/libc. Public source headers and licenses remain unchanged, and the exported
configuration matches the archive build. Ordinary image/bundle staging and
exact-head existing CI results are recorded in the parent PR. Dependency
repositories report zero Actions tasks, so they provide no CI pass evidence.

The owner comment on merged PR #504 is incorporated into task 6's agreed
contract: exclusive sibling staging and rename, following cp's held-directory
and failure-ownership rules, with no direct-write fallback. Task 4, native
capture, is next after owner authorization. No screenshot implementation or
PNG runtime qualification has begun. No QEMU, debugger or build process remains
active.
