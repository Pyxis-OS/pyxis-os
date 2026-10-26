# Prepared HTTP transactions: qualification and costs

2026-10-12, nested KVM, Q35 `-cpu max -smp 4 -m 2G`, HPET, UTC RTC,
VirtIO net/user networking and entropy, no HDA. Disposable local fixtures at
`10.0.2.2:18080` and HTTPS `10.0.2.2.sslip.io:18443` with a temporary public CA;
no paid API or real credentials. These are QEMU results, not native measurements.

Baseline parent `4366eb28`, userland `21cf935c`; implementation kernel `62e57184`,
timed userland `705b148`; final qualification uses `c72971b` / parent
`98b51307` (#765). The later changes correct concurrent COMMIT observation and
validate STATUS fields, without changing the timed snapshot/direct-POST paths. Both use fs `30f625a2`, ports
`fd1eb30f`, lwIP `a1aadb91`, LLVM23.1.3-2d23443. Original pre-edit
`9bd3d66d`/`62da3399` measurements are retained locally; the table refreshes the
comparison after the managed-libc merge. No compiler-container rebuild.

## Nested-QEMU matched cost runs

Separate baseline/initial implementation/baseline/final implementation boots
(A1/B1/A2/B2), same configuration, fresh OVMF variables. No task-owned build or
GDB inspection during timing; other host workloads were not controlled. Each GET
and RAM row is three measured runs after warmup, byte verified; POST is three
runs, with the server independently checking 1 MiB length and SHA-256.

Commands: `iobench read http://10.0.2.2:18080/fixed --rounds 3`;
`cp boot://share/iobench.bin tmp://post-body` then
`iobench read tmp://post-body --rounds 3`; and three
`boot://httpget.pxe http://10.0.2.2:18080/echo --post boot://share/iobench.bin`
requests through the existing direct-libhttp exercise. Helpers/captures
stay local, outside ordinary images. Ranges are minimum–maximum, milliseconds.

| Workload | A1 baseline median (range) | A2 baseline median (range) | B2 final median (range) |
| --- | --- | --- | --- |
| Legacy GET OPEN | 181.085 (173.891–203.013) | 206.450 (201.070–222.588) | 205.986 (189.458–218.587) |
| Legacy GET whole 1 MiB | 201.135 (193.201–223.228) | 228.087 (222.600–242.751) | 391.975 (377.939–404.238) |
| Legacy FILE payload reads | 19.864 | 21.344 | 184.572 (184.120–187.094) |
| RAM 1 MiB | 0.454470 (0.449720–0.474210) | 0.451840 (0.436230–0.485500) | 0.436210 (0.435150–0.469330) |
| Direct fixed-file POST 1 MiB | 406.776 (363.155–414.899) | 371.750 (354.165–398.086) | 349.994 (337.492–377.185) |

B1 (`edb6541`) GET median was 607.908 ms (607.116–883.026): an unnecessary
zero-deadline WAIT_MANY poll submitted a BSP readiness request before every IPC,
including immediately after a wait woke. Direct TRY collection, one bounded
engine step and parking only when neither progresses restored OPEN time. Lost
wakeups remain covered by wait registration before the locked readiness scan.

Final legacy payload reading remains slower: B2 whole GET is 1.72 times A2,
2.551 MiB/s versus 4.384 MiB/s. RAM and direct POST overlap; clock controls are
about 35–42 microseconds/read. Source inspection shows readiness/BSP/timer work
when the next READ is not queued at TRY, replacing the old blocking RECEIVE loop;
this is not a measured per-function attribution. The cooperative loop is retained
for deadline/network/status progress. No generic timekeeping or wait-worker
optimization is included. The native comparison below measures a smaller regression; nested-QEMU cost
does not predict native throughput.

## Native ThinkPad A/B

Owner-provided results on [#765](https://git.internal/PyxisOS/pyxis-os/pulls/765),
2026-10-12 about 22:30: ThinkPad on AC, Ethernet, PXE; each revision booted its
own kernel and initrd. A is main `4366eb28`; B is #765's `98b51307` lineage with
userland #240. The fixture is the same 1 MiB iobench file served by
`python3 -m http.server` on horse over the LAN. Command:
`iobench read http://192.168.0.213:18080/fixed --rounds 5`.
Five byte-verified samples per boot; order A0 (already running), B1, A1, B2, A2.
Ranges are minimum–maximum. Raw files remain at
`horse:~/xf/765{A0,A1,A2,B1,B2}`; the table is the posted native summary,
not a new run by this task.

| Boot | OPEN median (range), ms | Payload reads median (range), ms | Whole GET median, ms | MiB/s |
| --- | --- | --- | --- | --- |
| A0 | 12.375 (11.92–13.46) | 1.862 (1.85–1.99) | 14.289 | 70.0 |
| A1 | 12.244 (12.08–12.45) | 2.231 (2.03–2.48) | 14.693 | 68.1 |
| A2 | 12.275 (11.98–13.66) | 2.008 (1.91–2.11) | 14.310 | 69.9 |
| B1 | 12.569 (12.15–12.63) | 4.638 (4.63–4.67) | 17.243 | 58.0 |
| B2 | 12.617 (12.04–14.31) | 4.597 (4.58–4.61) | 17.265 | 57.9 |

OPEN is unchanged. Buffered payload reads add about 2.6 ms/MiB, roughly
10 µs per 4,088-byte READ (257 reads); the whole GET costs +18%
(14.3 → 17.2 ms, about 70 → 58 MiB/s). Source inspection attributes the extra
work to cooperative readiness, BSP and timers when no READ is queued at TRY;
this is not a measured per-function profile. The owner accepted the cost on
2026-10-12, with the [throughput obligation and revisit points](https://git.internal/PyxisOS/pyxis-os/issues/1451).
The approximately 220 MiB/s buffered-read ceiling exceeds the current native TCP
reference (70.5 MiB/s send, 85 MiB/s receive, 2026-10-09); expecting larger
HTTP downloads to remain network-bound is an inference, not a measured result
or throughput contract. Track the HTTP/TCP gap with each relevant change.

## QEMU qualification and limits

Plain HTTP manual native-grant checks passed:

- Fixed/chunked/close-delimited GET at 1/17 MiB, exact bytes, max READ 4,088;
  shared cursor, single-use handoff and zero-capacity refusal. Truncated fixed/
  chunked bodies retain a prefix then IO, not EOF. Complete 404/500 retain bodies.
- All six 205 fixed/chunked/close cases: empty completes; nonempty exposes no
  content, returns IO and retains 205. Legacy FILE snapshots remain capped at
  16 MiB; over-limit OPEN returns FILE_TOO_LARGE without a grant.
- POST 0/1/17 MiB, independently verified upload length/SHA-256; 404/500 bodies
  and early 413 retained. Frozen writer copies cannot mutate; no READ/SYNC,
  observer ABORT or control STATUS authority. Invalid Host refuses before wire;
  credential-marked 302 returns Location without following it.
- Concurrent COMMIT in both winner orders returns one reader and one status-only
  success, with one server POST. The initial BUSY-on-opening defect was corrected
  and rechecked. Cancellation, control/reader retirement, expiry, repeated ABORT
  and export exhaustion/recovery passed (15 retained GET records, then LIMIT).
- STATUS/ABORT work during pending headers/body reads and concurrent npfs staging.
  The body-only child has no roots/cwd/control/observation; cancellation retained
  420,240 known bytes across 103 writes, no HTTP submission, cleanup OK. Specific
  DIRECTORY-operation responsiveness remains source-inspected. GET body and POST
  headers delayed 35 seconds complete within the original 120-second deadline.
- Pausing after 4,088 bytes of a 17 MiB GET leaves received/consumed counters
  12,280/4,088 unchanged for two seconds (8 KiB queued); resumption completes.

The custom local init initially omitted net configuration; it was corrected with
checked native VirtIO bind and static usernet assignment, not a provider change.
Final native fixture uses a disposable GPT/256 MiB npfs pool with an 8 MiB journal;
private staging is unlinked before writer export and all caller directory/cwd
aliases are closed. Helpers and runtime config are excluded from ordinary images.

Compiled debug types (source inspection, not a heap-peak measurement) show
transaction 2,888 B, work 31,664 B, engine 48,824 B, shared loop 1,128 B and an
8,192 B response queue. Request-header allocations, TLS, allocator bookkeeping,
kernel network buffers and npfs cache are additional. Terminal observation-only
records release engine/work/queue/backing. The old 1 MiB snapshot allocation was
inspected before code changes; native streams do not reserve total response size.

HTTPS checks passed on the final source: fixed/chunked/authenticated-close 1 MiB
and fixed 17 MiB GET; 1/17 MiB POST with independent length/hash checks; complete
404 and early 413 bodies; all six 205 cases. Raw TLS EOF after a prefix fails
TLS_TRUNCATED/IO; authenticated EOF before fixed/chunked completion remains HTTP
framing failure. A 35-second body wait completes under the original deadline;
STATUS/ABORT remain available during header/body waits. Provider death after a
4,088-byte prefix makes READ and observation both ENDPOINT_CLOSED, not EOF.
TLS 1.2-only compatibility was then checked with 1 MiB GET and POST by forcing
the local peer's version range to TLS 1.2. Both completed with exact bytes/hash.
A separate 17 MiB chunked HTTPS run was not performed; the shared chunk decoder
was checked at 17 MiB over HTTP and 1 MiB over HTTPS.

Ordinary kernel/SDK/C++ runtime/ports/userland/image builds passed, including the
final ABI/observation build. The guest reclaimed its provider processes and powered
off with pool flush; the extracted npfs pool passed the existing read-only
`fsck.npfs --image` structural check. This is no power-loss or native-disk evidence.
All task-owned QEMU/GDB/fixture-server processes are stopped. Raw captures remain
local. Userland #240 has no independent CI workflow; parent #765 builds its
published head.

The [interface](../../interfaces/http-transactions.md#private-backing-and-limits) retains
synchronous DNS randomness (3 s) and TLS entropy callbacks (5 s), additionally
bounded by the request deadline. STATUS/ABORT cannot run during those calls,
capability/service handoffs, allocation or cryptography. Native coverage is the GET cost comparison above, not the full functional
matrix. No multi-GB transfer is claimed; final end-to-end multi-GB shell/Lua delivery
belongs to the later adapter/coordinator tasks.
