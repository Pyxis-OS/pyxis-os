# Link selection qualification

The matched feature comparison uses Pyxis `68729a3`, userland `2b23085`, ports `2c1448a`,
and the existing effective Kconfig. Base `1320d89` adds documentation only to that baseline.
After: kernel `3f6c6e2`, userspace `fbc52d0` (integrated in `88a123f`), before
integrating concurrent vi/libc, USB and installer main updates. Network source is
identical between measured userland `fbc52d0` and integrated `084bb7e`; the
subsequent tie-policy review is qualified separately below.
The combined tree at `7b60c95` (userland `084bb7e`, ports `55b6f8e`) passed a
separate full image build and stock QEMU boot: gateway ping returned 3/3 replies,
DNS resolved `duckduckgo.com`, and HTTPS `example.com` returned its page.
Those checks are separate from the matched measurements.
The comparison uses installed QEMU, host KVM, four CPUs, 2 GiB, modern VirtIO
net/RNG, stock user networking and fresh matching raw OVMF variables. Traffic
runs have no debugger, packet capture or simultaneous build.

```sh
make -j16 PYTHON=build/hpet-config-venv/bin/python3 \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis- image
QEMU_DISPLAY=none MEMORY=2G CPUS=4 ACCEL=kvm VIRTIO_NET=1 \
  TCP_FORWARD=2323:2323 OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd scripts/run-qemu.sh run
socat -T10 UDP4-RECVFROM:18080,bind=127.0.0.1,reuseaddr,fork EXEC:/bin/cat
socat -u TCP4-LISTEN:5001,bind=127.0.0.1,reuseaddr,fork OPEN:<sink>,creat,trunc
```

Manual guest commands, through the existing remote terminal:

```sh
ping -c 5 10.0.2.2
ping -c 3 127.0.0.1
udp-send 10.0.2.15 10.0.2.2 18080 "Link selection baseline"
ttcp -t -n8192 -l8192 10.0.2.2 # three separate runs
dig duckduckgo.com
cat https://example.com
```

| Observation | Before | After |
| --- | --- | --- |
| Gateway ping | 5/5, mean 0.325 ms | 5/5, mean 0.326 ms |
| Loopback ping | 3/3, mean 0.263 ms | 3/3, mean 0.231 ms |
| UDP echo | 23 bytes, exact payload | Same payload, exact echo |
| TCP, 64 MiB | 2.129, 2.128, 2.113 MiB/s | 2.129, 2.100, 2.129 MiB/s |
| DNS and HTTPS | Both passed | Both passed |

Before TCP elapsed times were 30.064773, 30.068618 and 30.292755 seconds;
mean throughput 2.123 MiB/s, range 2.113–2.129. The sink contained 67,108,864
bytes after the final transfer; each guest run reported that same byte count. This compares transport behavior, not a
boot-latency benchmark. After elapsed times were 30.065820, 30.474925 and
30.056134 seconds (mean 30.198960); before mean was 30.142049 seconds, a
+0.189% change smaller than either sample spread (0.228 s before, 0.419 s after). This does not establish a
throughput change. Small ping samples likewise include scheduling variation.
The initial automatic selection adds bounded boot work;
background waiting remains idle except for a 100 ms observation interval.

## Selection checks

Interactive checks use the same kernel/userland code and four-CPU, 2 GiB host
KVM, with two modern VirtIO devices and separate user backends. QEMU's monitor
controls carrier with `set_link nic_a off/on` and `set_link nic_b off/on`;
functional runs use GDB and are separate from the traffic measurements.

With both links up, default ranking selected ID 5 rather than ID 6. The retained
list was reverse ID order, confirming that selection uses IDs rather than list
position. The unselected controller remained inactive with device status `0x0b`
(DRIVER_OK clear). Dropping the bound link retained ID 5 and IPv4 while the other
linked controller remained inactive. After restoring link, a fresh remote
connection returned 3/3 gateway replies; the earlier connection closed during
link loss, so this does not claim preservation of every TCP stream.

A private profile preferring the generated MAC on ID 6, including a duplicate
preference entry, selected ID 6 while both links were up and acquired DHCP.
Preference therefore precedes the ID tie-break, and duplicate entries are accepted.

