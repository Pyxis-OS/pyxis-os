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
Ordinary kernel build passed with the existing df87791 compiler and debug ceiling.
Interactive QEMU/hot-unplug and final CI results will be recorded before review.
The owner's Deck or vela-dock retest remains separate; no native run is claimed.
