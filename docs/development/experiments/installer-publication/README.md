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
