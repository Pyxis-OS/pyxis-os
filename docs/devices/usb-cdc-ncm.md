# USB CDC-NCM Ethernet

Implemented in [#859](https://git.internal/PyxisOS/pyxis-os/pulls/859),
[Steam Deck task 9](../wip/steam-deck.md#9-dock-ethernet). ThinkPad native checks
with the Steam Deck dock pass carrier, pooled DHCP, Remote, ping, Ethernet
cable cycling, USB branch recovery and file integrity. This is an original
native NCM class/wire implementation, not a vendor-driver or Linux source
adaptation.

## Ownership and admission

The owning BSP xHCI worker performs checked descriptor/class setup, notifications
and USB transfers. The BSP network worker sees retained NIC metadata and bounded
copied Ethernet frame queues; it never borrows USB DMA or calls controller helpers.
All contexts, rings, transfer buffers and discovery records are prepared before
AP startup. Runtime recovery allocates/maps nothing. PCI NICs keep their existing
paths; opaque USB IDs follow the PCI inventory.

Each supported function must have a unique checked configuration/control/data
pair, high- or SuperSpeed endpoints and a descriptor-supplied unicast MAC. Setup
negotiates bounded NTB16 without appended CRC, validates complete receive blocks
before publishing frames, and batches at most eight TX frames or one millisecond.
The current MTU is 1500; buffers are at most 16 KiB. Carrier/speed come from real
notifications. Carrier notifications use actual bytes and notification type,
matching Linux's tolerant status handling: interface index, request type and
wLength are advisory; any nonzero NetworkConnection wValue means up. SPEED
requires sixteen actual bytes and may arrive as an 8+8 split. The collector also
accepts combined known notifications. Unknown notifications discard only that
completion's remainder and preserve the last real carrier state. An all-zero
trailer after a complete known event is padding; a standalone zero header is
still decoded as DOWN. This narrow rule avoids inventing a DOWN from padding.

The checked Ethernet descriptor must support a 1514-byte frame.
GET_MAX_DATAGRAM_SIZE is advisory: a failed, short or out-of-range reply does
not override that descriptor or refuse admission. With the maximum-datagram
capability, setup attempts SET_MAX_DATAGRAM_SIZE(1514) unless the query already
reports 1514. SET refusal is also advisory and logged; later mandatory setup,
transport health and bounded whole-frame validation still apply. This follows
the descriptor-first policy in Linux's
[`cdc_ncm_set_dgram_size`](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/usb/cdc_ncm.c#L609-L654),
without importing its implementation.

Link loss immediately invalidates queued frames and any pending
software TX batch; uncertain submitted OUT storage is retained and never replayed.
An RX frame claimed by the network worker pins its epoch through Ethernet
delivery. If withdrawal advances the epoch, replacement readiness and frame
admission remain closed until that claim is released. A frame accepted before
withdrawal may finish before replacement readiness opens; a resumed old claim
cannot be accepted against a ready replacement. Protocol processing remains
interruptible, and releasing an old claim wakes both workers without waiting
inside the USB worker.

Trusted preparation may configure the class and receive carrier notifications
before selection. Its SET_INTERFACE enables the adapter's data path, so the
prepared transport posts bulk IN immediately and the data path is always
drained: an adapter whose IN pipe is not read fills its FIFO and may send
802.3x PAUSE frames, which an unmanaged switch obeys until its other ports
starve. Until a policy-authorized binding commits this controller to one
logical Ethernet interface, every received block is validated and discarded
on the USB worker; no frame reaches the network worker. Transmit begins only
after binding. Registry and
interface READ queries cannot start USB work. Initial NIC discovery seals
only after bounded boot-connected retries, independently of unrelated unsupported
USB classes. Trusted setup selects controllers through class-filtered inventory
and validates each whole binding batch before publication; per-interface DHCP
uses that interface's WRITE facet and pinned broadcast factory. Explicit MAC
waiting never substitutes another adapter. See [interface selection](net0-selection.md).

## Retained routes and recovery

Receive queue pressure retains the validated NTB and its next frame in the
class-owned buffer. The USB worker resumes after the network worker releases
a copied slot; full detection and wake arming share the BSP/IF=0 span.
No later NTB replaces a held suffix, and withdrawal discards it under the old
admission epoch. Posted network IN TDs reserve completion-record capacity;
rearm pauses when those credits are occupied and resumes after consumption.
Notifications and TX still run during RX pressure. This removes software
handoff loss for short stalls. The pause is bounded: once the provider has
refused frames for 50 ms (`USB_NETWORK_RX_STALL_MS`), the held suffix and every
later block are dropped until the provider accepts a frame again. Drops are
counted per stall and per transport; the transport's first stall and resume
log at info, later ones at trace, since overload can repeat them many times a
second. A stalled network worker therefore costs
received frames, not the adapter's FIFO. States in which Caelum stops servicing
the USB controller altogether remain [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1290).
See the [attribution record](../development/experiments/usb-cdc-ncm/rx-backpressure.md).

A logical NIC keeps its MAC, ID and binding. Recovery reuses that NIC; a bound
replacement activates bulk under the same logical interface binding, without
another BIND or repinning existing grants. Session DHCP waits for
transport readiness and reported carrier before acquisition, retaining its
bounded retry/maintenance windows. Its checked root/hub port route,
device identities and NCM function facts are retained. A replacement must match
those facts and the MAC. There is no different-adapter failover. Fresh records
and hardware contexts replace the affected suffix; candidates sharing a hub
prefix share its replacement. Old generations remain retained and fenced.
Runtime physical reconstruction belongs to the shared [USB topology
owner](usb-hubs.md#bounded-runtime-topology-recovery), including hubs and HID
siblings. NCM performs class rebind only, after the owner has published a checked
fresh route. There is no second branch enumerator racing HID admission. A hub
route still needs a checked notification stream. Storage/HCI binding and pool
reattachment remain excluded.

Connection bitmaps trigger status inspection, not unconditional generation loss.
Confirmed removal closes NIC admission and discards queued work before endpoint
fences or reset waits. Recovery does not hold a descriptor request concurrently
with the existing HID control state machine. Root freshness is rechecked after
attachment waits; downstream connections are reset/checked, and the route is
revalidated before the new transport can start Ethernet traffic. Only the current
transport may publish or withdraw the NIC; a fenced retired transport relinquishes
that authority explicitly.

Tunable preparation budgets currently permit two candidates/controller, two
initial retries per unresolved boot-connected root, and four replacement
attempts per known NIC. Initial retries target the uncertain suffix under a live
parent; they never reset working HID/storage/HCI or admitted NIC siblings to
resolve another leaf. An uncertain hub which cannot be reset without destroying
those owners leaves discovery honestly incomplete. Known NIC identity remains
discovered while its current transport is unavailable. Failed attempts retain
their contexts and consume capacity. Boot candidate routes are bounded before class setup. Runtime physical
admission shares ordinary retained topology capacity with hubs and HID, but a
protected core-record/host-context share covers one fresh attempt per unique
known route prefix. Ordinary churn cannot consume that share; failed protected
attempts consume it, so exhaustion can still leave NCM recovery unavailable. Class pools separately include initial
attempts. Physical retry deadlines and limits are documented with the topology
owner; the four-attempt NIC bound counts class rebinds. A physical retry pause
keeps the NIC pending and preserves unspent route protection until an observed
stable unplug/replug; it is not retained-backing exhaustion.
No runtime registry reopening admits a new unrelated NIC. Exhaustion or incompatible checked identity leaves
the selected NIC unavailable; the shared controller and unrelated devices remain
running unless a hardware ownership/fence failure makes that impossible.
The immutable lsusb boot observation is not rewritten by private retries or later
recovery; the NIC provider reports current availability separately.

## Observed AX88179A behavior

The Steam Deck dock's `0b95:1790` uses NCM configuration 2 behind its
`2109:0817` SuperSpeed hub. Native ThinkPad qualification observed:

- GET_MAX_DATAGRAM_SIZE returns 28 although the Ethernet descriptor advertises
  1514. The descriptor-backed profile is retained; SET(1514) completed successfully.
- SPEED_CHANGE uses wIndex 1 (the data interface), rather than control interface
  0, and reports zero speeds. Header metadata is advisory; actual NetworkConnection
  establishes carrier. Zero speeds alone do not mean the link is down.
- At Caelum startup the dock briefly power-cycles, including roughly half a
  second of interrupted PD power reported by the owner. Its physical cause is
  unassigned. The driver recovers the retained branch; later USB-C contact
  changes also recovered with the same logical NIC and binding.

These are measured behaviors of this dock, not VID/PID-specific code paths.
The native run used an unmerged observation overlay; periodic summaries, raw
notification bytes and DHCP wire records are absent from the submitted driver.

## Limits and qualification

Normal UDP log, Remote and xfer use the ordinary stack. Fatal USB UDP and
stopped-kernel network GDB are unavailable; see [emergency transport debt](https://git.internal/PyxisOS/pyxis-os/issues/1281).
Configuration policy and budgets are not ABI. No raw USB networking authority is
exposed to userspace. The [experiment record](../development/experiments/usb-cdc-ncm/README.md)
distinguishes the before-code baseline, isolated root-gadget checks and the
native ThinkPad/Steam Deck dock results, including matching host/guest SHA256
for 64 KiB and 15 MiB HTTP transfers. The Deck itself and vela are not qualified
by these checks. ThinkPad/RTL8156BG native receive qualification reaches
86.904–99.723 MiB/s for 64 MiB with matching content and no reported gaps over
2 ms; see the [handoff record](../development/experiments/usb-cdc-ncm/rx-backpressure.md#native-retained-handoff-result)
for measured TX limits. Never run a USB gadget on the shared development VM;
use a disposable guest or real hardware.

The [RTL8156 receive-throughput work](../wip/usb-ncm-throughput.md) records the
implemented retained handoff, native result and separate backlog/TX follow-ups.
