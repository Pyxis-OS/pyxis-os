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
  ([keyboard diagnostics](thinkpad-keyboard-diagnostics.md) on its branch).
  `fastfetch`, Lua and Doom all run.
- **Display:** occasional tearing in Doom is expected, because nothing is double
  buffered. It is accepted for now.

Remaining native gaps, in the order the owner chose to address them:

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
  short self-test at boot. On failure, `random` reads report the source
  unavailable. They never return suspect bytes and never fall back to anything
  predictable. The existing rule that timestamps don't substitute for randomness
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
- [ ] Ordinary build and QEMU validation without virtio-rng, plus VirtIO regression.
- [ ] Owner native PXE confirmation of HTTPS/TCP entropy startup.

**Validation:**

- **QEMU:** with `VIRTIO_RNG=0`, `random` reads succeed, TLS setup in `httpfs`
  succeeds and TCP identity reports ready.
- **Native:** the three red startup lines (`httpfs` TLS, the `https` service and
  provider failure) and `net: TCP identity unavailable … (entropy error 6)` are
  gone.
- **Docs:** update [randomness](../devices/randomness.md), and record the
  ChaCha20 follow-up in technical debt.

## 2. Ethernet: passthrough to QEMU, then a driver

The ThinkPad has two Realtek RTL8111-family controllers (`10ec:8168`), and they
differ a lot for passthrough:

| Function | Revision | IOMMU group | Notes |
| --- | --- | --- | --- |
| `05:00.0` | 0x15 | 15, alone | Can be passed through cleanly on its own. |
| `02:00.0` | 0x0e | 12, shared | Multi-function management chip. The group also holds two UARTs (`02:00.1`, `02:00.2`), an IPMI interface (`02:00.3`) and an EHCI controller (`02:00.4`); all five must be passed through together. |

**Owner action first: identify which function is the RJ45 port in use.** With
the cable plugged in, on Fedora:

```sh
ip -br link
readlink /sys/class/net/<interface>/device
```

Ideally it is `05:00.0`. Both are the same chip family, so one driver should
eventually cover both, though revisions differ in detail.

**Host setup on the ThinkPad's Fedora** (*proposed* steps, owner-run):

- Fedora enables the AMD IOMMU by default; the inventory shows AMD-Vi active.
- Bind the chosen function to `vfio-pci`, for example
  `sudo driverctl set-override 0000:05:00.0 vfio-pci`. Undo with
  `driverctl unset-override`.
- While it is passed through, Fedora loses that NIC. Use the AX200 Wi-Fi or the
  dock's USB Ethernet for host networking.
- VFIO pins all guest RAM. Either raise the memory-lock limit or run with
  `MEMORY=2G` for driver work; the default is now 8 GiB.

**Launcher change (*proposed*):** `scripts/run-qemu.sh` has no passthrough option.
Add one explicit option, for example `VFIO_PCI=0000:05:00.0`, which adds
`-device vfio-pci,host=…`. It stays off by default, like the other device options.

**Driver scope (*proposed*), a later task after the setup works:**

- A Caelum driver for the RTL8111 revision behind the chosen port, exposed as
  `net0` beside the existing VirtIO NIC path in [networking](../devices/networking.md).
- Reuse the existing [PCI](../devices/pci.md) claim, BAR mapping and MSI-X
  handling from the xHCI work.
- Develop in QEMU with VFIO, then run natively. Native success also brings the
  remote terminal to the ThinkPad.

## Not covered here

- Storage drivers (NVMe, USB mass storage).
- Double-buffered or tear-free presentation. That would need page flipping
  through a GPU driver; firmware framebuffers have no vsync.
- Suspend and resume.
- TSC as a clock source; it stays the deferred direction in the
  [TSC investigation](thinkpad-kvm-tsc.md).
