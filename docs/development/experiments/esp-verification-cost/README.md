# ESP payload verification cost

2026-10-15, owner-assigned performance fix. Disposable nested-QEMU media only;
installed activation and native writes remain gated. The FAT publication
interval is still 30 seconds, with no omitted CHECK or payload hashes.

## Cause and implemented contract

Each 3 KiB mounted FILE read previously reset its FAT windows, validated the
complete selected-file chain in the locator, then traversed it again to compare
canonical identity. Payload reads also submitted individual 512-byte backing
requests. Progress reporting was not pacing verification: its 250 ms gate
returns immediately, without sleeping. An initial attribution run spent
410.115 of 416.507 seconds in FILE reads, 1.130 hashing and 2.565 in progress.
That run included a brief parallel build and is not the matched baseline below.

The first change batches FAT reads through already-reserved 128-sector windows
and consecutive payload sectors through 4 KiB scratch. The owner then accepted
read-proof reuse on 2026-10-15: regular-file geometry, metadata and complete-chain
validation remain valid for an unchanged exclusively owned mount epoch. The
cold read validates and compares the canonical chain in one walk. Warm reads
index the retained chain directly; sequential reads never restart a FAT walk.
Every mutation attempt, CHECK, sync/seal, reclamation or error invalidates read
proof, including native authority/capacity/buffer refusals and queued failures.
Call-side failures only set an atomic flag; the filesystem worker consumes it
before the next operation. No allocator or VM ownership changes are involved.
Invalid allocation proof or epoch wrap disables reuse. Payload bytes are still
read and hashed. Silent in-session metadata corruption may remain undetected
until invalidation, CHECK or remount; this consequence is explicitly accepted.
The independent RO mount path is unchanged. See the
[native mount reference](../../../devices/fat32-mounts.md) and
[format contract](../../../../fs/docs/fat32-mounted.md).

## Matched read/hash measurements

QEMU 10.2.2, nested KVM, Q35, host CPU, four vCPUs, 2 GiB, OVMF, standard VGA,
modern VirtIO block. A 768 MiB GPT disk contains a 512 MiB mirrored FAT32 ESP,
512-byte sectors and 4 KiB clusters. The same already-staged `boot.arc` is
40,375,612 bytes, SHA-256
`150f50b6334d368274e531fd08373aa970d7216d5e3badc8c625f59e3b179205`.
The fixture is a clone of the verified staging attempt; only its dirty marker
was cleared offline on that clone to admit a fresh RW mount. Original artifacts
remain untouched. No payload writes occur during these probes.

A local, never-merged caller times FILE reads, SHA-256 updates and progress
advances. The unchanged 3,072-byte probe makes 13,144 reads per pass:

| Source / progress | Total (s) | FILE reads (s) | Hash (s) | Progress bucket (s) |
| --- | ---: | ---: | ---: | ---: |
| Baseline `b9b6bed7`, fs `101e1f68` / on | 410.058 | 404.430 | 0.985 | 2.321 |
| Batching only, fs `183a3675` / on | 165.215 | 158.433 | 1.256 | 2.476 |
| Batching only / off | 145.821 | 140.797 | 1.197 | 0.931 |
| Final `68c3a472`, fs `22eaf447` / on | 15.669 | 12.200 | 0.772 | 0.808 |
| Final / off | 14.978 | 11.887 | 0.726 | 0.586 |
| Final / on repeat | 15.311 | 12.006 | 0.738 | 0.759 |

All hashes match. The disabled progress bucket still measures probe clocks and
a branch. Do not attribute all variation to output: reporting is not the primary
cause. Final same-chunk progress-on verification improves 26.2–26.8 times over
the baseline. The probe's many clock calls add overhead; these are profiled,
shared-host nested samples, not unprofiled or native timings. No owned build or
debugger runs during timing; unrelated host/guest activity is uncontrolled.

A second local probe interleaves larger requests against the same final kernel
and payload, with progress enabled in every pass:

| Read size | Calls | Total (s) | FILE reads (s) |
| --- | ---: | ---: | ---: |
| 4,088 | 9,877 | 16.316 | 13.585 |
| 3,584 | 11,266 | 13.207 | 10.306 |
| 4,088 | 9,877 | 16.293 | 13.564 |
| 3,584 | 11,266 | 12.935 | 10.079 |

