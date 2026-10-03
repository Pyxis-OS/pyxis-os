# ThinkPad bring-up: next steps

Status: **owner direction, 2026-10-03.** The order and the entropy approach under
[owner decisions](#owner-decisions) are accepted. Items marked *proposed* await the
owner. This document assigns no implementation by itself.

## Where native bring-up stands

On the [ThinkPad T14 Gen 1 AMD](../targets/t14-gen1-amd/notes.md), booted over PXE,
Caelum reaches an interactive shell on all 12 CPUs with the full 32 GB:

- **Clock:** the 32-bit HPET runs software-extended. The owner's `date` check kept
  time from 14:12 to past 14:24, more than two of the counter's ~300 s wraps, with
  Doom running in between. A longer run is planned.
- **Keyboard:** works with the scan-set read-back made optional
  ([keyboard diagnostics](thinkpad-keyboard-diagnostics.md), merged in #343).
  `fastfetch`, Lua and Doom all run.
- **Display:** occasional tearing in Doom is expected, because nothing is double
  buffered. It is accepted for now.

Remaining native gaps, in the order the owner chose to address them (a
[reverse remote terminal](#3-later-idea-reverse-remote-terminal-with-broadcast-discovery)
is recorded as a later idea):

1. **No entropy source.** QEMU's virtio-rng doesn't exist on hardware, so
   `random` reads fail. TLS setup fails (`httpfs: TLS setup failed (native
   entropy failure …)`), and so does TCP identity (`entropy error 6`).
2. **No Ethernet driver,** so there is no network or remote terminal on the
   ThinkPad.

Storage (NVMe or USB mass storage) is separate work and is not covered here.

## Owner decisions

1. **Entropy first,** as the smaller, self-contained task. Ethernet follows.
2. **The entropy source is the CPU's RDSEED, with RDRAND as fallback,** used
   directly, with the same trust model as today's virtio-rng (see
   [randomness](../devices/randomness.md)). A kernel ChaCha20 generator seeded from
   these sources is the accepted *later* direction, not part of the first task.
3. **Ethernet is developed by passing the ThinkPad's NIC through to QEMU** with
   VFIO on the ThinkPad's own Fedora, so the driver can be written and debugged in
   a VM before running natively.

## 1. Entropy: RDSEED and RDRAND

The Ryzen 5 PRO 4650U advertises both `rdseed` and `rdrand` (inventory CPU flags).
QEMU's `-cpu max` passes the host's instructions through, so the work can be
developed and validated in QEMU with `VIRTIO_RNG=0`, then confirmed natively.

**Assigned implementation, 2026-10-03:** CPU entropy with virtio-rng preferred
when present, as confirmed by the owner after #345 merged. Ethernet remains later
work.

**Requirements:**

- **Detect each instruction through CPUID before use:** RDRAND is
  `CPUID.01H:ECX[30]` and RDSEED is `CPUID.(EAX=07H,ECX=0):EBX[18]`.
- **RDSEED first, RDRAND as fallback.** Both report "no data" by clearing the
  carry flag. Retry a bounded number of times; never spin without limit.
- **Health checks, fail closed.** Some Zen 2 firmware returned all-ones from
  RDRAND while still setting carry; Linux tests for this at boot. Reject an
  all-ones or all-zero word, and a word identical to the previous one, and run a
  short self-test at boot. The owner's follow-up decision uses a per-instruction
  latch: a failure disables its instruction until reboot, while a healthy
  survivor can serve. A runtime health failure discards and refills the entire
  current request; a cross-instruction repeat disables both. Without a healthy
  survivor, reads report unavailable. They never return suspect bytes or anything
  collected alongside one, and never fall back to predictable data. The existing rule that timestamps don't substitute for randomness
  stands.
- **Keep the `random` capability unchanged.** Callers see the same interface;
  only the kernel's backing source changes.

**Source selection (owner accepted, 2026-10-03):** use virtio-rng when present,
and CPU entropy otherwise. Preparation or runtime failure of a present VirtIO
device does not select another source. The existing VirtIO ready log remains;
the CPU path reports its ready/self-test result without logging random bytes.

**Implementation checklist:**

- [x] CPUID-gated 64-bit RDSEED/RDRAND, bounded retries and boot/runtime checks.
- [x] Fixed boot source selection, virtio-rng first, CPU only when absent.
- [x] Keep the random ABI, shared slots, deadlines and cancellation behavior.
- [x] Document the hardware trust and ChaCha20 follow-up.
- [x] Ordinary build and QEMU validation without virtio-rng, plus VirtIO regression.
- [x] Owner native PXE confirmation of HTTPS/TCP entropy startup.

**Validation:**

- **QEMU:** with `VIRTIO_RNG=0`, `random` reads succeed, TLS setup in `httpfs`
  succeeds and TCP identity reports ready.
- **Native:** the three red startup lines (`httpfs` TLS, the `https` service and
  provider failure) and `net: TCP identity unavailable … (entropy error 6)` are
  gone.
- **Docs:** update [randomness](../devices/randomness.md), and record the
  ChaCha20 follow-up in technical debt.

### CPU entropy implementation and validation

Branch `bringup/cpu-entropy`, based on merged main `bbf61c0`. Implementation
commit `24dca3130e551f053cea805e6778a0d581a53ac8` adds the source-neutral BSP
service and CPU instruction adapter. No dependency pin, launcher, Ethernet,
userspace source or compiler-container change is part of this task. The pinned
repositories remain fs `810d2af6`, ports `a50ae5cc`, lwIP `a1aadb91`, userspace
`53b6860f`. The [randomness reference](../devices/randomness.md) records implemented
source selection, self-test, retry, repeat-history and failure behavior.

Agent-host validation used GCC 16.2.0 and QEMU 10.2.2 Q35/KVM guests on an AMD
Ryzen 5 PRO 4650U. These are guest results, not native Caelum PXE validation.
All boots used four CPUs (one socket, four cores, one thread per core), 256 MiB,
fresh matching Fedora OVMF variables, the normal info image, xHCI disabled,
VirtIO networking and loopback TCP forward `2333:2323`. Baseline main was built
and booted before implementation; the submitted code was then booted with matched
startup settings. Each baseline and matching post-change configuration was booted
once; the additional CPU feature configurations were also each booted once.
This establishes startup behavior, not a latency or throughput measurement.

Build commands:

```sh
make -j16 image PREBUILT="sdk userspace ports" \
  PYTHON=build/hpet-config-venv/bin/python3 \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis-
# After changing the ABI header comment, rebuild exports and consumers together:
make -j16 image PYTHON=build/hpet-config-venv/bin/python3 \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis-
```

Both passed. The full SDK/ports/userspace build emitted existing vendored sbase
and Doom warnings. The final kernel/image build passed without kernel warnings
or undefined ELF symbols; freshly rebuilt matching bundles were verified on
reuse. Changed-document relative file targets and whitespace checks passed.
An independent read-only review found no concrete ownership/cancellation
regression. No new test suite, fault injection, boot automation or diagnostic
hook was added. The bounded boot self-test is explicitly part of the assigned
entropy requirements.

The baseline launcher command was:

```sh
QEMU_DISPLAY=none MEMORY=256M CPUS=4 ACCEL=kvm \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd \
  VIRTIO_RNG=0 VIRTIO_NET=1 TCP_FORWARD=2333:2323 \
  scripts/run-qemu.sh run
# Repeat with VIRTIO_RNG=1 for the baseline VirtIO path.
```

Post-change boots used the same devices and image with `-gdb
tcp:127.0.0.1:1234`, attached after startup rather than pausing firmware:

```sh
cp /usr/share/edk2/ovmf/OVMF_VARS.fd build/cpu-entropy-vars.fd
qemu-system-x86_64 -machine q35 -accel kvm -cpu max -rtc base=utc \
  -smp cpus=4,sockets=1,cores=4,threads=1 -m 256M \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=build/cpu-entropy-vars.fd \
  -display none -serial mon:stdio \
  -netdev user,id=pyxis_net,hostfwd=tcp:127.0.0.1:2333-10.0.2.15:2323 \
  -device virtio-net-pci,netdev=pyxis_net,disable-legacy=on \
  -cdrom build/pyxis.iso -boot d -gdb tcp:127.0.0.1:1234 \
  -no-reboot -no-shutdown
```

For the VirtIO post-change boot, add
`-object rng-random,id=pyxis_rng,filename=/dev/urandom` and
`-device virtio-rng-pci,rng=pyxis_rng,disable-legacy=on`. The RDRAND-only and
unsupported configurations change only the CPU to `max,rdseed=off` and
`max,rdseed=off,rdrand=off`, respectively; both omit virtio-rng.

| Revision/configuration | Observed startup and inspection |
| --- | --- |
| Baseline `bbf61c0`, no RNG device | Interactive shell; TCP identity entropy error 6; all three HTTPS setup/provider errors visible in Development. |
| Baseline `bbf61c0`, VirtIO RNG | TCP identity ready; Development has no HTTPS entropy errors. |
| `24dca31`, no RNG device, CPU `max` | RDSEED/RDRAND boot self-test passed; TCP identity ready; HTTPS setup has no entropy errors. GDB: `RANDOM_CPU`, both instructions advertised, no health latch, no active caller and all eight slots free. |
| `24dca31`, VirtIO RNG, CPU `max` | VirtIO source ready; TCP identity ready; HTTPS setup has no entropy errors. GDB: `RANDOM_VIRTIO`, CPU instruction state untouched, all eight slots free, DMA outstanding zero, used index seven. |
| `24dca31`, no RNG device, CPU `max,rdseed=off` | RDRAND boot self-test passed, TCP identity ready, HTTPS setup has no entropy errors. GDB confirms only RDRAND advertised and all slots free. |
| `24dca31`, no RNG device, both CPU features off | Boot and shell work; CPU source unavailable, TCP identity entropy error 6 and the expected three HTTPS setup/provider failures. GDB: admission closed, both features absent, all slots free. |

The existing host remote client connected on `127.0.0.1:2333` in the CPU and
VirtIO boots. On the CPU boot, `dig example.com` returned NOERROR with two answers
and exited zero. `cat https://example.com/ > home://entropy-https.html` exited zero
on both sources, exercising an HTTPS connection in addition to provider startup.
An attempted `wc -c` inspection was not available in the image and is not counted
as validation. No guest library, probe or test program was added.

Baseline hashes: kernel
`3bce9f12f0635f42586cfa6dc5c6afa17731e0f4f69f7823a500a807396706dd`, ISO
`bff45aa0fc411607d4e8b8b2ee13703b71334b7e1c7e2ecca76816cd1e817b45`.
Validated implementation hashes: kernel
`5a714e21908d4b5e6aeb6ce1b8a4fcad145570b628ee4b61f0d58f795489156c`, ISO
`3bfc988737bee21bd89b639585ca2faa676e879fa1fd5c5988a204e9d09f0911`.
Local evidence remains in ignored `build/cpu-entropy-*` logs and screenshots.
QEMU, GDB and remote-client processes were stopped after inspection.

Limits: carry-clear exhaustion with both instructions present, suspect-word
rejection, cancellation during generation, and late/failed VirtIO completions
have code-review coverage rather than induced-failure evidence. The ordinary
CPU feature masks check discovery/absence, not simulated bad instruction output.
The health checks do not certify hardware randomness. Remaining owner action:
PXE boot this implementation on the ThinkPad and confirm the HTTPS startup and
TCP identity entropy errors are gone. Ethernet remains separate unassigned work.

### Review follow-up: independent instruction health

The owner chose per-instruction health latches in the #347 review on 2026-10-03.
Implemented in `dae754c7017a9066c34f4956e51cee403a4f8f50`, after integrating
main `ae5ee94` so the reverse-terminal proposal remains intact. Boot tests each
advertised instruction separately; a failing instruction is disabled until
reboot, and the CPU service remains available while another is healthy. At
runtime, zero/all-ones/per-instruction repeats disable only the failing
instruction. The worker clears all staging bytes and resets the current read's
filled count before refilling under its original deadline and cancellation.
An ambiguous cross-instruction repeat disables both. The randomness reference
and technical debt describe this current policy; the earlier `24dca31` record
above describes the original single-latch implementation.

The reviewer requested temporary, uncommitted forcing changes for validation.
Four variants based on `dae754c` were rebuilt and booted with the same four-CPU,
256 MiB Q35/KVM, CPU `max`, no virtio-rng, fresh Fedora OVMF and VirtIO networking
configuration used above. Only instruction outputs in the arch adapter were
forced; the service's checks, staging and queue handling were unmodified.
Each forcing variant was built and booted once. GDB attached after startup.

| Temporary forcing case | Independent expectation and observed result |
| --- | --- |
| RDRAND always returns all-ones with carry set; RDSEED unchanged | RDRAND disabled at boot, RDSEED remains healthy, TCP identity ready. GDB: disabled `{true, false}`, admission open, no active call and all eight slots free. |
| Separate tagged, increasing words from each instruction; the seventh RDSEED draw returns zero | The eight-word self-test passes; RDSEED fails after two further good words. RDSEED alone is disabled, the interrupted request refills from RDRAND, and TCP identity is ready. All four delivered 64-bit TCP key words have the RDRAND tag, with no RDSEED prefix retained. A subsequent remote `dig example.com` returns NOERROR and exits zero, confirming later reads succeed. |
| Both instructions always return all-ones with carry set | Both disabled at boot; source admission closed and TCP identity unavailable with entropy error 6. All slots are free. |
| Tagged words pass self-test; the first runtime RDSEED word succeeds, subsequent RDSEED attempts clear carry, then RDRAND returns that last RDSEED word | The repeat is across instructions rather than within one stream. Both are disabled, the current read fails and TCP identity is unavailable. GDB confirms both latches, closed admission and all four TCP key words still zero. |

For the runtime refill and repeat cases, controlled good words used RDSEED tag
`0x2468ace000000000` and RDRAND tag `0x135790ab00000000`, with an independent
instruction draw count in the low bits. GDB compared the high 32 bits of each
TCP key word against the RDRAND tag, rather than merely checking readiness. In
the cross-repeat case the forced RDRAND word was `0x2468ace000000005`, matching
the preceding RDSEED draw, after 32 carry-clear RDSEED attempts. This distinguishes
the ambiguous shared-failure rule from a per-instruction repeat. These values
are temporary validation fixtures, not entropy sources in the submitted code.

All forcing changes were reverted and both production source files compared
identically with the committed revision. The production image was rebuilt using
the existing matching SDK/userspace/ports bundles. It passed without compiler
warnings or undefined ELF symbols. The clean CPU boot passed self-test and TCP
identity, with both latches clear, all eight slots free, and a successful
`cat https://example.com/ > home://entropy-https.html` through the existing remote
client. A clean VirtIO-priority boot reported TCP identity ready; GDB confirmed
`RANDOM_VIRTIO`, untouched CPU state, no outstanding DMA and all slots free.
The forced images were not published; no forcing hook or test infrastructure
remains. All QEMU, GDB and remote-client jobs were stopped.

Validated production hashes: kernel
`b80ba89d216cb809fe712cbaeba6243cdffba67221d28593834a02de482e7df5`, ISO
`0f42e38f36d617ea067a7a93735e45e1a4fad34697fc90db52dfe8c5e4a1ed4d`.
Local forcing patches, build/boot logs and GDB records are ignored
`build/cpu-entropy-forced-*`; production evidence is `build/cpu-entropy-latches-*`.
An independent read-only review found no concrete latch/refill ownership hazard.
Native PXE confirmation remains pending. These controlled guest checks validate
the failure policy and read provenance; they do not qualify entropy quality or
measure throughput. Cancellation during refill and late/failed VirtIO completions
remain code-inspected rather than induced.

## 2. Ethernet: passthrough to QEMU, then a driver

The ThinkPad has two Realtek RTL8111-family controllers (`10ec:8168`), and they
differ a lot for passthrough:

| Port | Function | Revision | IOMMU group | Notes |
| --- | --- | --- | --- | --- |
| Built-in RJ45 | `05:00.0` | 0x15 | 15, alone | Owner's PXE port; pass through this function only. |
| Dock Ethernet | `02:00.0` | 0x0e | 12, shared | Multi-function management chip. The group also holds two UARTs (`02:00.1`, `02:00.2`), an IPMI interface (`02:00.3`) and an EHCI controller (`02:00.4`); all five must be passed through together. |

**Owner-confirmed mapping (2026-10-03):** the built-in RJ45, also used for
native PXE boot, is `05:00.0`. It is alone in IOMMU group 15, so passthrough targets
that function only. Dock Ethernet is the PCI controller at `02:00.0`, sharing
group 12 with its UARTs, IPMI and EHCI; the same driver family can cover it later
with revision-specific handling. AX200 Wi-Fi is `03:00.0`.

The router reserves a fixed address for the built-in port. Its owner-confirmed
static profile is recorded in the
[driver plan](thinkpad-rtl8111.md#goal-and-machine-configuration). These values
are machine configuration, not a driver contract.

**Passthrough completed (2026-10-03).** The
[NIC passthrough reference](../development/thinkpad-nic-passthrough.md) records
the implemented `VFIO_PCI` launcher, Fedora host setup and successful owner/agent
boots. Pyxis inventories `10ec:8168`, revision `0x15`, without claiming it.

**RTL8111 five-task outline approved (2026-10-03).** The
[milestone](thinkpad-rtl8111.md) records the review's revised order: owner Fedora
capture and Caelum identification, preparation, the VirtIO selection refactor,
RTL8111 I/O, then VFIO/native qualification. The owner directs configuration to
choose the interface; there is no automatic RTL8111 preference. The per-controller
model, dock revision support, ownership details and hardware references remain
proposals pending confirmation. Driver implementation and task 1 have not started.

## 3. Later idea: reverse remote terminal with broadcast discovery

An owner idea from 2026-10-03, recorded so it isn't designed out. It is not
assigned, and it comes after the Ethernet driver.

**Idea.** Reverse the remote terminal's direction. A server runs on the
development host and broadcasts a small UDP beacon about once a second. Pyxis
listens for it, takes the sender's address and connects to the server over TCP.
The session then works as today, but Pyxis never listens for connections and
nobody needs to know its IP address.

**How the discovery works.** The beacon is a UDP datagram to `255.255.255.255`
or the subnet's broadcast address (Ethernet destination `ff:ff:ff:ff:ff:ff`).
Every host on the same local segment receives it, and routers don't forward it.
It carries a protocol tag and version, the server's TCP port and a name. Pyxis
binds the agreed UDP port and waits.

**Owner decision: no authentication for now.** Like the existing
[remote terminal](../userland/remote-terminal.md), reverse mode stays
unauthenticated and unencrypted until Pyxis itself has authentication. A host
that answers gets the configured shell privileges, so this assumes a trusted LAN.
Record it in technical debt with that revisit point when implemented.

**Also accepted by the owner (2026-10-03).** Neither is security:

- **Opt-in only.** Reverse mode starts only when the image or init configuration
  asks for it, never by default.
- **A non-secret name in the beacon,** which Pyxis is configured to match, so that
  with several servers or development machines on one network each Pyxis connects
  to the intended one.

**Prerequisites, from [networking](../devices/networking.md):**

- the Ethernet driver (section 2);
- an address on the LAN. Today only manual static configuration exists; there is
  no DHCP;
- **broadcast reception.** The stack currently discards broadcast and multicast IP
  traffic. DHCP needs the same capability, so the two belong together.

**Interim, without discovery:** reserve a fixed address for the ThinkPad's MAC in
the router, configure it statically in the image, and connect from the host in
the existing direction.

## 4. Parked: DASH serial-over-LAN for boot logs

**Idea.** The second Realtek controller (`02:00.0`, RTL8168ep, XID `502`) is on the
motherboard; the dock only provides its RJ45 jack. It is a DASH management
controller (AMD PRO manageability). Its PCI functions are:
- `02:00.1` and `02:00.2`: two 16550 UARTs. Fedora sees them as `ttyS4` at I/O
  `0x3200` and `ttyS5` at `0x3100`;
- `02:00.3`: an IPMI interface;
- `02:00.4`: an EHCI controller.

If DASH text redirection forwards one of those UARTs over the network, Caelum
could write its log there and give remote early boot logs with no Pyxis NIC
driver. Caelum already only reads this controller's XID and leaves it untouched
(merged #362), so Pyxis would not disturb the management firmware.

**Tried on 2026-10-03, without success:**

- **BIOS:** "DASH support" is enabled, and the BIOS lists the Realtek UEFI UNDI
  driver for `02:00.0`. There is no DASH authentication or network setting.
- **Under Fedora:** TCP 623 and 664 on the port's reserved address are answered
  by Fedora itself (`filtered` with the firewall on, `closed` with it off).
  Nothing on the NIC claims them. Linux's r8169 signals "OS driver active" to the
  DASH firmware for this variant, which may make the firmware step back.
- **In the BIOS and powered off:** no reply on 623 or 664, and `arp-scan` of the
  LAN shows no ThinkPad MAC at any address.
- **AMD DASH CLI v8.0.0 for Linux** is available and lists `textredirection`,
  `usbredirection` and `kvmredirection`. It manages only controllers that are
  already reachable and set up, and has no command for first-time setup.
- **A side observation:** the powered-off laptop woke up on its own during the
  scans. Wake-on-LAN on this NIC is a plausible cause, but this was not
  confirmed.

**Next step, when it's worth the effort:** first-time DASH setup (credentials
and network mode). This likely needs a vendor tool running on the laptop's own
OS, possibly Windows-only. Then run the UART echo test from Fedora during a
text-redirection session, to see which of `ttyS4` and `ttyS5` is forwarded. Not
needed for the NIC driver work.

## Not covered here

- Storage drivers (NVMe, USB mass storage).
- Double-buffered or tear-free presentation. That would need page flipping
  through a GPU driver; firmware framebuffers have no vsync.
- Suspend and resume.
- TSC as a clock source; it stays the deferred direction in the
  [TSC investigation](thinkpad-kvm-tsc.md).
