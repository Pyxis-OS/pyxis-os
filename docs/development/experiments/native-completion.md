# Bounded native completion qualification

2026-10-12. These are manual nested-QEMU results, not native performance or
power-loss evidence. Raw screenshots, fixture source and debugger captures remain
in ignored `build/`; no exercise program enters ordinary images.

## Inputs and configuration

Baseline captured before implementation: main `7db4cb31`, userland `2bc56bf4`.
Matched interleaved runs after main advanced: before `50140b52`/`06275d0`, after
`a3b6eaa`/`4fe7a585`. The after kernel code is `9e4d815c`; subsequent parent
commits merge main and document it. Ports `fd1eb30f`, fs `28386cc7`, lwIP
`a1aadb91`; existing LLVM builder `2d23443`. Ordinary kernel, SDK and image builds
passed. The endpoint ABI and helpers change together, so this is an integration
comparison, with separately rebuilt matching SDK/ports/userland outputs.
A latest-main refresh compares `36bcd7e9`/`3daa24611` with
`0009e50a`/`62da3399`, both with fs `30f625a2`; ordinary kernel/SDK/image
builds passed again. The older comparison remains separately identified below.
No compiler-container rebuild is needed.

Q35, nested KVM, `-cpu max`, four CPUs/one socket/four cores/one thread, 2 GiB,
UTC RTC, standard VGA, raw OVMF CODE/VARS, modern VirtIO network/RNG and user
networking. Info logging, profiling off; both kernel configurations have identical
content. Unrelated guests remained running. One measurement guest alternated
fresh baseline/implementation boots for each workload; no debugger or task build
ran during timing. Local fixtures use host `127.0.0.1:18085` UDP echo and `18086`
TCP listener, reached as `10.0.2.2`; no credential or external service.

## Functional results

A temporary native consumer submits copied payloads, waits on receiver plus
console, prints native/transport results and explicitly closes completion receipts.
Commands were `session boot://completion.pxe tmp`, `net`, `full`, `exit` and
`wait`; this local-only name is not an installed command.

| Case | Observed result |
| --- | --- |
| RAM CREATE, WRITE/READ, RESIZE/SIZE, REMOVE | Attached FILE grant; `abc123`, count 6; size 2; held file still readable after unlink; new lookup NOT_FOUND |
| Second valid submission, same or other receiver | `CALL_BUSY` (9), no hang/crash; remains BUSY after collection until receipt close; next submission succeeds after ACK |
| Short/invalid completion output | BUFFER_TOO_SMALL (21) / BAD_BUFFER (5); retry receives the original successful WRITE, known count 6 |
| Correlation and cancellation | Reused ID BAD_REQUEST (4); terminal CANCEL succeeds without collection; acknowledged ID NOT_FOUND (12) |
| Full sixteen-record ordinary SEND queue | QUEUE_FULL (7); failed admission releases the process gate, allowing another receiver to submit |
| Read-only WRITE / unsupported SYNC | DENIED (2) / BAD_OPERATION (3), before admission |
| TCP CONNECT | Successful native result and attached TCP_RIGHTS; native INSPECT succeeds |
| UDP SEND/RECEIVE | Echoed `ping`, known accepted/received count 4 |
| Pending cancellation | Repeated CANCEL succeeds; one ABANDONED (28) result, separate reason and NOT_STARTED where cancel wins admission/start race |
| Expired/elapsed deadline | Expired SUBMIT TIMED_OUT (17); accepted waiting receive ends with native timeout/reason 17 |
| Receiver close and replacement | Pending work drains, gate holds meanwhile; a subsequent operation on the other receiver succeeds |
| Mixed console/receiver wait | Physical key wakes input with receiver not ready; cancellation then produces one receiver completion and ACK |

Read-only GDB confirmed the mixed-wait receive was IO_WAITING, STARTED and gate
count 1 before the physical key. After receiver closure and process exit with a
pending receive, UDP slots were FREE with no completion pointers/waiters. The
process-exit exercise returns after a 100 ms sleep without cancel/collection;
slot drain was inspected afterwards. No claim of forced physical-I/O interruption
or an exhaustive scheduler race schedule follows from these checks.

Mounted npfs used a disposable 68 MiB GPT disk, partition 1 at sector 2049,
a 64 MiB pool/1 MiB journal, empty `system` volume, and explicitly writable
`data://` in a copied archive configuration. The same FILE/DIRECTORY and
admission/collection sequence passed. GDB found zero admitted requests, empty
queues/retirement list, healthy pool and EMPTY journal. Clean powerdown flushed;
stopped-image fsck passed. These checks used kernel `9e4d815c`, helper `94404a4`
before its conflict-free rebase; fresh-main `4fe7a585` RAM/admission checks passed
again after the rebase. The latest-main refresh (`0009e50a`, userland `62da3399`,
fs `30f625a2`) rebuilt the ordinary image and repeated the same mounted-npfs
sequence, including both-receiver BUSY and retryable collection. The pool again
drained healthy with an EMPTY journal and clean powerdown. Native hardware,
HOST/provider forwarding, physical I/O
failure and active-journal cancellation remain unrun; excluded backends refuse.

