# NVMe driver task 1 qualification

[Accepted scope](../../../wip/nvme-coexistence.md).
No native run or native write is authorized by this record. The first native
boot is read-only and is staged by the orchestrator through Luna.

## Before-code control

Revision `a617bcd74fcb875b3ddae8ad1ceaaeabae698f9e`, ordinary default/info image,
LLVM 23.1.3 builder `pyxis-llvm23.1.3-49e2c1a`. Verified matching SDK/ports payloads;
userland rebuilt at its pinned revision. The unchanged kernel and image build
passed. Sealed ELF/ISO/initrd and hashes remain in local `build/nvme-control/`.

Host QEMU 10.2.2 (`qemu-10.2.2-1.fc44`), Q35/nested KVM, max CPU, four CPUs,
512 MiB, fresh OVMF variables with the host code/vars pair, standard VGA,
relative PS/2, VirtIO RNG. One disposable 32 MiB NVMe disk uses 512-byte blocks,
`mdts=6,max_ioqpairs=4,msix_qsize=1`; a separate 64 MiB modern VirtIO block disk
uses one queue. Both are regular raw files with writeback caching. No host block
device or native controller is attached.

The stock ISO boot passed without the Fedora-VM AHCI crash. NVMe appears in PCI
but has no block service on the control. GDB observed one block device: VirtIO,
131,072 blocks of 512 bytes, 64 KiB transfers, eight slots, writable/flush flags,
three published and three completed requests. GPT reported absent on that empty
VirtIO disk. The Development shell was reached by tab navigation and inspected
in a QEMU capture. No disk data was written by this exercise. QEMU was stopped;
no debugger remains attached. These are correctness controls, not native timing
or physical power-loss evidence.

## QEMU and debugger qualification

The added driver uses the same compiler, CPU/device configuration and ordinary
info logging as the control above. No host device or passthrough was used.
Local captures, debugger transcripts and disposable raw files remain under
`build/nvme-read/`, `build/nvme-write/`, `build/nvme-profiles/` and
`build/nvme-policy/`; none is committed. The controller profile offered one or
four I/O pairs; Caelum always created **one** pair, as scoped. Both used
`mdts=6` (64 minimum 4 KiB pages), with the driver's 64 KiB cap.

Read qualification preceded enabling write publication. Manual GDB inspection
and calls on the BSP with IF=0 established:

- A 32 MiB, 512-byte namespace reports 64 KiB transfers and eight slots, read-only
  with the option absent. Eight simultaneous 64 KiB reads succeeded and matched
  all eight host-prepared byte patterns. These exercise the driver's PRP lists for the 15 subsequent
  pages. A ninth request returned FULL, write/flush returned READ_ONLY,
  and a range crossing the last LBA returned INVALID. An undersized collection
  refused without consuming the result; the later full collection succeeded.
  Recollection of a consumed ticket refused. Its entire raw-file hash remained
  unchanged.
- With `nvme.write=1` on a separate scratch namespace, eight 64 KiB writes captured
  the admission bytes despite later caller-buffer mutation. Host read-back
  matched all regions. A queued write → flush → read returned the second pattern;
  all 14 published commands completed, queue descriptors were zero and consumed
  submission sequences equalled posted sequences. Published write bytes were
  589,824 and write failure remained clear. Queued-read abandonment succeeded;
  its old ticket and consumed tickets refused collection.
- A 4 KiB-block neighbour remained READY while a controller with active NSIDs
  1 and 3 was reported UNSUPPORTED. Its setup resources stayed retained.
- A mixed registry retained VirtIO as ID 1, the healthy NVMe with its sole
  active NSID **7** as ID 2, and the multi-namespace controller as unsupported
  ID 3. A 64 KiB read from NSID 7 matched the fixture; its default write refused.
  Global IDs were assigned before preparation lookup, avoiding temporary-ID
  overlap with leading VirtIO entries. VirtIO geometry/write flags were intact.
- Before clean reset, GDB observed `shutdown_done=true`, `CALL_OK`, admission
  sealed, CC=`0x464001` and CSTS=`0x9`: SHN normal, EN retained and SHST complete.
  Firmware/Limine and the shell then restarted.
- The unqualified controller produced real clean-restart and clean-power-off
  refusals, without fault injection. The local overlay showed the failure and
  unsafe warning with Cancel selected; GDB confirmed the warning was presented.
  Fresh **Force reboot** restarted, and **Force off** ended QEMU. Both logged
  storage-check bypass. No unconfirmed clean reset or power-off occurred. This
  exercises the refusal escape path, not a physical controller timeout.

The first debugger attempt used GDB's ordinary injected return on an NX kernel
stack and caused a debugger-induced panic. Subsequent calls used an existing
executable return breakpoint, entered outside IRQs, and allowed APs to run for
shared locks/VM handoffs. A separate mistaken direct call without IF=0 hit the block contract assertion.
Neither tooling mistake is counted as a driver check.

