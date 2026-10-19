# Multiple-interface public delivery qualification

2026-10-18–19, [task 3](../../devices/network-interfaces.md). This records the
pre-change controls, refreshed interleaved controls and the public cut's nested
QEMU qualification. No native run, PXE staging or personal-data override.

## Revisions and configuration

The pre-code baseline is main `fed2ce888506dd1e30d076d6ea1411a5102f797e`,
userland `a6c39a12`, ports `7604c85d`, fs `25337999`, lwIP `a1aadb91`.
Clean bundles came from successful [CI run 3112](https://git.internal/PyxisOS/pyxis-os/actions/runs/3112)
(API 3212), with provenance/hash verification. ISO SHA-256
`7e4f7b6bdc20aa59678650acaa089493f61e297230784d29009175774dd8ef61`;
ELF `852d9c996237675f812dc198b04089cde0fa20d534ffd859d5024fb40cb1c2d1`.

The refreshed control is main `3bb300aea365d5986a068f4753efff0832bd7b6c`,
userland `a9f6c64a`, ports `7604c85d`, fs/lwIP unchanged. Verified main
[CI run 3137](https://git.internal/PyxisOS/pyxis-os/actions/runs/3137)
(API 3237) supplied its image; ISO SHA-256
`56234c2adea3738a9e722386e92bf1324b337bd85d9313d9b6c8fbeec7fd3daa`.

The final qualified source is parent `de41eb9ecb53009694bdb2263eb9dc6d3e1480df`,
userland `147ddb78997399da344401fa9402009af3e9dd19`, ports
`cbc017ee0988fbc55cc272ce19eeae474d972774`; fs/lwIP unchanged. Parent base
`2b001c6e` adds unrelated npfs staged-writeback and docs to the refreshed
control; these diskless workloads exercise no mounted npfs path. Full kernel,
SDK/runtime, ports, applications and image builds passed in the existing owner
builder `pyxis-llvm23.1.3-3d69e83`. The final kernel-only route correction reused
verified unchanged SDK/application/ports bundles. All four checks passed on
[CI run 3170](https://git.internal/PyxisOS/pyxis-os/actions/runs/3170).
Final stock ISO SHA-256
`8208c7a3450d707ba288f17bb8c1289f2866cd11c8c7ed85bd127ba4503ac4d2`;
ELF `d47f96095a95e874f9067979a425d4972586a8d55775df41c3901dda7e2ccbb3`.
Private dual/static/rejection profiles change image assembly only.

Use the [task-1 configuration and commands](network-interfaces-baseline.md):
QEMU 10.2.2, Q35 nested KVM/kernel IRQ chip, host CPU, four CPUs, 2 GiB, raw
OVMF pair/fresh VARS, standard VGA, IDE ISO, VirtIO RNG and modern VirtIO NICs.
Single-interface controls use user networking/DHCP, no disk or HOST export and
the same frozen 120×40 machine client. No debugger overlaps a timing window.
These are whole-emulator/NAT controls on a shared VM, not physical NIC limits.

## Matched controls

Before code: one fresh boot, 8 s warmup, repeated TCP/UDP/hash, three fresh
1024-launch sessions, three CALL/SEND invocations each, three 5 s idle windows,
then a separate GDB snapshot. One launch overlapping GDB was excluded and
replaced before implementation. All listed byte counts/hash/UDP payloads,
1024 child exits and normal/drain-complete FINAL records matched.

| Pre-code control | Three samples |
| --- | --- |
| TCP TX / RX, 16 MiB, MiB/s | TX 3.365, 3.411, 3.376; RX 4.895, 4.878, 5.367 |
| 1024 launches, whole-client seconds | 14.796, 14.997, 14.947 |
| CALL five-pass medians, ms | 3.298, 4.679, 3.173 |
| SEND five-pass medians, ms | 0.758, 0.809, 0.446 |
| Idle whole-QEMU CPU, percent of one CPU | 10.2, 8.8, 9.8 |

Refresh order was A1 main → B1 public cut → A2 main → B2 deadline correction →
A3 main → A4 main → B3 final source. B1 used parent `624cffd6`/userland
`c9cc3ba4`; B2 used `5cee46b6`/userland `7d138f44`. Later source changes add
manifest admission/dependency fixes and the off-link pinned-factory correction.
A3 throughput was captured while another agent's build was observed; it is
retained raw but excluded from cost attribution. No competing compiler build
was observed in the other windows; shared-VM scheduling still varies.

| Control | A1 main | B1 public cut | A2 main | B2 deadline correction |
| --- | --- | --- | --- | --- |
| TCP TX, MiB/s | 3.787, 3.747, 3.730 | 4.105, 4.103, 3.729 | 3.787 | 3.794, 3.228, 2.514 |
| TCP RX, MiB/s | 5.444, 5.470, 5.316 | 6.285, 6.135, 6.259 | 5.884 | 5.938, 3.660, 4.372 |
| 1024 launches, seconds | 14.045, 14.446, 14.045 | 13.243, 12.591, 12.943 | 15.001 | 15.352, 13.248, 13.444 |
| CALL medians, ms | 2.828, 3.183, 3.320 | 3.026, 3.225, 2.528 | not repeated | 2.573, 3.200, 3.246 |
| SEND medians, ms | 0.519, 0.595, 0.414 | 0.492, 0.527, 0.413 | not repeated | 0.525, 0.515, 0.450 |
| Idle CPU, percent | 9.8, 8.8, 9.0 | 11.6, 13.0, 11.2 | 11.0, 12.4, 13.6 | 8.8, 8.8, 8.6 |

These TCP TX peers counted a full output file; A4/B3 repeat the original task-1
`socat | wc -c` sink to remove host file writing from both sides. Same 16 MiB
payloads, ttcp commands, fresh boot, client and TX/RX order; every TX count was
16,777,216 and RX reached that count through EOF. A4/B3 ran no launch/IPC phase.

| Count-sink confirmation | A4 main | B3 final source |
| --- | --- | --- |
| TX, MiB/s | 2.336, 2.411, 3.824 | 3.724, 3.689, 3.650 |
| RX, MiB/s | 3.132, 5.081, 5.792 | 6.181, 5.750, 5.687 |
| Idle CPU, percent | 11.6, 9.8, 11.4 | 10.0, 9.4, 10.6 |
| Whole-QEMU RSS after this workload, MiB | 327.363 | 325.188 |

Both images vary materially. B2's slower TCP samples recur in main A4; no
consistent candidate regression was established. These ranges do not rule out
small changes or establish a native speedup. Source inspection found needless
100 ms polling by an entirely bound DHCP owner; it now sleeps until its earliest
renewal/expiry, retaining bounded polling for pending link/exchange/cleanup work.
The A2 idle drift prevents attributing the whole idle difference to that fix.

After B3, GDB observed 15,210 global PMM allocated pages, one retained DHCP UDP
endpoint, no retired UDP list, worker wait published, notification clear and local
queue empty. Interface context and lwIP projection sizes are 128 and 72 bytes
before allocator overhead. Whole-QEMU RSS and global allocator snapshots include
live spaces and workload warming; neither isolates network backing nor proves
all failure paths leak-free. Debugger inspection occurred after measurements.

## Public-cut functional qualification

Interactive boots and GDB used the same four-CPU configuration. Single-interface
Remote, loopback/own-address/gateway ICMP, exact UDP echo, TCP TX/RX/hash and
closed-port errors passed. A RAM-built disposable C consumer exercised factory
scope enumeration/end, one-interface derivation, denied scope/rights widening,
copy/close retention, CUT lineage, loopback-only rejection of physical targets
and real loopback UDP/echo. The registry's lookup facet could query but not clear
configuration. This consumer was not added to the build or repository.

Dual-interface private profiles use two distinct QEMU MAC selectors, independent
user LANs `10.0.2.0/24` and `10.0.3.0/24`, plus an absent optional `wlan0`.
Both Ethernet clients acquired independent port-68 DHCP leases. Default egress
selected eth1 while DNS and Remote/log/debug selected eth0. Retained controller
order differed from profile order; logical names/IDs still matched configuration.
The absent optional radio did not block either client or the local shell.

Observed checks:

- Both gateways and local addresses worked. Equal UDP ports opened separately
  on both interfaces; INSPECT reported the selected ID/address epoch. Conflicting
  source/path, unknown IDs, cross-interface host-owned destinations and scope
  widening failed. Derived one-interface factories used their own off-link
  gateway even when system default selected the other NIC. Both complete final
  manual-consumer runs exited zero with drain-complete FINAL.
- `set_link` off/on for the data NIC changed its reported carrier/DHCP readiness;
  management on the other NIC remained usable. Existing address retention on
  carrier loss was preserved. Address replacement, lease expiry and held-flow
  policy changes need task 4's deeper lifecycle matrix.
- Ordinary Remote accepted only on the selected management NIC. Reverse Remote
  used a loopback-only host beacon and retained that ingress interface for the
  follow-up connect; repeated sessions disconnected/reconnected normally. No
  native beacon or target was contacted.
- `lua -e 'assert(pyxis.run({"dig","example.com"}) == 0)'` resolved through the
  selected DNS interface in both dual-DHCP and DHCP-plus-static boots. A numeric
  host-local HTTP URI returned its independently computed hash through completion
  CONNECT. Lua's shipped bundle admitted all twenty declared requests, including
  its optional separate resolver; no registry authority was added to its default
  manifest. Runtime catalogs can forward registry READ only when held/declared.
- A mixed static/DHCP profile configured eth1 without inventing a DHCP lease and
  preserved eth0's resolver. A legacy `net0` profile failed with a clear error
  before binding: GDB showed only loopback and an empty policy.
- Final dual-session GDB inspection found only the expected DHCP/beacon UDP
  endpoints, no retired UDP records and no loopback queue. Earlier bounded
  ordinary/error-path checks found configuration/TCP control slots and echo
  records released. These snapshots do not qualify every cancellation/fault or
  copyout-failure path. No fault-injection infrastructure was added.

Management log/debug selection and fatal/stopped-driver ownership were source
reviewed; the private profile's policy snapshot confirmed their selections.
Native emergency transport, radio traffic and the full lifecycle/isolation matrix
remain task 4, requiring a separate owner go. USB NCM stopped/fatal transport is
still unsupported. No automatic fallback or replay was introduced.

Subsequent [native task-4 qualification](network-interfaces-native.md) covers
both wired ThinkPad roles and scoped DNS supplements. Physical unplug and
deeper held-flow/failure interleavings remain explicit technical debt; this
earlier record retains its original evidence boundaries.

## Evidence

Raw pre-code argv/events, hashes and excluded launch sample:
`/shared/present/network-interfaces/task3-baseline/`; component provenance in
`task3-inputs/`. Early scoped-core captures remain under `task3-work/`.
Refreshed/final builds, profile files, events, peer counts, debugger snapshots
and timing context are under `task3-public/`: `main-control`, `final-single`,
`main-control-2`, `post-deadline`, `main-control-3` (excluded timing),
`paired-main`, `paired-final`, `dual`, `reverse`, `reverse-final`, `static` and
`reject`. Paths are relative to `/shared/present/network-interfaces/`.
The screenshot retained in `/shared/present/screenshots-keep/network-interfaces/`
shows the real dual-interface local TTY. All owned QEMU, debugger and peers were
stopped after qualification. Exact submitted-head CI is reported in the PR;
standalone dependency workflows expose no CI tasks.
