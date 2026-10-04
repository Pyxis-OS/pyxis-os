# DHCP qualification

The broadcast endpoint and acquisition milestones were qualified in QEMU,
RTL8111 passthrough and owner-run native cold/PXE boots with the dock attached.
[Parent #374](https://git.internal/PyxisOS/pyxis-os/pulls/374) records native
broadcast delivery, concrete-first same-port unicast and wildcard fallback.
[Parent #380](https://git.internal/PyxisOS/pyxis-os/pulls/380) records two native
DHCP cold boots, gateway ping, DNS and HTTPS. The owner recalls the reserved
`.50`; the native DNS result (`1.1.1.1`) equals fallback and does not distinguish
the source. QEMU acquisition did distinguish lease DNS (`10.0.2.3`). Prior
milestone planning and full task-1/task-2 measurement records remain in Git.

## Lifecycle qualification, 2026-10-04

Lifecycle code: userland `a089c29`, with subsequent review corrections `9880d21`.
Kernel code: parent `b352a1d`; task 3 changes no kernel ABI or packet path.
Ordinary `make -j16 image` passed using
`CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis-` and
`PYTHON=build/hpet-config-venv/bin/python3`. No compiler container rebuild is needed.

The short-lease run used host KVM, four CPUs, 2 GiB, VirtIO net/RNG and an isolated
user/network namespace. The host physical network and SELinux policy were not
changed. A manually configured TAP `dhcp0` had `10.77.0.1/24` and `.2/24`; dnsmasq
supplied only `.50`, a 120-second lease, `/24`, gateway `.1`, and first DNS `.1`.
Commands inside the namespace:

```sh
unshare --user --map-root-user --net bash --noprofile --norc
ip link set lo up
ip tuntap add dev dhcp0 mode tap
ip addr add 10.77.0.1/24 dev dhcp0
ip addr add 10.77.0.2/24 dev dhcp0
ip link set dhcp0 up
dnsmasq --no-daemon --user=root --group=root --interface=dhcp0 --bind-interfaces \
  --no-resolv --no-hosts --dhcp-authoritative \
  --dhcp-range=10.77.0.50,10.77.0.50,255.255.255.0,120s \
  --dhcp-option=option:router,10.77.0.1 \
  --dhcp-option=option:dns-server,10.77.0.1 \
  --address=/lease.pyxis.test/10.77.0.42 --log-dhcp \
  --dhcp-leasefile=build/dhcp-task3-ns.leases
# Separate namespace terminal:
tcpdump -Z root -i dhcp0 -n -U -w build/dhcp-task3-ns-renew.pcap \
  'udp port 67 or udp port 68'
```

QEMU used q35, `-accel kvm -cpu max`, four cores, 2 GiB, the matching Fedora
`/usr/share/edk2/ovmf/OVMF_{CODE,VARS}.fd` pair with a private writable variables
copy, `build/pyxis.iso`, and
`-netdev tap,id=pyxis_net,ifname=dhcp0,script=no,downscript=no` with
`-device virtio-net-pci,netdev=pyxis_net,disable-legacy=on`. An ordinary namespace
GDB connection inspected settings and retained grants; packet capture observed
real server traffic. These are debugger-assisted functional checks, separate
from the unprofiled performance runs below.

Acquisition installed `.50/24`, gateway `.1`, DNS `.1`. A remote `dig
lease.pyxis.test` returned `10.77.0.42` through `.1`. At T1 the captured REQUEST
was unicast with current `ciaddr`, no options 50/54 and valid UDP checksum. The
remote connection survived renewal. Restarting dnsmasq with DNS `.2` produced a
renewal ACK that changed only chosen DNS; a newly launched dig used `.2` and
returned the same answer.

The last ACK at 15:31:15 local time supplied duration 120 s, T1 55 s, T2 100 s.
After stopping dnsmasq, capture showed unicast REQUEST at 15:32:10, broadcast
rebind at 15:32:55 with the same transaction, and a fresh zero-source DISCOVER
at 15:33:15. GDB then showed address/mask/gateway/prefix all zero and DNS
`1.1.1.1`. Restarting dnsmasq led to DISCOVER/OFFER/REQUEST/ACK and restored
`.50/24`, gateway `.1`, DNS `.2` without reboot. Existing TCP listeners follow
normal address invalidation; DHCP does not restart those services.

GDB after handoff found nine remaining maintenance/output grants and no input,
launcher or filesystem roots. The second boot used final code `9880d21` with
no DHCP server. The Development shell reached its prompt with the offline
acquisition diagnostic. It was exited manually before the server was started. The same maintainer then
acquired `.50`, and GDB confirmed its renewal sleep and retained nine grants;
the Development terminal showed `exit` followed by the new DHCP report.

NAK, infinite leases, omitted renewal options and fatal authority/clock failures
were reviewed in code, not exercised with injected packets or faults. Native
renewal/rebind/expiry is not yet qualified; prior native results establish
acquisition rather than this lifecycle.

## Matched traffic

Baseline: parent `f0001f0`/userland `ad1d53a`, whose source tree matches merged
`b352a1d`/`ad1d53a`. After: parent kernel `b352a1d`/userland `9880d21`.
Both use the stock DHCP profile, host KVM, four CPUs, 2 GiB, VirtIO net/RNG,
no debugger or packet capture, and these manual commands:

```sh
QEMU_DISPLAY=none MEMORY=2G CPUS=4 ACCEL=kvm VIRTIO_NET=1 TCP_FORWARD=2323:2323 \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd scripts/run-qemu.sh run
# Host loopback sinks, separate terminals:
socat -T10 UDP4-RECVFROM:18080,bind=127.0.0.1,reuseaddr,fork EXEC:/bin/cat
socat -u TCP4-LISTEN:5001,bind=127.0.0.1,reuseaddr,fork OPEN:<capture>,creat,trunc
# Guest remote shell:
ping -c 5 10.0.2.2
ping -c 3 127.0.0.1
udp-send 10.0.2.15 10.0.2.2 18080 "DHCP acquisition baseline"
ttcp -t -n8192 -l8192 10.0.2.2 # three separate runs
```

Baseline TCP seconds: 30.086819, 30.229522, 30.410405; mean 30.242249,
spread 0.323586. Gateway ping returned 5/5 (mean 0.705 ms), loopback 3/3,
and the 25-byte UDP payload returned intact. The final TCP sink contained
67,108,864 bytes; no content hash was checked.
