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
   observations to this record; hardware timing, wear and unsafe-shutdown
   behaviour remain pending until those results arrive.
