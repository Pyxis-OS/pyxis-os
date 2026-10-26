# Go networking qualification

2026-10-23. Kernel prerequisites and Go adapters. Caddy and native/PXE
qualification remain open.

## Revisions and setup

Baseline kernel: `1896666645001f723eed9d6168a490d272c950fe`. The matched
pre-rebase implementation includes the shared storage/completion accounts,
dynamic TCP/UDP records and bounded native queues through `719fff4d`.
After rebasing onto main `49c98834`, the complete image builds with userland
`91a8096f3e9d2490c49d5e0beebc0e519c276022` (PR #359, based on `6de2b65`),
ports `298ccda5300fb102ca42e9bd3aece1ffa737b4cb` (PR #152), filesystem `f3f22b6e`
and lwIP `a1aadb91`.

Use the published `pyxis-llvm23.1.3-3d69e83` builder, `make -j16 image`,
and the same optional Go hello bundle. QEMU is nested KVM q35, four
`max,+invtsc` vCPUs, 2 GiB, OVMF, VirtIO RNG/NIC, user networking and a read-only
VirtIO-SCSI ISO. The guest uses its stable KVM clock. Timing includes nested
virtualization and host scheduling; it is not a native hardware result.

Private native callers use the current public SDK and existing trusted
`session boot://netnative.pxe MODE` handoff. Ordinary bundle `service/create`
provisioning follows the accepted [authority decision](../../../wip/go-runtime.md#additional-decision--ordinary-client-completion-authority-accepted),
not a product feature supplied by this handoff. No fixtures, instrumentation or
new benchmark framework are merged.

## Initial functional checks

- Native TCP request/reply, UDP echo, configured DNS lookup and Go hello passed.
  Missing LISTEN authority refused; it did not become available from CONNECT.
- 64 simultaneously held TCP streams registered in one readiness set. Exactly
  the four armed WRITABLE sources were returned; empty TRY_READ was WOULD_BLOCK.
- Four receivers admitted 64 pending UDP receives. Duplicate direction was BUSY;
  pending operations were not READABLE. Cancellation produced 64 actual terminal
  results, each collected/acknowledged once. Drained UPDATE/rearm stayed empty.
  Final logical receiver close produced CLOSED despite retained observation;
  REMOVE left no event. Repeated rounds passed.
- Zero-byte FILE reads retained 925 completion receipts before CALL_LIMIT.
  The receiver's seventeenth delivery independently refused QUEUE_FULL. After
  acknowledgement and actual deferred reclamation, a fresh operation completed.
  The count is an observation of this operation's metadata, not an ABI limit.
- A process exited with 64 pending UDP operations and all associated objects;
  subsequent session admission/cancellation/collection passed. Cleanup did not
  borrow the dead process's private VM.
- TCP pressure retained 1011 one-byte streams with allocated receive rings
  before an actual TRY_READ NO_MEMORY. The other 1010 streams inspected as
  connected, without eviction; closing the population permitted a fresh
  connect/read/close. These are observed populations, not promised capacity.
  Post-workload GDB inspection found empty pending I/O/retirement queues and
  approximately 69 KiB of charged transport storage, including surviving
  connections/transport timers. No debugger ran during timed samples.

## Matched short-throughput samples

Existing `ttcp` sends/receives one MiB, three times per boot:
`ttcp -t -p 18083 -n 128 -l 8192 10.0.2.2` and
`ttcp -r -p 18084 10.0.2.2`. Host sinks/sources independently count bytes.
A fresh baseline kernel and the implementation kernel share identical archive,
firmware, boot options, devices and CPU layout; fresh boots run A–B–A–B.
Six observations per direction per revision:

| MiB/s | Baseline range; median | Implementation range; median |
| --- | --- | --- |
| TX, including closure | 44.2–58.8; 56.2 | 52.9–59.3; 57.9 |
| RX, through peer EOF | 76.4–89.6; 83.6 | 66.4–88.3; 85.3 |

Earlier noninterleaved runs varied considerably (including lower implementation
rates). The interleaved ranges overlap and do not establish a speedup or a
repeatable regression. Dynamic operation allocations and shared accounting add
real work; redundant per-record/empty-queue clock reads were removed. No claim
about sustained service throughput, fairness or large-population timer latency
follows from these short transfers. Pinned lwIP still has interrupt-enabled full
PCB-list walks; see [the limit](https://git.internal/PyxisOS/pyxis-os/issues/1498).

Raw commands, manifests, source caller, hashes and captures remain in
`/shared/present/go-net/`: `baseline/`, `native/`, `native-pressure/`,
`native-final/`, `repeated-baseline/` and `interleaved/`. The last two retain both
matched kernel identities and the identical archive hash. Full-image build output
is `final-build.txt`. No private payload or raw capture is committed here.

## Explicit constructor delegation

After rebasing the native branch onto main `4647c424` (merged library adapters),
the full image builds with userland `91a8096f` and ports `298ccda5`. Ordinary
bundle launch in QEMU created a real receiver, observed private entry flags and
found no constructor in the exported inheritance capture. Raw children and
bundles omitting the grant received none. Lua run/job paths and libuv reached
actual child completion. Lua's explicit resource list without service and
libuv's explicit zero-right service selection refused; default/full selections
worked. No constructor fallback or new source was used. Private fixtures and
captures: `service-build/`, `service-runtime/` and `go-runtime/` below the
shared evidence directory. These checks are separate from Go network delivery.

## Go networking qualification

Fork `17511414e63488257a01ef2ba500e867f7ec27da` builds `net`, `net/http`,
`crypto/tls` and `crypto/x509` for Pyxis. Existing `go/build` TestDependencies,
Linux net and Plan 9 net cross-builds pass. The ordinary pinned recipe rebuilds
the published fork without upstream downloads; the compiler container is unchanged.
The following checks use the same four-vCPU nested configuration, not native
hardware. The fresh recipe-built caller repeats the runtime checks in
`go-final/` and `rebased-final/`; after rebasing onto main `0a3e0c03`, userland is `3d3c8ea9`
and ports remains `298ccda5`. The explicit `DISTRIBUTION=full` qualification
archive retains the Remote caller; ordinary CI builds the default core archive.

- TCP short/122,880-byte copied exchange, `CloseWrite`, drained EOF, refusal,
  canceled pending CONNECT, read deadline/clear and partial write deadline.
  The blocked writer accepted a real prefix of a 16 MiB request before timeout;
  this is a functional check, not a throughput result.
- UDP ordinary/empty datagrams, truncated copy with the remainder consumed,
  expired/cleared/extended deadlines, joined close and 64 simultaneous readers.
- Numeric TCP works without DNS configuration/authority; missing DNS factory and
  malformed server fail. Configured IPv4 lookup, fixed HTTP response and verified
  HTTPS to example.com pass. A self-signed test certificate fails with unknown
  authority; absent/malformed public bundles fail with `SystemRootsError`.
- A private trusted init launch with explicit LISTEN completed an independently
  host-verified request/response on concrete guest IPv4/port. Its zero process
  result gated subsequent Remote startup; this is not a new interactive LISTEN
  grant or a wrapper/broker feature.
- While 64 TCP or UDP readers and stdin waited, all-stop kernel GDB snapshots
  showed 65 registrations using 34,120 readiness-metadata bytes. TCP had 3 live
  and 3 live/retiring native tasks; UDP had 5 and 5. This establishes that these
  idle populations did not reserve one native task per connection, not a maximum
  task count or a large-population performance result. Each close joined all reads.
  The recipe-built rerun showed 3 live/retiring tasks for both populations; task
  counts vary with runtime scheduling. Force-stopping 64 pending UDP readers
  then admitting/closing a fresh population also passed.

Raw source/commands/hashes/captures: `go-runtime/`, `go-extra/`, `go-server/`,
`trust-invalid/`, `trust-missing/`, `go-final/`, `rebased-final/`, `guest/` and
`recipe-build.txt` below
`/shared/present/go-net/`. The private controller retries only pre-command
connection setup while QEMU host forwarding precedes guest listener readiness;
submitted commands/native sends/connects are not automatically replayed.
No fixtures, boot automation, fault injection or new benchmark framework is merged.

## Startup comparison

The existing `go-launch-probe` launches hello eleven times per series after one
discarded warmup, retaining actual process/group retirement. A is merged library
adapters (`9629361b`); B is networking (`17511414`). Both run on the same
prerequisite kernel/archive in A–B–A–B order, `GOMAXPROCS=1`, with no debugger
during the series. All 44 measured launches returned the expected 23 output bytes
and exit status zero. Twenty-two samples per revision, milliseconds:

| Observation | A range; median | B range; median |
| --- | --- | --- |
| Launch to first output | 4.408–6.600; 4.794 | 4.286–5.858; 4.810 |
| Launch to observed exit | 4.554–7.033; 4.982 | 4.434–6.097; 5.020 |

The overlapping nested results show no material startup regression and stay
inside the 250 ms budget. They do not establish native startup latency. Commands,
probe source, image hashes and all samples remain under `hello-ab/`;
`recipe-output-hashes.txt` identifies the exact caller/hello binaries.

## Receive-window admission and review qualification

The current native backend (`90b2370b`) preallocates every stream's 65,535-byte
ring and reserves an additional receive window before SYN/passive acceptance.
Within the existing 64 MiB allowance, ordinary actual storage plus reserved
windows plus 64 KiB transient control capacity must fit. Protocol actual storage
consumes those reserved window/control credits instead of being charged twice.
New records and transmit data cannot spend ingress credits. The allocation
header retains the class through free. The short accounting guard never covers
heap work; accounting and heap calls share the existing IF scope.

RX/control/timer allocations use the protocol class. The current 16-pbuf
out-of-order bound, full 1,500-byte packet backing and segment metadata fit one
reserved window. A small optional lwIP timer-split hook keeps copied transmit
payload ordinary even when called by a timer. Listener conversion frees unused
stream backing; other reservations survive TIME_WAIT until PCB/pbuf destruction
has returned. Unread ring bytes retain their independent drain/close lifetime.
This is budget admission, not a guarantee against unrelated heap exhaustion.

Four-vCPU nested QEMU refused new admission with 507 held streams. GDB sampled
33,662,444 actual bytes (304 protocol bytes) and 33,291,780 reserved bytes,
including the independent Remote stream. The host then filled all 507 receive
windows with 65,535 bytes each: GDB verified every full ring, RX pbuf allocation
failures stayed zero, and all 506 survivors drained/verified every byte after
releasing one stream. Cleanup allowed a fresh exchange; only two service records
remained, about 67 KiB actual storage, with pending I/O/retirement queues empty.
These populations are observations, not ABI caps. Evidence: `window-native/`, `window-pressure/` and `window-pressure-final/` below the shared directory.

Fork `1780bcb4` is rebased onto merged terminal support (`9bac7195`); the terminal
and networking adapters both remain. Pyxis cross-builds of net/HTTP/TLS/x509,
os/syscall/poll and the toolchain terminal package passed, as did the existing
`go/build` TestDependencies check. The same Go networking/deadline/scope/cleanup
checks passed again on the new kernel. Genuinely pending UDP SENDs survived
extension and clearing beyond their original deadline, then joined actual expiry
without EINTR. A concrete-address LISTEN server completed a host-verified
exchange and retired before Remote startup, covering listener conversion and
passive admission. Native UDP cancellation/ACK and FILE receipt-budget/refill
checks passed. The final IF-scope kernel repeated the full suite and pressure
check. Evidence: `window-runtime/`, `window-runtime-final/`, `window-server/`,
`window-reserve-build-final.txt` and `window-irq-build.txt`.

FILE_TRANSFER charges its descriptor, job/state, receipt and cleanup metadata,
not requested I/O bytes. Source inspection shows the sole filesystem worker
allocates at most one 1 MiB scratch buffer and frees it before publication.
No new mounted-FAT32 transfer workload or native hardware run is claimed here.

## Accounting-cost comparison

The same existing ttcp workload runs three one-MiB TX and RX exchanges per fresh
boot in A–B–A–B order (six samples/direction/revision), with no debugger during
timing. Both kernels share the identical archive/configuration/devices above. A
is `be0ec108` (before window reservations); initial B is `68614a2c`. That first
series had lower B medians: TX 53.15/40.87 MiB/s and RX 82.20/67.78. The new
accounting added separate interrupt restore/save pairs around the heap call.
Consolidating accounting into the existing heap IF scope (`90b2370b`) retains
the same locks/credits while keeping the accounting lock outside heap work.

The repeated matched series, same A and archive, after that change:

| MiB/s | A range; median | Final B range; median |
| --- | --- | --- |
| TX, including closure | 41.14–59.08; 55.03 | 31.56–58.99; 53.87 |
| RX, through peer EOF | 70.45–88.64; 78.18 | 74.68–89.56; 83.77 |

These short nested results vary considerably and their ranges overlap. They do
not establish a repeatable material throughput regression, a speedup, or prove
the cause of every earlier slow sample. Evidence and exact hashes are in
`window-perf/` and `window-perf-irq/`; both use archive
`0bf6f96f366462f4b162a421b20b14ecc140dde2e750cc434a689e6f1c583dde`.
Native hardware and sustained/fair large-population service remain unqualified.
