# Automatic wired binding — first delivery

2026-10-19, QEMU only. Owner accepted the
[selection contract](../../devices/net0-selection.md) on this date.
No native machine, PXE payload or private network profile was touched.

## Revisions and build

- Control: OS `11376a8ec03c927c844ef1fecdd97c5ade6a8542`, userland
  `d5f905498194da33d8c26cddad1de52b573fad2c`, successful exact-main CI ISO.
- Candidate functional source: parent `aa3421cbffffd5d6d803c2b46b41af990887595b`,
  userland `061fc3f4c95eb78aa3534b3f0ff88ff0792e1afe` (userland #312).
  The first stock functional boot was built from the same implementation before
  its commit; subsequent profile variants and matched controls use the committed
  kernel. Later parent changes only record documentation/qualification.
- Unchanged fs `f3f22b6e07e7b38078ae86b414e430ffb0b4f9c2`, ports
  `e9b0eb4a1854d0e38313341aa46074c2fa1c8ae2`, lwIP
  `a1aadb91a50360ff5b52864f7cec810b8162ee85`.
- Existing `pyxis-llvm23.1.3-3d69e83` builder; full source kernel, SDK, ports,
  userspace and image build. Profile variants reuse those verified matching
  components via `PREBUILT="kernel sdk userspace ports"`; only image assembly
  uses `NETWORK_CONFIG=...`. No compiler/container rebuild or game-data override.

Raw commands, argv, profile files, serial/Remote outputs, debugger observations,
images and build logs are under `/shared/present/auto-wired/`. Machine-readable
Remote results retain actual completion status. Exploratory unavailable commands
and a pipe benchmark without its required creation grant are not counted as
successful qualification or timing controls.

## Functional checks

QEMU 10.2.2, q35/KVM, host CPU, four vCPUs, 2 GiB, the Fedora 44 OVMF fixture
pair with fresh variables per boot. Two modern VirtIO-net PCI devices use separate
user-network subnets, `10.0.2.0/24` and `10.0.3.0/24`. Controller 5 is the second
PCI device in these runs; controller 6 is the first. Synthetic QEMU MACs in the
private qualification profiles are not machine selectors. Remote is pinned to
`eth0`; no management fallback is exercised or supplied.

| Check | Observed result |
| --- | --- |
| Exact-main stock profile | Only controller 5 binds as `eth0`; controller 6 remains unbound. |
| Candidate stock profile | Controller 5 → `eth0`, controller 6 → `eth1`; independent DHCP addresses/gateways on both subnets. Policy remains pinned to `eth0`. |
| Carrier down before boot | Controller 6 still binds as `eth1`, reports down and DHCP `wait-link`; `eth0` operates. `set_link n0 on` gives the same `eth1` its lease without renaming/rebinding. |
| Unknown carrier | With VirtIO `status=off`, both controllers report unknown carrier, bind and acquire independent leases. No up-carrier claim is invented. |
| Excluded controller | Excluding controller 5's MAC leaves it unbound with no logical interface/client. Controller 6 receives `eth0` and DHCP. |
| Explicit reservation | Explicit MAC entry reserves `eth0` for controller 6; automatic controller 5 receives `eth1`, without duplicate binding. Both acquire leases. |
| `wired.auto = false` | Original optional linked `eth0` profile binds only controller 5; second device remains unbound. |
| All excluded / zero NICs | Both reach the local `home://` prompt. Debugger sees loopback only; absent automatic policy pins do not fabricate another path. |
| Explicit and excluded same MAC | Clear setup refusal, no spaces started, debugger sees no non-loopback publication. |
| Undefined generated pin `eth9999` | Parser refuses the undefined interface before publication; debugger sees loopback only. |

Idle debugger inspection also confirms distinct `eth0`/`eth1` registry entries,
VirtIO bindings and separate IPv4 configuration. GDB is detached before timing;
no stopped-kernel network behavior is claimed. No hardware fault, NCM hotplug,
initial-inventory timeout or resource-exhaustion injection was performed.

## Matched ordinary launch and memory controls

Sequential boots in order A1/B1/A2/B2/A3/B3, never concurrent measured guests.
A is exact main, B is the candidate stock profile with both NICs active. No
build/debugger job runs during these controls. The host Remote client and virtual
hardware are identical. Each session runs `net`, then
`fastfetch --structure Memory --logo none`, 16 untimed echo launches, and three
successful instances of:

```text
lua -e 'for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'
```

Elapsed time is host wall time from command submission to successful Remote
command completion, including Lua startup/launch/wait/exit and the result path;
it is not isolated kernel launch latency. Every measured loop reports
`launches=1024` and exited/0.

| Boot | 1,024-launch samples, seconds |
| --- | --- |
| A1 | 2.586 / 2.585 / 2.615 |
| B1 | 3.121 / 2.809 / 3.897 |
| A2 | 2.726 / 2.686 / 2.697 |
| B2 | 2.600 / 2.611 / 2.742 |
| A3 | 2.765 / 2.887 / 2.793 |
| B3 | 2.888 / 2.925 / 2.802 |

Across nine samples each, A median is 2.697 s (2.585–2.887), B median 2.809 s
(2.600–3.897), approximately +4.1%. B1 is distinctly slower, including one
3.897 s outlier; the later paired medians are −3.2% and +3.4%. Thus the first
slow round does not reproduce consistently, and these nested-VM observations
do not establish a precise steady-state cost or a native regression. They also
do not justify claiming zero cost. Both the wider range and aggregate difference
remain visible for review; no unrelated optimization was added.

Fastfetch allocator backing is 64.86–65.11 MiB in both A and B, with opposite
pool-boundary crossings between boots. This is global allocator allocation
sampled while Fastfetch runs, not process RSS or a precise per-interface cost.
Native multi-NIC cost/selection checks remain unrun and separately scheduled.

A follow-up candidate boot using the explicit-only profile (one active NIC)
reported 4.805 / 4.571 / 3.705 s; the immediately following fresh exact-main A4
reported 2.624 / 2.630 / 2.619 s. This slower candidate control remains
unattributed. It cannot be explained solely by activating a second NIC, and the
echo P1F payload is byte-identical between control and candidate. Source inspection
places the new kernel selector logic on binding, not child launch, but that is
not runtime attribution. These results are a qualification limitation to review,
not evidence that the cost is zero or that host noise caused it. The implementation
has not been broadened into a scheduler/launch optimization; a tighter profile or
native cost comparison would be needed to resolve the slow-boot cases.

## Accepted linked-role follow-up

Owner accepted the pending-role reservation rule on 2026-10-19. Full source build
passed at parent `69ee7f90` with userland
`642267de885a9ebf12e7ddcd2a73b2513d6443d0`; kernel/ABI logic is unchanged.
With the same four-CPU/two-VirtIO setup, explicit linked `eth0` preferred controller
5 while it was down; the working controller 6 became automatic `eth1`. Controller
5 remained unbound and reserved, with `eth0` waiting for link. After `set_link n1
on`, it bound as the same `eth0` and acquired its own DHCP lease, preserving the
peer and the explicitly pinned `eth1` management path across SESSION handoff.

With the preferred controller's VirtIO STATUS feature off, it remained unknown
and unbound while automatic `eth1` operated; debugger inspection confirms the
linked role still has no driver. Thus reservation does not bypass known/up
binding. In the otherwise matched `auto = false` profile, down preferred
controller 5 did not reserve the role: controller 6 bound as `eth0`, and stayed
there when controller 5 linked. Stock automatic two-NIC DHCP was also rechecked.
Raw follow-up records use `reserved-down`, `reserved-unknown`,
`linked-explicit-down` and `stock-followup` under the same evidence directory.
These are functional checks, not new timing samples; the measured stock workload
has no explicit role and does not take the changed reservation branch. All owned
QEMU/GDB/client jobs were stopped afterward.

The parent was then rebased linearly onto main `8e136b20400c996f3d7e66c3c9893ddfca0e8eab`
(NCM always-drain and panic diagnostics), retaining both selection-doc changes.
At rebased `de849e5633e33a2139d7418640af9412910a739a`, the kernel/image rebuild
with verified unchanged SDK/ports/userland bundles passed. Stock two-NIC DHCP
and the reserved-down → same-role binding/DHCP transition passed again
(`rebased-stock`, `rebased-reserved` records). Binding/ABI/userland code is
unchanged by the rebase. The historical cost samples above were not relabeled
as measurements of this newer main.

After the packages merge, userland was rebased onto `6d15281` (network code
unchanged, final published head `ff34fa67d84d266a963f231c3e387be12b94d13b`), and
parent onto `327b818b`. Kernel/userspace/image rebuilding with matching unchanged
SDK/ports bundles passed at `aa1da108`; stock two-NIC DHCP passed again
(`final-stock`). The packages confirmation change is retained. No jobs remain.

## Linked role with an excluded preferred candidate

Review correction at parent source `2d4d143f` and userland
`1a7722449d93a5ea56b7d070e948560415abc961`. Kernel/userspace/image rebuild
passed with verified unchanged SDK/ports bundles. The same four-CPU/two-VirtIO
configuration now filters exclusions before linked-role ranking and reservation.
Daybreak's no-preference profile and a profile preferring the excluded NIC both
boot: permitted controller 6 binds as `eth0`, gets DHCP, and serves Remote; excluded
controller 5 stays unbound with no logical interface/client. Explicit-only mode
with that exclusion also chooses the permitted peer. Exact excluded-MAC selection
and a required linked role with all candidates excluded both refuse setup; idle
GDB confirms loopback only, before any non-loopback publication.

The explicit-only late-binding case boots with its permitted NIC down while the
excluded NIC is up. GDB sees unbound `eth0`; after `set_link n0 on`, the permitted
controller binds and obtains DHCP on the same role. Thus the late selection path
also filters exclusions. Raw profiles, serial/Remote captures and GDB observations
are under `review-fix-*` in the evidence directory above. These are functional
checks, not new timing or native results.
