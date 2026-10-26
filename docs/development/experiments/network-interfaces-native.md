# Multiple-interface native qualification

2026-10-19, task 4 of [multiple interfaces](../../devices/network-interfaces.md).
Owner-scheduled hands-free ThinkPad runs collected from horse. Both interface
roles qualify with the DNS supplements below. A subsequent owner-present USB
unplug/replug observation also passes; no held-flow, Wi-Fi, stopped-debugger or
exhaustive lifecycle claim.

## Revisions and method

Exact main `b52533d96e6026e0fc910f8b67d8f529af066801`, userland
`147ddb78997399da344401fa9402009af3e9dd19`, ports
`cbc017ee0988fbc55cc272ce19eeae474d972774`, fs
`b3aae99f2aec02675edfe2ce0b076e1a2822314e`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. Clean components from
[main workflow3177](https://git.internal/PyxisOS/pyxis-os/actions/runs/3177)
(API3280), builder `pyxis-llvm23.1.3-3d69e83`; no personal game-data overrides.
The identical kernel SHA-256 is
`81cb47beeb5913721ea3eb55fa14db34aab965292ba608963079e16d6a22fdeb`;
effective configuration
`ad253301a125b3bc4ab6164b2347edcef3dfc215c04cb83bab96eb819cc07c90`.
The native kernel reports `b52533d96e60` and twelve online CPUs on the
Ryzen 5 PRO 4650U. The packet retains both image/archive manifests.

The built-in RTL8111 (`eth0`) and USB RTL8156BG CDC-NCM (`eth1`) share a LAN
and switch with horse. Two private profiles exchange data/default versus
DNS/Remote/log roles. Each selected data NIC is first in the profile's connected-
prefix tie order; changing the system default alone would not override that
on-link tie. MAC selectors stay outside repositories and published records.
The runner refuses a wrong driver, profile, revision, missing independent DHCP
lease or changed address mapping. Both management paths use reverse Remote
through the `t14` beacon; no concurrent listener is allowed.

Each base round interleaves three 64 MiB TX, 4 MiB RX and 64 MiB RX transfers,
reversing order for sample 2. TX is counted by `socat | wc -c`; separate 4/64 MiB
receive hashes match the source. It also runs a RAM-built scope consumer, three
CALL and three SEND controls (64-byte payloads, eight messages, five measured
passes each), 1,024 plain child launches, and before/after fastfetch snapshots.
Success requires actual child FINAL/exit/drain evidence, not launch acceptance.
Own RAM sources/programs and host helpers are cleaned. No mounted disk, ESP or
NVMe writes, debugger or target configuration mutation occur in these runs.

## Native results

| Round | Data/default | DNS/Remote/log | Original result | DNS supplement |
| --- | --- | --- | --- | --- |
| `native-usb-02` | USB NCM | RTL8111 | 118/119; only `wire.dns` missing | `native-dns-usb-02`, 21/21 |
| `native-pci-02` | RTL8111 | USB NCM | 120/121; only `wire.dns` missing | `native-dns-pci-01`, 21/21 |

Both base rounds verify independent addresses/DHCP, same-port UDP namespace
separation and returned interface/address epochs. Registry lookup cannot clear
configuration. TCP/UDP/echo factory enumeration, attenuation, copy/close
retention, denied widening and ancestor CUT lineage pass. Loopback-only grants
cannot access a physical local address. A one-interface factory opens an
off-link binding on its own interface despite the other being the system
default; this checks the returned interface ID, not an off-link transmission or
next-hop observation. The gateway-selection rule remains source-defined.
Conflicting source/interface restrictions and cross-interface host-local sends
are refused. These are the operations exercised by the retained `scope.c`, not
an exhaustive proof of all failure paths.

PCAPs confirm TX/RX only on the selected data address, reverse Remote on the
other address, and valid new-boot log headers plus START/sequence zero only from
the management address. Boot stamps group observations, not authenticated
identity. Every transfer count/hash, scope result, IPC pass, 1,024 child exits,
normal FINAL/drain and own-RAM cleanup passes.

Both original default DNS queries succeeded against `1.1.1.1`. Their switched
unicast path to the gateway cannot be seen at horse, and the original capture
filter also required host traffic. The original checks remain unqualified and
unchanged; a missing packet was not converted into a pass.

The two DNS-only supplements reuse the unchanged images and address/profile
mapping. Existing `dig @horse example.com` still uses the selected `dns_udp`
grant; a bounded client-restricted host UDP forwarder relays to the real public
resolver, without synthesized answers or a system DNS change. For **each** role,
the capture verifies three complete transactions, byte-identical query/reply
pairs across all four legs, zero wrong-interface queries, normal child/session
completion, verified proxy/capture cleanup and no owned firewall rule left. This proves
the selected DNS grant's source on the connected LAN, not direct observation
of the default public resolver's routed path. The combined base/supplement
checker re-evaluates all original predicates except that documented blind spot.

### Measured costs

Reported ttcp rates, MiB/s; three samples in run order:

| Workload | USB data | RTL8111 data |
| --- | --- | --- |
| 64 MiB TX, including closure | 83.777, 84.123, 84.256 | 112.495, 112.478, 112.556 |
| 4 MiB RX, through peer EOF | 78.012, 76.673, 76.873 | 111.433, 112.708, 112.634 |
| 64 MiB RX, through peer EOF | 82.224, 79.518, 79.674 | 113.024, 112.389, 113.003 |

| Control | USB-data profile | RTL8111-data profile |
| --- | --- | --- |
| CALL completion medians, ms, three invocations | 0.062, 0.069, 0.067 | 0.063, 0.067, 0.069 |
| SEND completion medians, ms, three invocations | 0.059, 0.062, 0.044 | 0.044, 0.044, 0.044 |
| 1,024 launches, whole-client seconds, one sample | 1.149 | 1.296 |
| Global used memory before/after, MiB | 163.125 / 168.477 | 162.871 / 167.984 |

The roles run on the same revision/client/configuration, with different NICs
and profile order. They are **not** an old/new-code A/B or a matched comparison
to earlier single-NCM runs. The measurements do not isolate NIC overhead,
network backing, a leak or an idle/CPU regression. Global memory includes live
spaces and warmed work. The earlier [core](network-interfaces-core.md) and
[public-cut controls](network-interfaces-task3.md) retain their matched nested
QEMU baselines; native rates do not replace those controls.

### Readiness miss and excluded setup attempts

`native-dns-usb-01` stopped before DNS setup: `net` exited zero, but eth1 was
prepared/bound with link down, no IPv4 address and DHCP `wait-link`. Eth0 was
up with DHCP bound. The supplement had omitted the base runner's bounded
readiness poll; it now polls read-only for 30 seconds and reports the saved
execution error on early refusal. Fresh `native-dns-usb-02` passed on that same
USB-data boot, without a further reboot. This does not establish the cause of
the earlier link-down snapshot. The failed attempt and its original incidental
`result_contract` report are retained, not counted as successful DNS evidence.

`native-usb-01` failed capture authorization before staging: the supervisor's
new session hid the tty-scoped sudo ticket. Keeping the invoking terminal
session fixed it and the owner ran subsequent rounds with FIDO authorization.
`native-pci-01` is void because a reboot listener still held the Remote port.
The next attempts used fresh directories. Packet staging later learned to
rebaseline a legitimate new menu only while restored/idle, preserving refusal
of outside edits while its own menu is active. These are host-harness changes,
not target-kernel or image changes.

## Owner-present USB withdrawal and recovery

The follow-up `native-unplug-02` used the USB-data profile with built-in Ethernet
for management. All `removed`, `management-alive`, `restored-0` and exit commands
completed successfully in the same Remote session, followed by an exited-zero,
drain-complete FINAL. The owner physically removed and reconnected the same
adapter on the same port; the helper issued no reset or configuration change.

| Observation | USB interface | Built-in management |
| --- | --- | --- |
| Before removal | Link up, ready, IPv4 assigned | Link up, DHCP bound |
| Removed | Carrier unknown, `ready no`; IPv4 retained | Remote `management-alive` succeeds |
| First query after reconnection | Link up, ready; same interface ID, controller, address and address epoch | Same interface, session remains usable |

The USB address epoch stayed at 2; its logical interface and controller IDs
were unchanged. The retained DHCP bound-report age increased across removal
and recovery. This qualifies that observed withdrawal/recovery path, not a fresh
DHCP exchange or preservation of active TCP/UDP flows. `restored-0` means the
first query after the owner's confirmation; no physical insertion timestamp
was recorded, so it is not a recovery-latency measurement.

`native-unplug-01` is void: its sole initial `net` query found eth1 prepared/bound
but link down, without IPv4 and reporting DHCP `wait-link` from 2.794 s earlier.
The owner had not yet been prompted to unplug. The next attempt's initial
snapshot, 19.119 s later by host output-file timestamps, was ready and reported
DHCP bound 17.745 s earlier. The initial command's 19.025 s host wall includes
Remote acquisition; it does not isolate NCM initialization. The owner reports
this followed a warm Remote reboot, with the observation started about one
minute later; the records contain no reboot timestamp to derive total latency.
Together with `native-dns-usb-01`, this is the second observed post-warm-reboot
readiness miss. It remains [readiness debt](https://git.internal/PyxisOS/pyxis-os/issues/1280),
not evidence that physical unplug failed. Both attempts are retained.

## Evidence and remaining limits

Raw outputs, machine events, hashes, packet captures, helper cleanup records and
component provenance live under
`/shared/present/network-interfaces/task4-native/`, in the named round directories.
The originals are unchanged. `provenance.json` records assembly-time state;
its pre-execution staging/native flags are not the final run status. Profiles,
images and raw PCAPs contain private LAN identifiers and are not published.
The orchestrator reports byte-exact PXE menu restore after the final supplement.
No helper, QEMU or debugger remains running for this task.

The [task-3 record](network-interfaces-task3.md) supplies dual-LAN QEMU controls,
optional absence, mixed addressing, scoped operations, data-carrier off/on with
management preserved, configuration errors and ordinary cleanup snapshots.
Retained flows across address/default/lease changes,
exhaustive cancellation/fault/copyout interleavings and native stopped-debugger
selection remain [explicit qualification limits](https://git.internal/PyxisOS/pyxis-os/issues/1279).
The owner-present unplug observation is now recorded above. Radio traffic
remains Wi-Fi work, and USB NCM stopped/fatal transport remains unsupported.
