# Bounded bulk FILE reads through providers

Accepted by chronium (2026-10-25). The merge gate was lifted after the
0.0.9 release (`c02a2bfa`, 2026-10-26). New consumers require a kernel
providing the bulk operations; released small-read interfaces stay intact.

## Problem and release boundary

The [native SHA qualification](../development/experiments/sha256/README.md)
passed, but hashing a 281,031,168-byte HTTP archive still takes about 2.65 s
versus the helper's 1386 MiB/s native RAM FILE read-and-hash. The actual live
image puts `http-work` on **RAM-backed npfs**. Each small archive read crosses
httpfs, native completion admission and the npfs worker; physical RAM does not
make that file a native RAM FILE eligible for the existing export shortcut.

The public operations are additive development changes after the 0.0.9 release.
They preserve the released ordinary operations, but a new consumer cannot run
on an unmodified 0.0.8 or 0.0.9 kernel. Development builds use exact revision
provenance under the [decided ABI model](pkg-upgrade.md#decided); there is no
development ABI number, compatibility shim or silent small-read fallback.

## Where the cap comes from

- [FILE](../../include/abi/file.h) has `FILE_PAYLOAD_MAX = 4096`; its eight-byte
  count leaves **4088 data bytes**. `lib/file.c` clamps every ordinary read.
- [Endpoint](../../include/abi/endpoint.h) has `ENDPOINT_DATA_MAX = 4096` and a
  fixed packet. Kernel delivery records embed separate request/reply arrays;
  all sixteen slots reserve those arrays. Provider helpers also use full local
  packets. Larger replies are not a one-line FILE constant change.
- [Native completion](../../include/abi/completion.h) has its own 4096-byte
  payload contract. Ordinary npfs jobs use the 4096-byte disk-I/O buffer.
  Neither completion notification nor that job buffer can carry a bulk result.

The current 16 KiB archive chunk becomes four 4088-byte reads plus 32 bytes.
The fixture needs **85,764 FILE reads** assuming full successful replies:
17,152 full chunks plus a 12,800-byte tail. Source inspection establishes the
pipeline/counts, not percentages of CPU time. Progress does not sleep; member
processing follows the archive VERIFY span. Detailed local attribution:
`/shared/present/sha256/PKG-VERIFY-ATTRIBUTION.md`.

## Selected transport

Use npfs's existing bulk backend with **dedicated copied bulk replies**.
Keep ordinary endpoint packets/control traffic small; allocate out-of-line data
only for admitted bulk FILE reads. This is not general shared memory or a
blanket enlargement of IPC. It preserves some copies in exchange for a simpler
ownership contract and amortizes provider/completion work over larger ranges.

Existing `FILE_TRANSFER` is not a substitute: it requires destination WRITE,
accepts native RAM/initrd→FAT32 or FAT32→RAM, and rejects exported sources and
npfs pairs. It supplies no caller-memory read interface. Nor is switching HTTP
staging to plain RAM a drop-in workaround: its default 64 MiB aggregate storage
cap refuses this 268 MiB GET.

## Accepted contract

### Authority and data flow

A positioned bulk READ borrows a FILE with the same READ and, for exports,
CALL authority as ordinary READ. No service constructor, writable destination
FILE, native backing grant or new ambient authority is given to the client.

1. The caller supplies offset, capacity and an optional absolute deadline,
   plus a private output span in its own process. The kernel captures the
   request, reserves its slot and bulk budget before acceptance, and validates
   bounds/authority. The output address is transport-local; **the provider
   never receives it**, and no pointer becomes authority in a FILE payload.
2. An exported FILE delivers a small typed request and its owned reply receipt
   to the provider. The provider sees the admitted offset/capacity and actual
   invoking READ rights. Ordinary four-capability attachment limits remain;
   bulk reads/replies themselves carry no application capability attachments.
3. Httpfs submits one backing bulk read. Npfs owns a stable kernel data buffer
   through actual worker retirement, using its existing bounded device-sized
   runs, dirty-cache coherence and mapping checks. It uses distinct owned bulk
   storage, not an enlarged ordinary npfs job array. Completion delivers small
   status/count metadata;
   the **owned completion receipt** permits collecting the bulk result into
   provider-local memory. It gains only that result's collect authority, not
   another operation's data or a backing FILE. Ordinary completion receipts
   and payloads keep their current contract.
4. A dedicated bulk reply copies from provider-local memory into the admitted
   operation's kernel storage. Kernel native/NPFS reads use the same bounded
   result model directly. The final copy reaches the caller's private span
   under a fresh, short mapping lease. No caller mapping is pinned across the
   provider/filesystem wait; no large copy holds an endpoint spinlock or keeps
   interrupts disabled. Existing allocation/VM ownership rules still apply.

The borrowed output span must remain owned and reserved by the caller until
return; concurrent release/remapping is not supported. Kernel validation is
repeated at the final copy. No component can write it after return. The native
and exported library helper returns an exact validated count/extent, with the
same EOF/short-read rules as FILE_READ; the requested offset/capacity range
must not wrap.
No oversized automatic packet or hash buffer belongs on a stack. A bulk read
does not give a mutable native FILE stronger snapshot/concurrent-write semantics
than its backing already provides.

### Lifetime, cancellation and collection

The source capability's lineage, export, description and worker ownership stay
retained as required for the operation. Source-handle close, explicit CUT and
call cancellation keep their existing distinct meanings after admission.
Querying a handle is not proof it will
remain live. Preserve CUT, receiver closure, withdrawal and cancellation/result
race rules; an owned bulk buffer must not turn revoked source authority into a
new independent resource. Bytes already returned remain the caller's data.

A deadline is absolute and never renewed by internal chunks. Zero retains the
existing no-caller-deadline meaning; backing-service bounds still apply. If
cancellation/closure/expiry wins terminal selection, the external read returns
an error and no usable count. Buffer contents on an error are unspecified;
there is no promise to retract bytes a final copy had already written.
Native completion keeps its actual result and cancellation reason distinct;
httpfs/result selection must not falsify that native result.

Buffer and budget references survive any in-flight reply copy or worker even
when the caller exits, closes or times out. Reclaim only after those owners
drain, using an independently retained budget gate, never a dangling process
pointer. A provider retaining a canceled receipt alone must not retain a large
buffer once no copy/worker can use it. After that release, a stale receipt
rejects late reply/collection without reallocating bulk storage or re-admitting
I/O. No age-based reclamation is introduced.

Successful completion collection consumes its data authority. Bad/short
**completion collection** output leaves its terminal result available
on that receipt without rerunning I/O; close/acknowledge discards it. This does
not create a new client-side retry ticket for synchronous FILE calls: invalid
final client output fails/discards that result. Neither path silently replays
an operation. Provider exit/cancellation must drain or cancel its outstanding
backing work before releasing the borrowed data storage.

## Accepted decisions (chronium, 2026-10-25)

1. **Transport:** dedicated copied bulk FILE READ/reply and native
   completion collection as above. Keep ordinary 4096-byte messages unchanged;
   no shared mapping or backing-handle escape.
2. **Bounds and charging:** authored maximum **1 MiB per read** and
   **16 MiB of kernel bulk-data storage per initiating process**, separate from
   existing completion metadata. Charge every live allocation at its own layer:
   endpoint reply data to its caller, backing-completion data to its submitter.
   These can overlap and both count; no uncharged duplicated kernel buffer.
   Provider/client private buffers remain ordinary process memory. Reserve
   before delivery/I/O; budget exhaustion returns LIMIT, allocation failure
   NO_MEMORY, and full delivery slots QUEUE_FULL, without eviction or waiting
   for budget. Retain the charge through copy/worker drain and result release.
   The ceiling is an authored workload bound, not a guarantee of a full reply
   or a value inferred from FILE_TRANSFER's unrelated limit.
3. **First delivery:** native RAM/initrd/npfs plus httpfs completed
   FILE snapshots, and explicit SDK/libpyxis use in pkg's archive/member and
   installed-tree hash loops. This covers both live RAM-npfs and installed
   NVMe-npfs. Other providers may implement the typed operation; unsupported
   backings refuse it. Keep ordinary READ and existing consumers intact; do
   not silently change libc/Go/all ports or fall back after a bulk failure.
   Keep every index/archive/member hash, identity/size check, cancellation,
   publication and sync barrier. No kernel hashing, package format change or
   configuration/grant migration is part of this work.

## Implementation sequence

- [x] Kernel/ABI/SDK foundation: functional typed bulk calls/replies and receipt
  collection, charged ownership, native backends and bounded npfs buffers/runs.
- [x] Userland/libpyxis and httpfs: provider receive/reply and backing completion
  collection, including forwarded grants and joined cleanup. Dependency PRs
  precede parent pins; no placeholder interfaces or compatibility readers.
- [x] Pkg consumer and QEMU qualification, with implemented behavior in FILE,
  endpoint/completion and provider references.
- [x] Native live RAM-npfs functional qualification on vela (chronium, 2026-10-26).
- [ ] Separately approved owned NVMe-npfs native qualification.

## Qualification

Use the existing SHA packet/workload and ordinary QEMU/GDB checks; no new
benchmark framework. Compare A ordinary reads and B bulk reads on the **same
post-release kernel, runtime, SHA engine, grants and fixture bytes**. A/B/A/B
uses fresh package identities and checks both full archive and all member
hashes, extraction and final selected-set verification. Memory hashing stays a
control. Record actual read sizes/counts and backend runs; a full 1 MiB response
would reduce this fixture to 269 source requests, an arithmetic comparison,
not a measured speedup or promise of full responses.

QEMU covers native RAM/initrd, RAM-npfs and disk-backed npfs; exported and
forwarded handles; EOF/short replies; budget/queue refusal; source-handle close
after admission, timeout, CUT,
caller/provider exit and cancellation while work/copy is outstanding. Check
actual retirement and that output storage is untouched after return. Preserve
ordinary small FILE/endpoint/completion behavior and worker fairness; no new
long interrupt-off or locked copy interval is acceptable.

The live RAM-npfs functional check is recorded below. The remaining native
check needs a separately approved installed NVMe-npfs workspace and existing
grants, using the same 268 MiB/1242-file fixture.
NVMe gets the same request batching; physical reads, cache state and device
limits still apply. Reuse the existing target memory/FILE scan/read+hash
controls; separate native target timers from host Remote phase arrivals.
Record exact revisions, AC/HWP state, storage backend, cache conditioning,
request counts and variation. No predicted timing target replaces measurement,
and no raw device writes, destructive tests or native runs are authorized here.

## Foundation qualification (2026-10-25)

Four-vCPU nested KVM, `-cpu host,+invtsc`, q35, modern VirtIO storage/rng,
4 GiB RAM for the native probes. The final native probe passed 98 checks,
including 1 MiB RAM/initrd/RAM-npfs and persisted VirtIO-npfs reads, EOF/short
reads, argument/deadline refusal, ordinary READ, completion collection after
source close, short/invalid output retry, single-use collection, the 16 MiB
budget, exported and forwarded reads, CUT, and late reply/no writes after return.
A separate lifetime probe passed 80 checks for delivery saturation, budget
reclamation, receiver closure, cancellation, retained canceled receipts and
non-consuming wrong-reply refusal. These are QEMU results, not native hardware
qualification. Private sources and logs remain under
`/shared/present/bulk-file-reads/qualification/`; no probe or automation is merged.

Kernel and focused SDK/runtime builds use the existing Clang 23.1.3 builder
`pyxis-llvm23.1.3-3d69e83`. The ABI textual check preserves every released 0.0.8
header line. New consumers require a kernel providing bulk reads. No compiler rebuild,
new CI workflow or default-boot diagnostic is added.

The [matched QEMU record](../development/experiments/bulk-file-reads/README.md)
contains the A/B/A/B package results, controls and measurement limits. All four
archive/member/extraction/selected-set checks passed; the native RAM result is
recorded below and native NVMe-npfs remains open.


## Native live RAM-npfs (2026-10-26)

Chronium's vela run passed the A/B/A/B archive, 1242-member extraction and
selected-set checks on the XSAVE packet's shared kernel/runtime/provider.
Host-observed install spans were A 12.654/12.403 s and B 7.836/7.843 s,
from first output to typed completion; these are not target CPU/read timings.
[Native record](../development/experiments/bulk-file-reads/README.md#native-vela-2026-10-26).
Installed NVMe-npfs and the separate memory/FILE scan controls were not run.
