# AX200 runtime HCI transport

The kernel owns USB transport, Intel initialization and HCI bookkeeping. The
trusted userspace service owns GAP policy, L2CAP, SMP and ATT/GATT. The native
[controller ABI](../../include/abi/bluetooth_hci.h) exports copied packets through
one exclusive process-owned controller session; it exports no DMA or raw USB.
The [task 2 record](../development/experiments/bluetooth-runtime-hci/README.md)
distinguishes measured passthrough from source-reviewed behavior.

## Binding and readiness

Boot enumeration admits only checked AX200 `8087:0029` configuration 1,
interface 0/alternate 0, with one interrupt-IN and two bulk endpoints matching
the observed root/full-speed profile. Interrupt and asynchronous bulk reception
start before initialization. Complete inventory must establish a single adapter.
The owning BSP xHCI worker advances finite initialization transactions without
waiting for HCI replies. Storage waits reach the same bounded progress point.

Intel Read Version precedes Reset. The investigation's operational tuple skips
SFI upload for development. A matching cold bootloader instead enters the bounded
pinned SFI upload and real boot-event flow; unknown firmware fails closed.
Operational version is checked before and after Reset, and pinned DDC must
complete successfully. Local version, commands, LE features, buffer lengths and
packet credits are checked before mandatory event masks and readiness. The
[task 3 record](../development/experiments/bluetooth-firmware-readiness/README.md)
distinguishes this implementation from its pending image, warm and native
validation. Neither path establishes production firmware qualification.

## Authority and messages

Trusted boot initialization receives `bluetooth_hci`. Ordinary spaces and shell
children receive no incidental controller authority. CONTROL authorizes STATUS
and exclusive ACQUIRE; development readiness additionally requires explicit
ALLOW_DEVELOPMENT. Ownership attaches to the process, surviving copied or closed
handles. An epoch binds RELEASE, SUBMIT_COMMAND, SUBMIT_ACL and RECEIVE to it.
Requests have finite monotonic deadlines within the current five-second window.

Submission copies one complete HCI wire packet, without an H4 type byte. CALL_OK
means bounded queue admission, not controller/procedure success. The kernel
owns command allowance and per-link ACL packet credits; USB completion and HCI
acknowledgement are separate facts. Same-opcode overlap is refused. Reset,
vendor initialization, event-mask and competing flow-accounting commands are
reserved to the kernel.

RECEIVE copies one complete event or ACL record with epoch, consecutive sequence,
connection generation and applicable command submission ID. An undersized
reply preserves the head. Idle timeout cancels only the logical reader, leaving
posted DMA owned. No raw packet, peer address, key or controller identity is
logged. Applications are not supplied this privileged packet interface.

## Progress and failure

Independent endpoint sequences and bounded framing detect discontinuity. A first
ACL frame can precede its connection event across drains. Up to eight whole
frames are retained in endpoint order for at most five seconds from their first
byte, then replayed only after connection admission in the captured session.
Overflow, stale epoch/generation or unresolved expiry reports input loss; it does
not invent a link or permit handle reuse. Current
settings provide eight commands, eight outgoing ACL packets, 32 received records,
eight live links and 16 queued native requests. Each progress pass has a finite
budget; it allocates nothing and enters no synchronous recovery or wait. Private
bulk reception has two retained DMA receives and eight copied completions,
separate from storage's synchronous bulk ownership and resource budget.

Service exit immediately clears the weak owner reference and session epoch.
Re-grant requires confirmed clean bookkeeping: no live link, unaccounted
command/data, partial frame or deferred ACL. Current cleanup is conservative:
any published non-read-only command, admitted connection or published ACL sets
a sticky dirty flag. It never clears, even after disconnection and settled
credits. Release/exit after that radio work requires reboot; only fully accounted
read-only sessions can be re-granted. Unconfirmed cleanup or stream loss
leaves Bluetooth unavailable until reboot. A Bluetooth protocol failure alone
does not quarantine USB storage; true USB ownership failures still do.

Accepted 2026-10-08: a previously disconnected handle cannot be reused during
this controller lifetime. Known retired-link ACL is discarded while preserving
the disconnect event. Independent event/ACL streams do not yet prove a safe
reuse boundary; generation tags alone cannot do so. Connection/reconnect tasks
must [qualify that boundary](../technical-debt.md#bluetooth-hci-connection-handle-reuse-boundary).

STALL, overflow and uncertain transfer retirement retain DMA. No task 2 pairing,
bond, HID, pointer injection or automatic recovery is implemented. Real ACL and
service connection traffic are later qualification gates, not inferred from
idle reception.
