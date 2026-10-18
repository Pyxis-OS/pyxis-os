# USB connect-change notification handling

2026-10-15. Owner-requested correctness fix; native retest pending.

## Report and contract

On the Steam Deck, with main plus #894/#897, plugging Ethernet into the Valve
dock caused xHCI 4:0.3 to fence the root-port-1 subtree. NCM recovered as net0,
but the USB stick remained lost and npfs reported writeback/cleanup status 19.
The supplied log lacks PORTSC at the retirement, so its exact live bits are not
established. The owner-local masked record is
`/shared/present/deck-first-look/deck-2026-10-15.txt`.

Keep a reserved port's existing devices after a live connect-change only if the
old addressed slot returns its exact retained 18-byte device descriptor and its
configured value from GET_CONFIGURATION. Hold class transfers during the bounded
probe. A mismatch, malformed or short reply, timeout, busy endpoint zero or
uncertain port status retires the old slot and permits ordinary fresh enumeration.
The same rule applies to downstream hub children. Startup and fresh-attachment
checks remain conservative; no new hotplug profile or DMA reuse is introduced.

Linux v6.19's
[`hub_port_connect_change`](https://github.com/torvalds/linux/blob/v6.19/drivers/usb/core/hub.c#L5283)
keeps an existing connected/enabled device after successful descriptor
revalidation. The local proof uses the two bounded reads above, not Linux's
complete descriptor/recovery path.

## Implementation

A shared root health predicate covers ordinary presence and inventory checks,
not just `update_ports`: control deadlines and transfer rearm consult presence
before the update pass. A healthy reserved connection also keeps its generation,
preventing later class-level retirement from the same ignored notification.
Observed PORTSC changes retain the existing W1C/neutral-write discipline.

Hub notification bitmaps hold existing children before class collection. Root,
generic HID-hub and private/network-hub workers compare identity through the old
slot, then re-read port status before releasing the hold. A synchronous storage
transfer that finishes during a hub notification resolves the pending proof
before publishing its bytes. The HID recovery hint still permits confirmed
disable/reset recovery without C_CONNECTION. Debug-only verification/retirement
records carry root PORTSC or hub status/change bits. No info or trace
instrumentation, new boot option or default output was added.

## Qualification

Based on current main `9155b226`; kernel-only change, dependency pins untouched.
Ordinary `make -j8 kernel LOG_LEVEL=debug` passed with the existing df87791
compiler. Kernel code revision `d84e1f27ca5863b9c16ab18fc3a2615202ef9267`
passed all four existing CI jobs (run 2810). The final documentation head's CI
is checked separately in the PR.

Interactive nested QEMU 10.2.2: Q35, KVM `-cpu host`, four CPUs, 2 GiB, OVMF,
modern VirtIO networking and a SCSI CD carrying the current-main ISO with only
this kernel replaced. Its archive is the unchanged main `9155b226` CI artifact.
QEMU xHCI has three USB 2 and three USB 3 root ports: one boot-present USB 2
hub with a keyboard and a read-only 32 MiB storage child, plus a read-only
48 MiB storage device on another root.

- Initial enumeration published both disks and the hub keyboard. Remote shell
  and verified IPC calls completed normally.
- QMP `device_del hubkbd` retired only that downstream HID: debug output recorded
  status `0x100`, change `0x3`, and slot 4 fencing. GDB showed the hub and sibling
  roots still reserved, generation unchanged, controller running/not failed.
- QMP added a fresh keyboard on the same hub port. After debounce, GDB observed
  one active runtime HID and an idle runtime state, while both storage devices
  and root generation remained unchanged.
- QMP `device_del hub` then retired the actual root subtree. Debug output
  recorded root port 4 `PORTSC=0x202a0`; GDB showed `PORT_REMOVED`, generation
  1 -> 2 and both boot children removed. The independent storage root remained
  `PORT_RESERVED` and not removed; controller running, no failure or pending
  command. Remote shell and verified IPC calls still completed normally.

The benign connected+enabled CSC branch is established by source inspection;
stock QEMU did not generate the dock's notification, so this is not a simulated
native reproduction. There were no register edits, injected change bits or
merged test probes. Owner-local remote/GDB/log records are under
`/shared/present/usb-connect-change/qemu-2026-10-15/`. The first harness attempt
used a legacy VirtIO NIC and could not reach Remote; it was corrected to
`disable-legacy=on` before these checks. All QEMU/debugger jobs are stopped.

The owner's Deck or vela-dock retest remains separate; no native run is claimed.
Inspect `log -l debug` for root PORTSC or hub status/change on ignore/retirement,
then check that connecting dock Ethernet leaves its USB storage usable and that
an actual disconnect still retires it. No dependency PR or merge-order constraint.

## Identity-proof follow-up, 2026-10-18

The original live-status-only rule above was insufficient: an immediate USB 3
replacement can leave PORTSC connected and enabled with CSC set while the old
slot still points at a departed device. The bounded identity proof and transfer
hold described in the contract and implementation are the accepted correction.

`make -j16 kernel LOG_LEVEL=debug` passed with the existing df87791 builder.
Interactive nested QEMU 10.2.2 used Q35/KVM, four CPUs, 2 GiB, OVMF, the
existing qualification ISO with only the kernel replaced, a USB 2 hub/keyboard,
and an independent USB 3 read-only storage device. QMP stopped the guest,
deleted the keyboard, attached a mouse to the same hub port, and resumed it.
GDB showed the old child removed, one runtime replacement record, the hub and
independent root still reserved, and the controller running without failure.
The same stop/delete/add/resume sequence replaced the USB 3 storage device on
its root port with a different backing. GDB showed that root `PORT_REMOVED`,
generation 1 -> 2, the old device removed, no pending command, and the hub root
still reserved at generation 1. The QEMU jobs are stopped after qualification.

Stock QEMU did not produce the dock's benign live CSC with the old addressed
device still responding. That success branch is source-inspected; the owner must
qualify it natively by connecting and disconnecting dock Ethernet while USB
storage remains usable, and confirm a real unplug still retires the subtree.
