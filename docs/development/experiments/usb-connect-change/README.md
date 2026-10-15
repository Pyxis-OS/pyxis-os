# USB connect-change notification handling

2026-10-15. Owner-requested correctness fix; native retest pending.

## Report and contract

On the Steam Deck, with main plus #894/#897, plugging Ethernet into the Valve
dock caused xHCI 4:0.3 to fence the root-port-1 subtree. NCM recovered as net0,
but the USB stick remained lost and npfs reported writeback/cleanup status 19.
The supplied log lacks PORTSC at the retirement, so its exact live bits are not
established. The owner-local masked record is
`/shared/present/deck-first-look/deck-2026-10-15.txt`.

Keep a reserved port's existing devices on connect-change alone when connected,
enabled and without over-current; acknowledge the observed changes. Retire on
actual loss/disable/over-current. The same rule applies to downstream hub ports,
retaining their existing additional power/reset/suspend/link-fault checks.
Startup and fresh-attachment checks remain conservative; no new hotplug profile,
recovery policy, device identity or DMA reuse is introduced.

Linux v6.19's
[`hub_port_connect_change`](https://github.com/torvalds/linux/blob/v6.19/drivers/usb/core/hub.c#L5283)
keeps an existing connected/enabled device after successful descriptor
revalidation. This fix implements the owner's stated live-status rule, not a
copy of Linux code or its complete descriptor/recovery path.

## Implementation

A shared root health predicate covers ordinary presence and inventory checks,
not just `update_ports`: control deadlines and transfer rearm consult presence
before the update pass. A healthy reserved connection also keeps its generation,
preventing later class-level retirement from the same ignored notification.
Observed PORTSC changes retain the existing W1C/neutral-write discipline.

Hub notification bitmaps schedule inspection without irreversible HID loss.
Actual port status drives generic HID and private/network-hub retirement;
connect-change alone no longer retires a healthy child. The HID recovery hint
still permits confirmed disable/reset recovery without C_CONNECTION. Debug-only
ignore/retirement records carry root PORTSC or hub status/change bits. No info
or trace instrumentation, new boot option or default output was added.

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