Every hash matches. The transport maximum straddles extra sectors; the shared
stager therefore rounds its maximum verification request down to the observed
device block size (512 gives 3,584). A block larger than the 4,088-byte transport
maximum keeps that maximum. Source checks use 4,088; copying caps at the
4,080-byte WRITE maximum. There is no ABI expansion. The aligned result is
31.0–31.7 times faster than the original 410.058-second run.

Baseline/final ELF SHA-256:
`e64deda8c7fbdd94d27c902abad8cdab0afcbaff275fab380be6c9ae0193611b` /
`cb0f01ab2d4691c6fc274461f844390643ce44f942c50a7130b4a1524e9b8617`.
Original/larger-read probe initrd SHA-256:
`c319faf011197cc885c5ae042ef4aa3b771125909ee05c068606f5c4a2d1e721` /
`cf824120de536dcad77220bd9135370a3fa6797573ae11a35206f3e5e9c43fa9`.
The complete disk remains byte-identical after every probe, SHA-256
`3f2e6bcd8312db1eb7740518bdf0fe951626c725c040e126410a3a1121490b3e`.
Probe source, patches and logs stay outside the repository.

## Publication headroom

One complete archive hash fits 30 seconds with about 17 seconds left in the
profiled nested run. **The full two-verification publisher does not yet fit.**
An actual scratch consumer (parent `36d3f5fa`, userland `96d69335`, fs `22eaf447`)
stages a 5,930,128-byte kernel, 40,492,204-byte rescue archive and revision, then
runs the unchanged BEGIN/CHECK, configuration revalidation, both payload checks,
record publication and owned-entry switch path. A never-merged boundary-only
clock probe observes:

| Boundary | Elapsed from before BEGIN (s) | Remaining interval (s) |
| --- | ---: | ---: |
| CHECK complete | 0.175 | 29.829 |
| First final verification starts / completes | 1.363 / 15.838 | 28.641 / 14.166 |
| Second starts / completes | 17.313 / 31.827 | 12.691 / -1.823 |
| END complete | 31.839 | -1.835 |

The final checks take 14.470 and 14.510 seconds. The post-verification guard
returns TIMED_OUT, retaining a committed readiness record and staged artifacts
but **not activating the boot entry**. Limine and the foreign EFI sentinel stay
byte-identical. After ordinary guest power-off, offline hashes match every
staged payload; all 937 regular rescue files match their supplied source bytes.
GPT validation and read-only `fsck.fat -n` pass, including a clean dirty mark.
This is successful staging and bounded refusal, not successful publication.
The guarded two-check path still lacks nested margin; its deadline and checks
are unchanged. Per-file namespace lookup validation remains, while held regular
READ/SIZE/INFO share proof; no per-chunk chain traversal remains.

An earlier fresh attempt stopped before BEGIN on a VirtIO 5-second FLUSH timeout
after 146,880 staged kernel bytes. Read-only debugger inspection saw 24,681
published requests and 24,680 completions; no interval timing is claimed for it.
A fresh fixture, not an automatic retry of that attempt, produced the result
above. Staging precedes the interval and remains slow in this nested profile;
this read fix does not claim faster writes or a full Update speedup.

The interval run uses the same nested device profile, with boundary logging
rather than per-read profiling. Kernel SHA-256:
`36d210fc0a228e07de6f96d5f96f4481ba0677f1d85826cd67a87f6c2742425c`;
local probe initrd:
`e20cddb1396b839250a674c655beb3dded50073ac3818c779e19b42d649711be`.
Native ThinkPad timing through the orchestrator is pending, requiring an
owner-approved writable scratch ESP: the optimized engine requires RW admission
and writer authority even when this hash-only caller makes no mutation. Admission
itself does not dirty the volume; the first mutation owns that mark. The unchanged
whole-image hashes above confirm hash-only probes made no media change. Internal
NVMe and the real coexistence ESP are not authorized targets.
All owned QEMU/debugger jobs are stopped at this measurement checkpoint.