## Raw authority and a prepared NVMe boot disk

Three disposable 1 GiB namespaces were attached to the unchanged stock installer
with `boot.install=1 nvme.write=1`. The fixtures were an all-zero file, a healthy
foreign GPT containing a Linux partition, and an otherwise zero file with a byte
set at 1 MiB. Target IDs followed observed PCI order, not attachment order.

After typed `wipe`, the foreign GPT returned `cannot claim disk (status 2)`.
The interior-data fixture also returned status 2 despite the installer's
header-based blank classification. Both full-file hashes remained unchanged.

An initial 4 KiB-stack-buffer verifier timed out the 1 GiB zero scan at its
existing 30-second deadline, returning status 17 and writing nothing. Switching
to one bounded heap scratch buffer of at most 64 KiB preserved the same proof
and deadline and admitted the fixture. Within the first 20.6-second observation
interval it had also formatted the pool/GPT and begun copying programs; this is
an upper-bound observation, not an isolated scan timing benchmark.

The installer wrote and verified all 45 programs (2,210 payload files), then
refused its ESP partition claim with status 2, as task 1 requires. It accurately
reported a partial installation. A clean power-off flushed the retained pool
and confirmed shutdown on all three controllers. The target logged 200,906,240
published write bytes; SMART write-unit rounding corresponded to those writes.
The two refusal fixtures still matched their original hashes.

After QEMU exited, a host-built FAT32 ESP containing the matching pinned Limine,
Caelum and archive was placed **only in that expendable fixture's ESP**. Its
menu bound the freshly created GPT GUID and explicitly enabled `nvme.write=1`.
With no ISO or other disk attached, OVMF/Limine booted it directly through NVMe.
Caelum discovered its healthy GPT, mounted the installed npfs volumes, selected
the verified bin revision and reached the `home://` shell. This proves boot and
pool operation on a prepared NVMe disk; it does not claim the current installer
can finish NVMe installation or authorize native ESP writes.

From that installed shell, `echo nvme-ok > nvme.txt`, `sync nvme.txt .` and
`cat nvme.txt` succeeded. A clean restart confirmed shutdown before reset;
`cat nvme.txt` after the fresh boot printed `nvme-ok`. A final clean power-off
also confirmed shutdown. Separately, GDB abandoned a published READ before
software completion validation: the slot remained ACTIVE/device-owned and
abandoned, then became FREE only after the worker checked its CQ completion.
No write failure was latched by abandoning that read.

## First native boot: read-only

The orchestrator asks Luna to stage the PR head; Codex does not stage or boot the
ThinkPad. Build with the existing compiler and unchanged dependency pins:

```sh
make -j16 image LOG_LEVEL=info LOG_UDP=1 DISPLAY_TIMING=off
```

Use the ordinary PXE entry, `init=boot://boot-init.pxe display.timing=off log.udp=1`.
Keep `nvme.write`, `boot.install` and `mount.disk` **absent**. The laptop stays on
AC; the UDP log on horse captures output. No native write run is authorized.

