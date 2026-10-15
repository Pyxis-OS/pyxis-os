# Installer task 5 scratch qualification — 2026-10-13

Captured before task-5 code. Source is current main `c7a914e7`; its tracked tree
matches CI head `e3d3d696` exactly. Used that successful CI run [#2195](https://git.internal/PyxisOS/pyxis-os/actions/runs/2195)
image, with userland `fcb0c0e`, ports `4ed452df`, fs `414be0ac`. No kernel,
application, ABI or payload modification. Only the disposable live-medium menu
selects the existing `init=boot://init-install.pxe boot.install=1` entry directly.

Kernel SHA-256:
`9bc85d67409d6b6aeace60f8ee843e2b5b3f9276ff46b94dd62486a650b44c2b`.
Archive SHA-256:
`5a78d918395451b4a1998b1118357a5d47a96a850b5b75d20615e6fb9db39cac`.

QEMU 10.2.2, Q35/nested KVM, `-cpu max`, four vCPUs, 2 GiB RAM, UTC RTC,
fresh OVMF variables, modern VirtIO block, 512-byte sectors, writeback target,
VirtIO entropy, no NIC/USB/HDA. Booted from a disposable FAT live-medium image
containing the exact CI kernel/archive, avoiding the host's known AHCI CD-ROM
path. The target is a fresh 2 GiB raw file; Install chooses Read the room and the
12 MiB default journal. No native drive is attached or staged. No owned build
runs during measurements; shared-host load is uncontrolled.

## Results and timing boundaries

| Flow | Observed apply wall time | Whole-QEMU CPU | Result |
| --- | --- | --- | --- |
| Install, uninstrumented repeat | Between 122.6 and 174.3 s | Between 94.1 and 130.0 s through those observations | Success; complete copy/verification and EFI 5/5. |
| Update on the installed same-revision fixture | 270.588 s | 213.46 s | Success; full program stage and whole-ESP replacement. |
| Update before ESP writer entry | 242.336 s | 179.33 s | Includes initial revalidation and clearing the existing revision directory. |
| Update ESP writer entry to restart offer | 28.252 s | 34.13 s | Replacement, flush/release, raw EFI verification and final cleanup checks. |

Install is a manual observation bracket: at 122.6 s verification was still
running; success was visible at 174.3 s. The CPU bracket also includes any idle
time before the success observation, so it is not an exact apply CPU value.
Update uses observational hardware breakpoints at confirmed executor entry,
ESP writer entry and restart offer, filtered to the installer's address space.
Target function calls and memory writes are disabled. An ordered ELF relink's
LOAD bytes match the actual CI P1F byte-for-byte; no relinked executable is run.
`/proc` counters include all QEMU vCPU/device-emulation work, not just guest BSP.
These are single samples, not a performance comparison or native-media estimate.

An initial debugger-attached Install failed after 1,243 of 2,229 files with a
VirtIO request timeout and an incomplete target. Its timing is excluded; cause
is not established. The uninstrumented fresh-target repeat succeeds. No driver
fix or behaviour change belongs to this proposal baseline.

Both successful paths copy/verify 2,229 regular files / 46,730,754 payload bytes,
publish a 393,860-byte program manifest, then pass EFI 5/5 raw path/payload
checks (43,909,120 bytes). Programs are verified through the filesystem view and
may be cached; EFI reads follow raw flush/release. This is no power-loss proof.

## Behaviour before preservation

After successful Install and native clean shutdown in QEMU, a host preparation
adds a foreign `EFI/fedora/foreign.txt` sentinel and an owner comment to the
installed Limine file. Update offers the target, reports unknown/rebuildable
prior history, clears its existing 2,353 program entries and rewrites the ESP.
After success and clean shutdown, host inspection confirms **both the foreign
file/directory and the comment are gone**. This demonstrates today's destructive
behaviour only, not a real Fedora boot or coexistence qualification. The original
pool/kernel revision is known from the fixture; unknown history reported by the
current inspector remains unknown rather than being relabelled as a manifest-
backed Update measurement. Pool preservation and the still-active pool boot
configuration direction are separately established by current source inspection.

Native ThinkPad timing is **pending**, requiring an owner-present, explicitly
approved spare writable target. Today's destructive Update must never baseline
the internal Fedora ESP. The task-5 proposal records that native baseline and
later preservation/activation gates. All owned QEMU/GDB processes are stopped.
Raw images, screenshots, debugger scripts and logs remain local build artifacts.


## Buildable-scope checkpoint

[Userland #253](https://git.internal/PyxisOS/pyxis-userland/pulls/253) and
[parent #797](https://git.internal/PyxisOS/pyxis-os/pulls/797) implement frozen
reconciliation, private unlinked READ-only RAM FILE handoff, boot-bound complete
records and incremental owned-file publication behind
`INSTALLER_PUBLICATION_SCRATCH=1`. Default installed startup and Install/Update
remain unchanged. No native media was attached or written. Both PRs remain
draft; task 5 remains unchecked.

The current source consumes shared stager/boot-entry helpers from
[userland #272](https://git.internal/PyxisOS/pyxis-userland/pulls/272) at `1c8d49b6`
([Pyxis #880](https://git.internal/PyxisOS/pyxis-os/pulls/880)), on userland main
`1c8d49b6`. Task-5 userland is `34421344`; fs is `22eaf447`, with the
[read-cost fix](../esp-verification-cost/README.md). The caller retains staged
objects through publication and closes them before mount retirement. Stage
already synchronizes/reopens/verifies; both final interval checks remain.
CHECK runs once, immediately after BEGIN. No lease extension or omitted hash.
The cleanup-only follow-up propagates an absent/stale CLOSE bookkeeping error
without replacing an earlier error; all owned closes are attempted and the result
is consumed. Focused default/scratch builds and source review pass. Runtime
evidence below retains its exact original revisions.
Canonical parent verification now requires ENUMERATE on the scratch caller's
root and traversal handles; its config executable links the shared child-open
helper. The focused current caller stages through ordinary canonical parents;
orderly interruption leaves only the unreferenced attempt, unchanged foreign
EFI/menu bytes and a clean FAT/GPT check. It does not qualify final activation.

Earlier smaller-image checks, at their original revisions, established:

| Check | Evidence |
| --- | --- |
| No history/catalog/record | Review offers trusted defaults without prior `config` use; Cancel leaves the whole image unchanged. |
| Unsupported handwritten Lua | Whole-archive selection and Cancel preserve exact source bytes; literal admission never executes it. |
| Stale Review inputs | Host-changed disposable input refuses before staging; no additional image changes. |
| Private FILE handoff | At `d1eb451`, all three unlinked exact-READ native FILEs were independently reread and compared. |
| Readiness selection | Prepared/mismatched-boot logical fixtures select archive and refuse apply; a complete matching committed fixture selects ESP. These are host-built logical fixtures, not completed publication. |
| Earlier small publication | At `b3a18fc`, real 1,510,032-byte kernel and 776,828-byte rescue archive stage and verify, but the old FAT32 implementation times out during interval revalidation. Foreign bytes/menu remain unchanged. |

Raw images/captures/debugger logs stay outside the repository. Earlier logical
fixtures and interrupted staging do not qualify durability or activation.

## Current functional refresh — 2026-10-15

Parent image `09dec965`, userland `0b2606d5`, fs `22eaf447`; LLVM builder
`df87791`, matching SDK/ports exports from the read-cost CI source. Full scratch
image and focused installer/config builds pass. Changed installer/config code
has no warnings; the full build retains existing httpfs enum-comparison and
fat32-inspect switch warnings. Default and scratch focused builds also pass.

Nested QEMU 10.2.2, Q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF, standard VGA,
modern VirtIO block, 512-byte sectors. A disposable 768 MiB GPT disk has a
512 MiB mirrored FAT32 ESP / 4 KiB clusters. Its exact legacy normal/rescue menu
is disk-GUID-bound to a real old kernel/rescue pair; a foreign EFI sentinel and
Fedora menu block remain outside Pyxis ownership. This is a synthetic layout,
not Fedora boot qualification.

The extracted read-only certifier admits the legacy pair; Review pre-selects
legacy adoption, prints "owned switch last", and offers revision defaults
without prior configuration. Cancel exits 0 and the complete disk hash remains
identical. Manual ESP-GUID rendering is source-reviewed; coexistence integration
owns its runtime qualification. The pure planner adds no activation authority.

Kernel SHA-256:
`625bcfe08af3c464fb6f3e9a2614cd5f34054c9b017ebcd818153e9243cf794a`;
archive:
`327899b64822a13a0ecff29e3e4bc4cdb50753f5b6ed300aac96e3f89699260f`.

## Full-payload verification cost gate — 2026-10-15

The [matched read-cost record](../esp-verification-cost/README.md) shows
40,375,612-byte verification improving from 410.058 seconds to 12.935–13.207
seconds, all hashes matching. Reporting never paced the work. However, the
actual two-verification publication interval still lacks nested headroom.

A boundary-only, never-merged clock probe on parent `36d3f5fa`, userland
`96d69335` and fs `22eaf447` stages/syncs/verifies the full 5,930,128-byte kernel,
40,492,204-byte rescue archive and revision, then runs both final checks. CHECK
finishes at 0.175 s from before BEGIN; checks take 14.470 and 14.510 s. END is
at **31.839 s**. The post-verification guard returns TIMED_OUT before the Limine
switch: readiness is committed, boot activation pending, staged/record evidence
retained. There is no rollback/retry/reboot offer. Limine and foreign EFI bytes
compare unchanged. After normal guest power-off, all payload hashes match;
937 regular rescue files match source bytes; GPT and read-only `fsck.fat -n`
pass with a clean dirty mark. This is staging plus bounded refusal, not a
successful complete publication/activation result.

A separate earlier attempt hit a VirtIO 5-second FLUSH timeout after 146,880
kernel bytes, before BEGIN. Read-only GDB inspection found one uncompleted
FLUSH; no interval cost is claimed for it. The timing above uses a fresh fixture,
not automatic replay of the failed attempt. Staging remains slow before the
interval; the read fix claims no write or full-Update speedup. Native timing
requires an owner-approved scratch target and writer admission, even though
the read/hash probe itself makes no mutation. Neither internal NVMe nor a real
coexistence ESP is authorized.

## Remaining gates

Successful owned-entry activation, previous-pair cleanup, actual startup from a
committed boot-bound set, representative occupied/larger ESP lease headroom and
Daybreak's adversarial pass remain before release. The 30-second limit and all
checks remain unchanged. Native/installed activation still needs the owner's go;
internal coexistence additionally needs its accepted authority/qualification.

Classic Install/Update retain their fixed boot paths until task 5's committed
binding is delivered (owner decision relayed by epsilon, 2026-10-15). Coexistence
uses immutable attempts and manual entry text. Shared stager ownership evidence
is not a second active-binding record or automatic menu ownership.

Merge shared userland #272 then Pyxis #880, and fs #39 then Pyxis #882; task-5
userland #253 then Pyxis #797 follow only after their remaining gates. All owned
QEMU/debugger jobs are stopped at this checkpoint.

## Slow clean-control follow-up — 2026-10-15

Daybreak's clean-control screenshot reports status **17 (TIMED_OUT)** in phase 3
(EXCLUSIVE), before confirmed readiness. Its serial log completes payload hashes
and releases the interval; there is no evidence of INPUT_LOST. The booted
`stager-kernel.elf` and `fixed-image/boot/caelum.elf` both hash to
`bfe59107ae05e537a7858da7c991897c1b63f43858554350b9ce790c1b2481ed`,
the saved pre-epoch baseline. Thus its roughly 25-minute total and 98 KiB/s archive
hash rate are not post-#882 results. New installer objects do not update the
kernel filesystem implementation in an already assembled ISO.

An ordinary current caller at `d5710c74`, kernel `b1c3d87a` / fs `22eaf447`,
uses the earlier saved live root and a fresh qcow2 overlay over the same normal
768 MiB GPT / 512 MiB FAT32 fixture. Q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF,
modern VirtIO block and 512-byte sectors. There are no new guest timing probes or
owned parallel builds. At a manual observation 387 seconds after Review
confirmation, the 5,931,552-byte kernel has completed and archive copying has
reached 19,559,520 of 40,492,540 bytes, still **before BEGIN**. The run is stopped
by ordinary power-off at a partial archive; no full-run elapsed time, activation
or per-component timing attribution is claimed. Foreign EFI and Limine bytes
remain unchanged; GPT and read-only FAT checks pass. qcow2 and shared-host costs can
inflate this nested result; it is not a native throughput measurement.

In the pre-fix control, source inspection establishes separate write costs:
every bounded append calls
`file_update`, whose `validate_object` performs two full existing-chain walks.
Unlike regular reads, mutation contexts start with eight-sector FAT windows.
`run_plan` submits individual sector writes and flushes five non-mark phases per
call, including empty phases. A 40.4 MB archive at the 4,080-byte transport maximum
therefore needs about 10,000 append calls and 50,000 phase flushes. These counts
are source-derived, not measured attribution. Increasing read-window batching
would not remove the quadratic append validation; contiguous regular-file read
batching is already implemented.

The [two write-cost decisions](../../../wip/installer-tui.md#write-cost-decisions--accepted-2026-10-15)
were subsequently accepted by the owner. Their implementation and matched
qualification are recorded below. The existing 30-second interval, both
final payload checks and uncertainty handling remain unchanged. The earlier
31.839-second complete-interval result remains an independent open gate.

## Append-proof and phase-batching comparison — 2026-10-15

The owner accepted both write-cost defaults. The focused implementation is
[fs #41](https://git.internal/PyxisOS/pyxis-fs/pulls/41), `f58c0f9f`: separate
committed-append proof, unchanged read invalidation, complete validation before
pending mirror repair, bounded adjacent-sector writes within phases and no
empty-phase flushes. Native adapter failures invalidate both proof families;
WRITE entry preserves append eligibility for the engine. No deadline, authority,
durability barrier around actual writes or final payload check changes.

Matched controls use QEMU 10.2.2 nested KVM/Q35, `-cpu host`, four vCPUs, 2 GiB,
OVMF, standard boot framebuffer and modern VirtIO block with 512-byte sectors.
Both use a fresh raw `cache=none` clone of the same 768 MiB GPT image with a
512 MiB FAT32 ESP and 4 KiB clusters. The archive is exactly 40,494,204 bytes,
SHA-256 `9609e0eebfcfaa497c623bcd6196a10da623724f63490c7644fa80e561203c0a`.
The caller is userland `34421344` with the earlier boundary-only clock probe,
never merged; both images use its identical initrd. Compiler: LLVM `df87791`.

The completed control uses kernel `b1c3d87a`, fs `22eaf447`, a 5,931,552-byte
kernel, and the unmodified epoch-read engine. Confirm-to-installer-exit elapsed
is **789.620 s** (host UTC, including confirmation typing, Apply checks, staging,
verification, publication and cleanup; excludes boot/preparation/Review).
BEGIN-to-END is **35.547 s** using guest-clock boundary samples: CHECK 0.176 s,
first final verification 15.035 s, second 17.508 s. The unchanged 30-second guard
returns TIMED_OUT with committed readiness and activation pending; the boot entry
is not changed. This is a completed publication attempt with bounded refusal,
not successful activation. QMP whole-boot totals are 852,012 reads, 198,363 writes
and 57,077 flushes. Payload hashes, unchanged menu and synthetic foreign EFI
bytes match; GPT and read-only FAT checks pass after normal guest power-off.

The comparison kernel is the docs-only successor `f160e202` plus the native
adapter patch and fs `f58c0f9f`; its kernel/arch/include sources otherwise match
the control. It carries a 5,940,984-byte kernel; the archive and caller stay
identical. One comparison attempt timed out on a VirtIO FLUSH after staging
31,656,720 archive bytes, before BEGIN. Read-only GDB found 595,926 published
requests and 595,925 completions, retained opcode 4/FLUSH and the stopped queue.
No filesystem cause is established by that snapshot. An earlier qcow2 control
also timed out, so it is excluded from the raw matched pair. Failed mounts are
never retried or reset; images and queue evidence remain owner-local.

A fresh identical comparison completed the entire attempt with no transport
error. It waited at Review during the coordinated functional/build slot; QMP
confirmed zero target writes before confirmation. Both timed runs had no owned
build or second owned QEMU. All figures below are completed observations, not
extrapolation from partial staging.

| Observation | Control: fs `22eaf447` | Append/batching: fs `f58c0f9f` |
| --- | ---: | ---: |
| Confirm-to-installer-exit, host seconds | 789.620 | 615.844 |
| Before BEGIN through after END, guest seconds | 35.547 | 43.645 |
| CHECK, guest seconds | 0.176 | 0.191 |
| First final payload verification, guest seconds | 15.035 | 22.347 |
| Second final payload verification, guest seconds | 17.508 | 17.660 |
| QMP read requests, whole boot | 852,012 | 658,093 |
| QMP read bytes, whole boot | 1,539,667,968 | 744,843,264 |
| QMP write requests, whole boot | 198,363 | 79,817 |
| QMP write bytes, whole boot | 101,561,856 | 101,585,920 |
| QMP flush requests, whole boot | 57,077 | 45,595 |

Apply elapsed falls **22.0%**, read requests 22.8%, write requests 59.8% and flush
requests 20.1%. Read bytes fall 51.6%; the small write-byte increase includes the
larger comparison kernel and allocation slack. QMP counts include firmware,
read-only planning, staging, verification and publication; they are not separate
phase attribution. This is one completed nested pair, not a native or statistical
latency guarantee. Verification times vary even though the regular-read engine
is unchanged; this pair does not establish their cause.

**There is no 30-second interval margin.** The optimized attempt spends 40.006 s
in its two final verifications alone and ends at 43.645 s, approximately 13.64 s
past the deadline. Both attempts return TIMED_OUT with committed readiness,
activation pending and the original menu intact. Prefix-proof and phase batching
improve staging/I/O counts but do not clear the final verification cost gate.
No deadline renewal, omitted final check or automatic retry is used.

After normal power-off, all three staged payload hashes and all four selected
configuration hashes match their records. Both extracted archives are
byte-identical; each kernel matches its own source hash. The synthetic
`EFI/fedora/foreign.txt` and the complete original Limine menu compare unchanged.
GPT and read-only `fsck.fat -n` pass with clean marks on both completed images.
The fixture contains a foreign marker, not a bootable Fedora installation.
Previous-pair cleanup, actual boot-entry activation and native/coexistence
qualification remain open. Source review confirms successful warm appends avoid
the growing selected-file FAT walk; per-call geometry/parent checks and chain
storage reservation still remain. No new probe or diagnostic is merged.
Owner-local artifacts: `/shared/present/esp-stager-cost/append/`.

## Remaining write amplification — 2026-10-15

An unchanged-image attribution run completes in **612.431 s**, reproducing the
615.844 s result. Kernel, caller, archive, raw fixture, compiler and QEMU setup
are the comparison inputs above. There are no product-code changes. Existing
host `perf` probes record QEMU's VirtIO read/write submissions and completions
and `blk_aio_flush`; serial progress boundaries have QMP counter snapshots.
All **1,538,222 events / 769,111 requests** pair, with no overlapping requests,
missing completions, out-of-order events or failed completions. Whole-boot
read/write/flush counts and bytes equal the previous comparison exactly.
Boundaries are aligned by completed request counts, not by assuming the perf
and Python clocks share an epoch. Phase times below use the trace event clock;
serial boundary resolution is about 10 ms. Tracing is observation overhead,
not a new uninstrumented performance comparison.

| Complete Apply component | Observed seconds |
| --- | ---: |
| Kernel payload writes, 5,940,984 bytes | 88.776 |
| Archive writes, 40,494,204 bytes | 468.178 |
| Initial staged-payload verification | 13.680 |
| Exclusive BEGIN through END | 32.818 |
| Other checks, small/configuration work and cleanup | 8.978 |
| **Complete Apply** | **612.431** |

The interval includes 0.183 s CHECK and **14.896 / 14.875 s** final verification
passes. Each pass makes about 14,845 backing reads; neither writes nor flushes.
Their QMP backing-read service sums are 1.931 / 1.907 s. The remaining elapsed
includes caller hashing/transport, dispatch and scheduling; it is not all disk
service. The unchanged 30 s guard again returns TIMED_OUT with committed
readiness pending and the original boot menu selected.

Archive staging alone has **510,741 reads / 403,798,528 bytes**, **69,484 writes /
88,568,320 bytes**, and **39,666 flushes**. The phase breakdown is:

| Archive phase | Elapsed s | Writes | Written bytes | Write service s | Flushes | Flush service s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Caller, validation, allocation and image preparation | 289.903 | — | — | — | — | — |
| DATA | 42.581 | 19,774 | 63,116,800 | 5.550 | 9,926 | 28.227 |
| FAT, both copies | 48.424 | 19,932 | 10,205,184 | 6.923 | 9,887 | 26.498 |
| DIRECTORY | 39.551 | 9,926 | 5,082,112 | 3.691 | 9,926 | 25.819 |
| FSINFO, both copies | 47.700 | 19,852 | 10,164,224 | 6.810 | 9,926 | 26.003 |
| Explicit file sync | <0.001 | 0 | 0 | 0 | 1 | <0.001 |

A phase spans its ordered writes through its flush completion; gaps between
phases of one mutation belong to the following phase. The preparation row
includes inter-call work and all reads before DATA. It is **not separately
measured allocation CPU time**. Of its 70.863 s request service, directory
validation reads account for 47.586 s / 347,410 requests; FAT reads 7.071 s /
49,670 requests; geometry/FSInfo reads 10.220 s / 69,482 requests; existing-data
reads for partial sectors/slack 5.986 s / 44,179 requests. The remainder of
preparation is between requests. No CPU-cycle attribution is claimed: this
nested host has no supported cycles event. A separate debugger sample confirms
warm append reuse and roughly 50 reads per append, but its elapsed timings are
excluded because breakpoint overhead is substantial.

FAT1 and FAT2 each receive 9,966 one-sector writes: service 3.779 / 3.144 s.
All directory and FSInfo writes are also 512 bytes. DATA requests comprise
11,065 at 4096 bytes, 1,248 each at 512/1024/1536/2048/2560/3072 bytes, and 1,221 at
3584 bytes. Overall, **50,958 of the archive's writes are only 512 bytes**; none
exceeds 4096 bytes despite the device's 65,536-byte transfer capability. Flush
request-to-completion service averages 2.686 ms, maximum 128.722 ms. Every
request is serial: total service is 200.383 s and between-request gaps 267.775 s.
These are elapsed waits/gaps, not BSP CPU utilization. QMP separately attributes
72.601 s reads, 23.342 s writes and 107.047 s flushes; its accounting boundaries
and clock differ slightly from the VirtIO trace probes.

The code and request counts explain the amplification:

- `FILE_WRITE` admits **4080 bytes**, not 3 KiB. The twelve written files make
  11,393 mutations; 11,348 allocate a cluster. DATA, DIRECTORY and FSInfo flush
  per mutation, FAT when allocating: **45,527 of 45,595 whole-boot flushes**.
  The archive alone makes 9,926 mutations / 9,887 allocations. The trace confirms
  exactly 39,665 mutation flushes plus one explicit sync.
- Free search retains `next_free`; it does not restart at cluster 2. Dirty MARK
  is once per dirty tenure, not per cluster. Prepared FAT images deduplicate
  within a call, but the same FAT sector is rewritten on successive calls;
  both FSInfo sectors are reread/rewritten even without allocation. This is
  linear amplification, not a surviving growing-file disk-chain walk.
- Every append still revalidates geometry, parent directories and the canonical
  entry. Directory collision checking is quadratic in directory entries, which
  stay fixed during this archive copy. No cross-call directory proof is assumed.
- The native adapter grows chain capacity by exactly one cluster and copies the
  whole vector. For 9,887 archive clusters that is **195,485,764 copied bytes /
  9,887 allocations**: genuine quadratic CPU work, although not disk I/O.
- New-cluster zeroing, old-slack zeroing and 4080/4096 misalignment produce the
  DATA amplification. Record order splits new-cluster and old-tail runs; the
  current pack buffer and native write adapter cap each write at 4 KiB.

Relevant sources: [FILE bounds](../../../../include/abi/file.h),
[native adapter](../../../../kernel/fs/fat32.c),
[mutation planning](../../../../fs/fat32/mounted.c),
[directory validation](../../../../fs/fat32/directory.c), and
[payload copy loop](../../../../userspace/installer/esp_payload.c).
The [accepted bulk-transfer decision](../../../wip/installer-tui.md#bulk-transfer-decision--accepted-2026-10-15)
addresses mutation count and verification transport together; its measured
implementation follows below. The deadline and both final hashes remain required.
Raw traces, parser, counter snapshots and discarded debugger sample remain owner-local in
`/shared/present/esp-stager-cost/append/`; they are not a benchmark framework.

## Bulk-transfer AFTER comparison — 2026-10-15

Kernel `fdf381b8`, fs `25337999` ([fs #42](https://git.internal/PyxisOS/pyxis-fs/pulls/42))
and userland `326bc411` implement the accepted decision: private RAM FILE transfers
of up to 1 MiB, one mounted mutation per range, geometric chain storage, indexed
preparation and adjacent-sector runs up to the device's 64-KiB bound. Final
verification reads use the same bounded transfer path. Validation/proof lifetime,
effectful phase barriers, the 30-second guard and both final hashes remain.

The profiled AFTER run uses the same nested QEMU/VirtIO target setup, 512-byte
sectors and fresh fixture as the attribution run above. The 40,494,204-byte archive
remains byte-identical, with the SHA-256 recorded above. The kernel grows from
5,940,984 to 6,128,464 bytes. These are completed shared-host observations with
host tracing, not a native or statistical latency guarantee.

| Observation | Append/batching BEFORE | Bulk AFTER |
| --- | ---: | ---: |
| Complete Apply, host seconds | 612.430693 | 19.853673 |
| BEGIN through END, guest seconds | 32.818 | 7.165812 |
| First / second final verification, guest seconds | 14.896 / 14.875 | 1.896274 / 1.941048 |
| Target write requests, whole boot | 79,817 | 1,081 |
| Target written bytes, whole boot | 101,585,920 | 46,956,032 |
| Target flush requests, whole boot | 45,595 | 294 |
| Publication result | TIMED_OUT; activation pending | Both hashes pass; activated |

Observed Apply time falls 96.8% (30.85×); the AFTER interval has 22.834 seconds
of arithmetic margin against 30 seconds. Whole-boot target reads fall from
658,093 / 744,843,264 bytes to 40,825 / 293,603,328 bytes. Request counts include
admission, planning and cleanup, not only payload copy. The exact target write-size
histogram is:

| Request bytes | BEFORE count | AFTER count |
| --- | ---: | ---: |
| 512 | 58,563 | 256 |
| 1,024 | 1,446 | 13 |
| 1,536 | 1,440 | 87 |
| 2,048 | 1,440 | 0 |
| 2,560 | 1,440 | 0 |
| 3,072 | 1,424 | 0 |
| 3,584 | 1,381 | 0 |
| 4,096 | 12,683 | 12 |
| 16,384 | 0 | 1 |
| 36,864 | 0 | 1 |
| 45,056 | 0 | 1 |
| 65,536 | 0 | 710 |

The AFTER trace pairs all 27,806 requests after the 14,394-request seed snapshot,
matching the QMP delta, with no pending/unmatched requests, overlap or failed
completions. It contains every target write and 293 flushes; the other whole-boot
flush precedes tracing. The 46,956,032 written bytes are about 0.7% above the
46,632,145 logical bytes staged across the twelve files. That residual includes
metadata, slack and publication work; this measures device-visible logical
writes, not NAND write amplification or wear.

An AFTER repeat at final kernel `6f365d1a`, with the same fs/userland pins and no
host perf tracing, completes Apply in **22.217133933 s** and the interval in
**8.622249 s**, again passing both final hashes and activating. Whole-boot writes
remain exactly 1,081 / 46,956,032 bytes with 294 flushes. Only this final repeat
switches the source CD-ROM from AHCI to VirtIO SCSI to avoid the known host QEMU
AHCI segfault; the target remains VirtIO block. Its elapsed time is therefore a
functional repeat, not a matched transport timing comparison.

Offline inspection of the profiled AFTER image confirms staged kernel/archive
hashes, GPT and read-only FAT checks, unchanged original boot payloads and foreign
EFI sentinel, and preservation of the original menu prefix/foreign stanza. The
owned menu entries now select the staged pair; the saved previous menu equals
the original. No native media or native timing is qualified here.
Local artifacts are `build/bulk-after/` and `build/bulk-final/`; owner-local trace,
`summary.json`, `trace-ops.json` and offline checks are under
`/shared/present/esp-stager-cost/bulk-after/`. The final repeat has QMP/boundary
logs but no separate host trace; the profiled histogram is not claimed for it.

## Native FILE range qualification — 2026-10-15

A temporary, never-merged manual client exercises the native API against fresh
operator-owned GPT/FAT32 clones in nested QEMU (Q35/KVM, four CPUs, 2 GiB,
VirtIO block, 64-KiB device maximum), at 512- and 4096-byte logical sectors.
The kernel is the focused transfer implementation rebased onto main at
`f1ab2a5d`; the SDK includes `baa7e7c`'s FILE_TRANSFER completion receipt admission.
The 4Kn client additionally uses the sector-aware mount validator, subsequently
aligned with epsilon's identical-condition `4468b5a` patch in userland #276.
No real disk, new grant, fault injection or merged test framework is involved.

Both geometries pass exact 1-MiB round trips with unaligned offsets, preserved
head/tail bytes and zero sparse RAM prefix; short and zero EOF; missing READ or
WRITE authority; unsupported backing and same-object refusal; and a raw request
above the 1-MiB bound. Refused operations leave independently checked destination
bytes unchanged. Independent completion passes in both directions after closing
the original source/destination handles immediately after accepted submission.
The RAM source is already unlinked; the completion is waited on, collected and
acknowledged before checking its result. Explicit CHECK, interval END, sync and
BUSY-until-drained retirement complete. A debugger sample after the 512-byte run
shows `first_job == last_job == NULL` and all four CPUs in the idle scheduler.

This check found and fixed the missing userspace completion-operation whitelist
entry. A preliminary client also had to wait for the ordinary asynchronous GPT
scan rather than treating PENDING inventory as a final identity mismatch; that
readiness change is only in the temporary client. It did not justify changing
media identity or admission policy. The first 4Kn scratch publisher used an old
512-byte-only MOUNT_OBSERVE validator and refused before effects; the focused
userland dependency now carries the shared 512/4096 geometry validation.

Raw client source, build provenance, logs and the debugger sample remain under
`/shared/present/esp-stager-cost/transfer-use/` and `transfer-use-4kn/`.
These runs do not exercise cancellation/CUT or process-exit races, injected I/O
uncertainty, physical media or power loss. Review of retained-reference cleanup
is separate evidence; Daybreak's release review remains required.

The full scratch caller also completes publication on a fresh synthetic 4Kn
GPT/FAT32 ESP after the shared geometry-validator correction. Kernel code is
`f1ab2a5d`, caller `01cb710` built with `INSTALLER_PUBLICATION_SCRATCH=1` and the
updated SDK; the later shared-constant cleanup changes neither condition nor
runtime behaviour. Both 6,158,968-byte kernel and 40,494,204-byte archive verify
in both final passes, committed record verification passes, END completes and
the installer exits zero. The Review explicitly reports manual boot-entry
adoption/activation pending: this fixture has only a synthetic foreign menu,
not an owned boot pair. No entry ownership is invented. This is a functional
run, concurrent with other work, with no matched timing claim. Its source,
image and log are under `/shared/present/esp-stager-cost/bulk-4kn/current/`.
