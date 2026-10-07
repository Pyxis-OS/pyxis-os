# Remote debugging without serial

Pyxis supports native PXE bring-up with a readable kernel log, reverse remote
terminal and independent UDP panic capture. These paths remain useful when a
display driver leaves the screen blank. The milestone completed on 2026-10-07
with QEMU checks and owner-run ThinkPad qualification.

## Capture and connect

Build the host tools:

```sh
make -C tools remote log
```

For a generated boot image, enable both paths:

```sh
make image REMOTE_BEACON=t14 LOG_UDP=1
```

An existing PXE entry can instead append `remote.beacon=t14 log.udp=1` to its
kernel command line. This needs no kernel or archive rebuild. The configured
Remote space and normal network setup must exist; installed boot defaults have
no Remote space. See [reverse connections](../userland/remote-terminal.md#reverse-connections)
for Limine macro handling and the trusted bootstrap handoff.

In one host terminal, start the log receiver before boot:

```sh
build/tools/pyxis-log > caelum.log
```

In another, advertise the named reverse terminal:

```sh
build/tools/pyxis-remote --listen t14 0.0.0.0 2323
```

Boot Pyxis with both host tools waiting.

Permit inbound UDP 2325 for logs and the chosen host TCP port for the reverse
terminal. The host advertises on UDP 2324; the guest connects to its sender's
address when the case-sensitive name matches. The name is not secret or an
identity check. If the LAN is not on the host's default route, give
`pyxis-remote` a directed subnet broadcast with `--beacon-address IPv4`.
`pyxis-log --source MAC` filters the advertised source MAC; `--bind IPv4`
selects a numeric local bind address. Neither filter authenticates a sender.

The host terminal accepts one session and exits when it ends. Restart it to
advertise again. The guest terminates the old execution group and waits for its
cleanup before rediscovering, so an abrupt host disconnect can be followed by
a fresh session. Reverse mode uses the existing terminal framing, shell,
foreground interruption, machine mode and verified file-transfer path.
Without `remote.beacon`, the Remote daemon keeps its ordinary TCP listener.

QEMU user networking supports reverse beacons through the documented
[UDP forward](../userland/remote-terminal.md#reverse-connections). It does not
deliver the kernel's limited-broadcast log traffic to a host receiver; use a
TAP/bridge network with the appropriate [net0 profile](../devices/networking.md#boot-configuration-and-use)
for UDP capture.

## Read retained logs

In a local or remote shell:

```sh
log
log -f
```

`log` prints a captured snapshot and exits. `log -f` first prints retained
history, then follows new text; Ctrl+C ends following. Caelum retains `klog`,
enabled traces and serial-mirrored boot-init output from its first byte in a
static 256 KiB ring, independently of the screen. Configured spaces receive an
explicit read-only `log` grant; readers have independent cursors and report
overwritten lines. Following polls every 100 ms.

The [kernel log reference](../interfaces/kernel-log.md) defines grants,
forwarding, cursors, overflow and synchronization. Ring reads need a working
scheduler; fatal capture may skip retention when a ring lock is held.

## UDP boot and panic output

`log.udp=1` enables kernel broadcast to `255.255.255.255:2325`. Normal replay
starts after the selected net0 driver activates and net0 has an IPv4 address,
from DHCP or static configuration. Until then it leaves the log cursor and
packet sequence untouched. Clearing the address pauses normal sending again.
The ring retains waiting history subject to its capacity. DHCP completion
shows the path to its server forwards; a static address alone does not prove
forwarding readiness.

Fatal output sends immediately after driver activation, with source zero if
unassigned. It bypasses userspace, lwIP and the log locks. The first panicking
CPU irrevocably takes the selected TX path, ending normal networking until
reboot. Enabled ordinary TX capacity is 15/16 VirtIO descriptors or 30/32
RTL8111 descriptors; no extra DMA pages are allocated. Omit `log.udp` to disable
capture (`LOG_UDP=0` for generated images); the kernel option accepts only `1`.

The [UDP protocol and fatal ownership contract](../interfaces/kernel-log.md#udp-capture)
cover line fragments, sequence gaps, duplicate handling and bounded CPU/DMA
handoff. Failed handoff, interrupted activation/reset, carrier loss, stalled
hardware or packet loss can prevent delivery. Device-owned storage is never
reused after a fatal timeout, and remains until reboot. Panics before NIC
activation have no UDP path. The ring is volatile and does not survive reboot;
there is no acknowledgement or retransmission, and a receiver started late
misses earlier datagrams.

Reverse sessions and log broadcasts are unauthenticated and unencrypted.
They expose shell authority and kernel addresses on the trusted development
LAN. [Technical debt](../technical-debt.md#kernel-log-retention-and-lan-visibility)
records disclosure, capacity and recovery revisit points. DASH serial-over-LAN
stays parked because its setup is fragile; the
[ThinkPad findings](../wip/thinkpad-next-steps.md#4-parked-dash-serial-over-lan-for-boot-logs)
remain available if it is revisited.

## Qualification

On 2026-10-07, the owner qualified reverse connection discovery and `log -f`
from the first boot line on the ThinkPad over PXE
([#471](https://git.internal/PyxisOS/pyxis-os/pulls/471)). Disabled UDP capture
and a deliberate RTL8111 panic line passed with a throwaway, unmerged panic
build ([#476](https://git.internal/PyxisOS/pyxis-os/pulls/476)). After normal
replay was gated on IPv4, a plain PXE boot of main `fcf142e` with `log.udp=1`
delivered the complete boot log from packet 0 without gaps
([#480](https://git.internal/PyxisOS/pyxis-os/pulls/480)). No panic-trigger option
or key binding is part of the shipped kernel.

Manual four-CPU, 2 GiB Q35/VirtIO nested-KVM checks covered retained reads and
following, natural ring overflow, reverse-name matching, session cleanup,
file-upload verification and the ordinary listener. UDP checks covered BSP/AP
panics, a held log-lock owner, interrupted TX publication and disabled output
([#474](https://git.internal/PyxisOS/pyxis-os/pulls/474)). With delayed DHCP,
normal cursor and sequence stayed at zero; after assignment, packets 0–72
arrived and all 4397 bytes matched an ordinary `log` read
([#478](https://git.internal/PyxisOS/pyxis-os/pulls/478)).

Three matched, unprofiled 16 MiB `ttcp` sends used the default DHCP profile and
QEMU user networking, with `CPUS=4 MEMORY=2G ACCEL=kvm VIRTIO_NET=1` and the
same compiler flags. Guest timing included closure; a host TCP sink checked
each byte total. Remote `log` connect/read/teardown used `/usr/bin/time -p`
at 0.01-second reporting resolution.

| Revision and UDP mode | TCP MiB/s, three samples | Remote log seconds |
| --- | --- | --- |
| Main `800f979`, before implementation | 1.293, 1.299, 1.294 | 0.03, 0.02, 0.02 |
| `8a4b944`, disabled | 1.224, 1.327, 1.352 | 0.03, 0.03, 0.02 |
| `8a4b944`, enabled, before the IPv4 gate | 1.270, 1.286, 1.284 | 0.02, 0.03, 0.02 |

The enabled mean was about 1.2% below baseline, within the broader disabled
variation. These end-to-end nested-VM measurements do not establish isolated
kernel cost, native throughput or panic latency. The detailed measurement
record is preserved in the #474 changes. macOS listener behavior, stalled-NIC abandonment
and RTL's ambiguous-slot duplication remain unqualified; native success does
not turn best-effort delivery into a guarantee.