## Costs and performance history

Existing commands, five verified rounds after an untimed warmup:

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 5
```

Repeat with `--size 4096`. RAM uses
`cp boot://share/iobench.bin tmp://completion-baseline.bin`, then
`iobench read tmp://completion-baseline.bin --buffer 4088 --rounds 5`.
Reboot between IPC commands. Milliseconds, median (range):

| Workload | Before code | Interleaved before | Interleaved after |
| --- | --- | --- | --- |
| CALL 64 completion | 103.510 (101.026–133.563) | 97.340 (83.266–104.134) | 98.932 (79.908–103.550) |
| CALL 4096 completion | 100.119 (87.331–111.645) | 100.227 (96.400–105.291) | 83.309 (81.060–86.854) |
| SEND 64 admission | 1.262 (1.221–1.331) | 1.195 (1.153–1.213) | 1.204 (1.110–1.508) |
| SEND 64 completion | 18.471 (14.453–19.518) | 16.166 (13.646–21.057) | 12.346 (10.692–14.793) |
| SEND 4096 admission | 1.303 (1.276–1.871) | 1.273 (1.263–1.327) | 1.288 (1.272–1.632) |
| SEND 4096 completion | 12.903 (11.988–17.283) | 15.618 (12.160–21.207) | 17.282 (15.417–18.196) |

Every warmup and timed pass verified 256 messages/payloads with zero rejection,
failed CALL or delivery. Admission medians rose under 1.2%; varied completion
results do not establish a general speedup. Clock calibration was not subtracted.

RAM verifies 1 MiB, 257 reads and EOF; all passes succeeded. Before code complete
median was 0.466 ms (0.430–0.481). Initial paired complete medians were 0.465 / 0.955
ms; after-clock calibration rose to 128.965 µs versus 36.371 before. A reverse-order
repeat resolved that concern but **retained a material measured shift**:

| Reverse RAM pair | Before | After |
| --- | --- | --- |
| Clock calibration, µs/read | 37.488 | 39.949 |
| Payload, ms | 0.355 (0.346–0.376) | 0.626 (0.615–0.658) |
| Complete, ms | 0.430 (0.421–0.460) | 0.938 (0.913–0.961) |

In that earlier integration comparison, payload rose 76%, complete 118%.
The cause remains unresolved; the latest-main repeat below does not reproduce it. Inspected ordinary
RAM read/copy, VM-buffer checks, SDK file_read/handle_query and syscall entry/exit
retain their instruction structure. Native dispatch adds one protocol comparison;
the generic SDK FILE adapter's frame grows 32 bytes for endpoint metadata. Neither
establishes the observed cost's cause. No cheaper-timekeeping task or speculative
optimization was added.

### Latest-main interleaved repeat

After the FAT32 merge and final userland rebase, the same commands ran on the
latest revisions above. The filesystem qualification guest had exited before
these timed runs; an earlier overlapping preliminary CALL pair was excluded.
Five timed samples per boot after an untimed warmup, A/B/B/A for RAM:

| Workload | Before | After |
| --- | --- | --- |
| CALL 64 completion, ms | 106.749 (92.101–125.177) | 104.496 (98.895–120.600) |
| SEND 64 admission, ms | 1.265 (1.209–1.455) | 1.298 (1.261–1.443) |
| SEND 64 completion, ms | 18.561 (16.084–21.891) | 15.948 (13.657–17.482) |
| RAM A/B payload, ms | 0.390 (0.383–0.552) | 0.354 (0.342–0.400) |
| RAM A/B complete, ms | 0.469 (0.456–0.730) | 0.433 (0.417–0.477) |
| RAM reverse B/A payload, ms | 0.367 (0.346–0.399) | 0.376 (0.365–0.407) |
| RAM reverse B/A complete, ms | 0.443 (0.424–0.542) | 0.453 (0.440–0.489) |

All 20 IPC and 20 RAM timed samples plus eight warmups verified. Latest SEND
admission median grew 2.6%, with overlapping ranges. RAM complete changed -7.7%
in the first pair and +2.2% in reverse order, also with overlapping ranges.
The earlier roughly twofold RAM shift is not reproduced on these submitted
heads. Its cause is not established; neither a general speedup nor native-host
performance follows from the latest timings.

Debugger type sizes: owned operation 8,392 bytes, gate 48, admission request 4,304,
npfs job 5,008 after the FAT32 merge (4,952 in the earlier comparison). Receiver records grow 16 bytes each. Backend/global slot limits
are unchanged; transient stack copies, allocator bookkeeping and retained resource
objects are separate. Resident operation storage is bounded per process; no
cumulative HTTP-body allowance is added by this prerequisite.