With both links initially down, the development shell showed “no linked net0
yet; continuing offline while waiting” and its prompt. READ enumeration returned
ID 5 with COMPLETE/PREPARED/CARRIER_KNOWN and no LINK_UP (`flags = 7`), net0
remained unbound, and both device statuses stayed `0x0b`. Bringing unpreferred
ID 5 up while preferred ID 6 stayed down reached BIND immediately, rather than
waiting for the preferred port. At that natural BIND breakpoint, removing ID 5
carrier through the QEMU monitor made BIND return CALL_UNAVAILABLE with
DRIVER_NONE still bound. No kernel/user request data was altered. Bringing ID 6
up afterward bound it in the retained background owner. This first run exposed a
VirtIO activation bug: QEMU updated STATUS while DRIVER_OK was clear without
incrementing config_generation, leaving the driver's boot-time link-down cache
unchanged after activation. Fresh enumeration and precommit checks saw carrier,
but ordinary TX remained unavailable. Activation now forces one bounded stable
configuration sample before queue notification. The corrected delayed run
started with both links down, remained unbound after
the foreground budget, then immediately chose unpreferred ID 5 when it gained
link while preferred ID 6 stayed down. Its fresh cache reported up, DHCP acquired
`10.0.2.15/24`, and a remote session returned 3/3 gateway replies. Userspace also
keeps setup pending if the bound controller remains prepared while active device
configuration stabilizes; permanent activation failure retains the binding and
stops setup. That transient generation path was inspected, not injected.

Final kernel `3f6c6e2` and userspace `c3379a2` also booted with two modern
VirtIO controllers sharing a generated MAC. Exact selection chose ID 5 and
acquired DHCP despite duplicate identities. Explicit MAC ambiguity remains the
existing inspected unique-match rule.

With a single `virtio-net-pci,status=off` device, enumeration returned
COMPLETE/PREPARED with no CARRIER_KNOWN/LINK_UP (`flags = 3`). Automatic setup
remained unbound and the local shell displayed the pending/offline message.
The same STATUS-less device booted with a private `driver = "virtio", dhcp = true`
profile, bound VirtIO with assumed-up carrier and acquired `10.0.2.15/24`.
A link-selected static profile started with carrier down and remained unbound
with zero IPv4 after the foreground budget. Restoring carrier applied the authored
`10.0.2.15/24` with gateway `10.0.2.2`; a remote session connected and returned
3/3 gateway replies. Static configuration stayed applied after the waiting setup
process completed.

Before the tie-policy review, the built-in RTL8168h/XID `541` booted through existing VFIO host
function `0000:05:00.0`, with `VIRTIO_NET=0`, four CPUs, 2 GiB, KVM and VirtIO
RNG. Host driver binding and unlimited memlock were already configured; the
launcher changed neither. Before binding, READ returned RTL family and flags
`0x7` (prepared, known carrier down, complete inventory) with DRIVER_NONE.
Selection waited for reported carrier, then activated RTL and DHCP acquired the
router reservation `192.168.0.50/24`, gateway `192.168.0.1`.
A remote session returned 3/3 gateway replies (mean 0.589 ms), DNS resolved
`duckduckgo.com`, and HTTPS `example.com` returned its page. A further debug boot
included both RTL and VirtIO. Before initial enumeration, both remained inactive;
VirtIO STATUS reported up and RTL PHY status reported link. Enumeration exposed
RTL ID 4 with complete/prepared/known/up flags `0x0f`; the default profile selected
RTL ID 4 rather than VirtIO ID 6. The debugger pause allowed the physical PHY to
settle, so this is a both-linked selection check, not a claim that ordinary boots
wait for an unlinked RTL port ahead of already-linked VirtIO.

Native cold/PXE qualification has not been run for this change. Existing RTL qualification with the dock attached
does not establish support for dock-facing DASH XID `502`.

## Tie-policy review

Userland `b4d0bca` removes the driver-family rank: the profile preference list
comes first, then ascending controller ID (retained PCI inventory order). An
ordinary full image build passed with parent `01fa067` and this userland revision.
A fresh four-CPU, 2 GiB KVM boot used the stock link/DHCP profile with no `prefer`
and two modern VirtIO NICs at `00:03.0` and `00:04.0`, each with a separate user
backend. At the natural BIND breakpoint, both devices reported STATUS `0x1`
(link up) and remained inactive. Selection chose ID 4 at `00:04.0` over ID 5 at
`00:03.0`; BIND returned CALL_OK and retained ID 4. This also confirms that the
current retained inventory is reverse scan order, not ascending PCI address.
DHCP and the remote terminal worked through the selected NIC; gateway ping
returned 3/3 replies and DNS resolved `duckduckgo.com` using `10.0.2.3`.
The matched traffic samples above precede this policy-only follow-up.
