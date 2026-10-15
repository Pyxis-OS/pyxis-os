# USB CDC-NCM Ethernet

Implementation is in flight in [#859](https://git.internal/PyxisOS/pyxis-os/pulls/859),
[Steam Deck task 9](../wip/steam-deck.md#9-dock-ethernet). The ordinary kernel builds;
recovery and native qualification remain pending. This is the original native
NCM class/wire implementation, not a vendor-driver or Linux source adaptation.

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
notifications. Link loss immediately invalidates queued frames and any pending
software TX batch; uncertain submitted OUT storage is retained and never replayed.

Trusted preparation may configure the class and receive carrier notifications
before selection. Ethernet bulk traffic begins only after net0 BIND. NET_CONFIG
queries remain read-only and cannot start USB work. Initial NIC discovery seals
only after bounded boot-connected retries, independently of unrelated unsupported
USB classes. Session MAC/link selection and DHCP use the existing configuration
surface; explicit MAC waiting never substitutes another adapter.

## Retained routes and recovery

A logical NIC keeps its MAC, ID and binding. Its checked root/hub port route,
device identities and NCM function facts are retained. A replacement must match
those facts and the MAC. There is no different-adapter failover. Fresh records
and hardware contexts replace the affected suffix; candidates sharing a hub
prefix share its replacement. Old generations remain retained and fenced.
Private reconstructed hubs admit only the retained NCM routes: no storage/HCI
binding or general USB hotplug. A hub route needs a checked notification stream.
Ordinary HID admission stays in its existing USB2 profile and is excluded from
private network branches and the retained NCM path ports. Shared USB2 hubs leave
global status and unrelated ports with HID; the coordinator drains unrelated
SuperSpeed/private-hub changes without admitting replacements there.

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
their contexts and consume capacity. Candidate routes reserve their share before
class setup, including failed setup, so extra candidates cannot consume the
retained replacement resources. Class pools separately include initial attempts. No runtime registry reopening
admits a new unrelated NIC. Exhaustion or incompatible checked identity leaves
the selected NIC unavailable; the shared controller and unrelated devices remain
running unless a hardware ownership/fence failure makes that impossible.
The immutable lsusb boot observation is not rewritten by private retries or later
recovery; the NIC provider reports current availability separately.

## Limits and qualification

Normal UDP log, Remote and xfer use the ordinary stack. Fatal USB UDP and
stopped-kernel network GDB are unavailable; see [emergency transport debt](../technical-debt.md#usb-network-emergency-transport).
Configuration policy and budgets are not ABI. No raw USB networking authority is
exposed to userspace. The [experiment record](../development/experiments/usb-cdc-ncm/README.md)
distinguishes before-code baseline, earlier root-gadget results and pending
recovery/native checks. Do not stage an unqualified checkpoint as a native build.