1. Capture boot's `nvme PCI ... SMART`, namespace preparation, `nvme N: ...
   read-only`, and `GPT: device N`/partition lines. Require data writes disabled,
   exactly one active namespace, expected 512-byte geometry/MDTS cap, and healthy
   primary/backup GPT matching the owner's current Fedora partition layout.
   Setup failure is a result to report, not permission to enable writes.
2. Reach the local shell, move between tabs and run ordinary boot/RAM commands.
   The live configuration binds no internal pool or raw installer grant.
3. Use Ctrl+Alt+Del for a clean reboot. Capture the second SMART snapshot and
   `normal shutdown confirmed` before firmware restarts. Written-data units must
   not advance during this Pyxis read-only session. If clean power is refused,
   preserve the log; Cancel remains available, or the owner may explicitly
   choose Force reboot after its warning. That escape is unsafe by definition.
4. Compare Identify/GPT/SMART with current Linux read-only observations. Compare
   actual sector bytes separately before claiming native payload equivalence;
   matching GPT CRCs/layout is metadata-read evidence, not a full-disk comparison.
   Keep serial/MAC fields out of committed records. Attach a masked log and owner
   observations to this record; hardware timing and wear are not recorded.

**Native result (2026-10-11, owner, ThinkPad, read-only).** The disk read at its
expected capacity, the three-partition GPT matched Linux's view and SMART was
read. The unsafe-shutdown count did not change across a Pyxis reboot, so the
shutdown notification works. Sector-byte equivalence was not compared. The raw
log stays outside the repository.

## Vela NVMe 2.0 scope — assigned 2026-10-14

Implementation authorized after both defaults were accepted on 2026-10-14. The owner reported a SanDisk SN7100S
(`15b7:5044`): VER `0x20000`, MDTS 7, CTRATT `0x20014`, CNTRLTYPE 1,
OACS `0x17`, ONCS `0xdf`, NN 1. Linux used 16 queues and a 32 MiB HMB.
These are owner-supplied Linux facts, not a Pyxis qualification. No serials
are recorded. The current version upper bound rejects it before Identify.

The owner's admission principle is accepted: NVMe 1.3 or newer, with no version
ceiling; qualify the particular register/command contract used. Writes remain
off unless `nvme.write=1`. The supported geometry, one active namespace,
64 KiB/MDTS request bound, DMA retention, clean shutdown and forced local power
escape stay unchanged.

### Implementation contract

| Area | Bounded handling |
| --- | --- |
| PCIe registers, queues, PRPs, Read/Write/Flush | Their used layouts remain compatible. Keep BAR/MSI-X validation, one queue pair and captured slots. No additional queues, data commands or power features. |
| CAP.CSS / CC.CSS | Prefer NVM-only `CC.CSS=0` when CAP.CSS bit 0 exists. Otherwise use `6` only when bit 6 exists and NVM is proven. Bit 7 permits an admin-only configuration; it is not a veto when NVM also exists. Never select `7` or publish an admin-only controller as a block device. |
| Identify / namespace type | Keep CNS 1 controller, CNS 2 complete active list and CNS 0 geometry. Require I/O CNTRLTYPE 1; unreported type 0 remains allowed only before 1.4. Where multiple command sets are advertised, parse bounded CNS 3 descriptors and require one CSI descriptor identifying NVM (CSI 0); successful CNS 0 alone does not exclude ZNS. CNS 5/6 describe optional protection/other command limits not used by this metadata-free, PI-free Read/Write/Flush path. |
| IOCSS profile | With CSS 6, CNS `1Ch` vectors plus Get Features `19h` must show that the selected profile enables NVM. The accepted rule refuses an unsuitable profile without altering it. No namespace formatting or attachment. |
| Namespace layout | Decode the six-bit FLBAS index and bounds in the supported table rather than aliasing a newer format to its low four bits. Do not refuse solely because additional formats are advertised. Still require selected metadata size zero, no PI, 512/4096-byte blocks and dense valid capacity; check the thin-provisioning flag as well as capacity. |
| CAP.CRMS / CC.CRIME | Keep ready-with-media (`CRIME=0`), and qualify the advertised readiness modes. Use inherited CRIME for the firmware-ready wait. After writing the disabled configuration, re-read CAP: changing CRIME can legitimately change CAP.TO, so runtime equality must compare the resulting snapshot. CRTO supplies the ready-with-media enable interval; disable/reset uses CAP.TO. The accepted software ceiling is 127.5 seconds. |
| Reset, shutdown, HMB | Confirm firmware halt before BAR sizing/DMA handoff. Controller reset clears inherited HMB enablement. Do not allocate or enable HMB. Existing drained queue deletion, normal CC.SHN/CSTS.SHST acknowledgement and retained ownership on failure apply; a completed shutdown alone would not authorize freeing a future enabled HMB. |

No specification or external driver implementation is vendored. References:
[NVMe Base 2.0c](https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-2.0c-2022.10.04-Ratified.pdf)
§§3.1.3, 3.5–3.7, 5.17, 5.27.1.10, 5.27.1.21 and 8.9;
[PCIe transport 1.0c](https://nvmexpress.org/wp-content/uploads/NVM-Express-PCIe-Transport-Specification-1.0c-2022.10.03-Ratified.pdf)
§3.1.2; [NVM command set 1.0c](https://nvmexpress.org/wp-content/uploads/NVM-Express-NVM-Command-Set-Specification-1.0c-2022.10.03-Ratified-1.pdf)
§§3.2 and 4.1.5. Specifications © 2008 to 2022 NVM Express, Inc. ALL RIGHTS RESERVED.
[Linux v6.19 readiness and namespace handling](https://github.com/torvalds/linux/blob/v6.19/drivers/nvme/host/core.c)
provides a separate implementation cross-check, not code to copy.

### Owner decisions — accepted 2026-10-14

1. **Readiness waits are capped at 127.5 seconds**, with the existing 500 ms
   minimum. Honor the advertised interval, including CRTO, within that ceiling.
   A larger advertised maximum does not itself refuse a compatible controller:
   try within the budget and report unavailable/retain ownership if it times out.
   This covers initial firmware-ready/enable/disable waits; five-second admin/I/O
   and bounded clean-power deadlines are unchanged.
2. **Validate the current IOCSS profile; refuse if it excludes NVM.** Prefer
   CSS 0 where offered. With CSS 6, require CSI 0 and NVM in the selected profile
   vector before geometry/I/O publication. Never change the profile.

HMB remains disabled and writes remain off by default. HMB is optional for this
command profile; its possible DRAM-less performance consequence is recorded as
[debt](https://git.internal/PyxisOS/pyxis-os/issues/1488).

### Qualification plan

Capture the unchanged driver on `e5e995f0` (only target documentation differs
from the planned `75d9b7b7` control) before driver edits. Use stock local
QEMU 10.2.2, Q35/KVM, four CPUs, a disposable 512-byte-block NVMe GPT image,
MDTS 7 and one/several offered queues; keep `nvme.write=0`. Exercise ready/GPT
reads and clean shutdown and inspect selected CC/CAP, namespace and read-only
block flags with GDB. Repeat matched functional checks after the change; no
performance claim or new benchmark/test framework.

[QEMU's documentation](https://github.com/qemu/qemu/blob/v10.2.2/docs/system/devices/nvme.rst)
reports NVMe 1.4. Its [source](https://github.com/qemu/qemu/blob/v10.2.2/hw/nvme/ctrl.c)
fixes VS at `0x10400` and exposes no version property; local `-device nvme,help`
confirms this. Stock QEMU is regression coverage, not an NVMe-2.0 emulator.
Do not claim that changing only VS would qualify 2.0 readiness. No QEMU source
fetch/build or fault injection is proposed for this task.

The orchestrator/Luna stage vela's read-only native boot: default info logging,
`nvme.write=0`, verify namespace ready and GPT read, report the capability/profile
refusal reason if any, and retain the existing SMART/normal-shutdown evidence.
No native Write, Flush, format, HMB allocation or installer apply is authorized.
The power-button task remains separate and follows this NVMe task.

### NVMe 2.0 implementation qualification — 2026-10-14

Control: unchanged driver on `e5e995f0`; implementation: `f7a940e3`. Default/info
kernel and image builds passed with LLVM builder `pyxis-llvm23.1.3-df87791`,
matching prebuilt SDK/userland/ports from the merged quiet-updater image and
unchanged dependency pins. The builder's missing kconfiglib was supplied from
the installed host module; no container rebuild. No timing measurement.

Four interactive QEMU boots paired control/implementation with
`max_ioqpairs=1` and `4`: Q35, nested KVM, host CPU, four CPUs, 512 MiB,
standard VGA, PS/2, VirtIO RNG, fresh OVMF variables, VirtIO-SCSI ISO boot.
The same disposable 128 MiB GPT disk, 512-byte blocks, MDTS 7 and default write
prohibition were used throughout. Each boot reached Development, read the
healthy primary/backup GPT and confirmed normal shutdown with zero published
write bytes. The disk's SHA-256 remained unchanged.

Implementation GDB inspection: VS `0x10400`, CAP `0x4008200f0107ff`,
CC `0x460001` (CSS 0, CRIME 0), 7.5 s CAP.TO, one queue pair, eight slots,
64 KiB request bound, writable/flush flags false. CNS 3 supplied NVM CSI on
this command-set-capable controller. A 64 KiB read through the PRP list completed
and matched the host's first 64 KiB; WRITE and FLUSH admission returned
READ_ONLY. Queue publication/completion counts matched. Debuggers and QEMU
were stopped. Raw logs/captures remain local under `build/nvme-v2/`.

Stock QEMU does not exercise CRTO readiness or CSS-6-only profile admission;
these received source review, not runtime qualification. Vela namespace readiness
passed the owner run below; GPT and clean shutdown remain unreported. The
read-only packet is `/shared/present/vela-nvme/`, with
`README.md`, owner `/dev/sdb1` copy/restore commands and verified `SHA256SUMS`.
Its never-merged diagnostic branch `diag/vela-nvme-on-75a8aaac` at `65fa0dec`
contains stick base `75a8aaac`, its existing #847 boot fix (`78646104`), and
only the NVMe implementation commit. No #848 USB changes or newer ABI/pins.
It is an info kernel-only build; keep the stick's initrd and Limine, enforce
`nvme.write=0`, and expect USB unavailable and rescue execution. Native evidence
required: namespace read-only readiness, Fedora's GPT layout and confirmed clean
shutdown. No internal disk writes or native staging by Codex.

## Vela native readiness — 2026-10-14

The owner booted the combined Vela power-button diagnostic kernel and reported:
`nvme 1: namespace 1 ready, 2000409264 blocks of 512 bytes, transfer=65536 slots=8 read-only`.
This confirms the NVMe 2.0 controller/namespace admission on the SanDisk SN7100S
and read-only readiness (1,024,209,543,168 bytes). No native writes were enabled.
GPT read/layout and confirmed clean shutdown were not reported by this run and
are not claimed as qualified.
