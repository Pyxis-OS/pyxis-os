# Native prerequisites for Go networking

2026-10-23. Kernel prerequisite qualification only: Go sockets, TLS and Caddy
are not delivered by this record. No native/PXE run.

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

## Functional checks

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
PCB-list walks; see [the limit](../../../technical-debt.md#tcp-vendor-traversal-cost).

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
