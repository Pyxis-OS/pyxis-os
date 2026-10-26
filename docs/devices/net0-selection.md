# Initial interface selection

Trusted boot configuration defines ordered logical interfaces before spaces and
session owners start. Explicit names identify configured roles; automatic wired
names use the initial controller inventory. `lo` is predefined, Ethernet uses
`ethN`, and Wi-Fi uses `wlanN`. A Wi-Fi definition can remain unbound until a
configured radio backend binds it. The opt-in [AX200 backend](ax200-wifi.md)
supports one legacy 2.4 GHz OPEN or WPA2-Personal/CCMP link through the separate
[Wi-Fi interface](../interfaces/wifi.md). Defining a name grants no radio rights.
See the [multiple-interface reference](network-interfaces.md) for qualification.

The stock [network profile](networking.md#boot-configuration-and-use) enables
`wired = { auto = true, exclude = {} }` with an empty explicit interface list.
After complete boot discovery, explicit entries reserve names and controllers;
remaining supported Ethernet controllers get unused `ethN` names in ascending
boot-local ID order. Carrier-down or unknown-carrier NICs are included. Generated
entries have independent optional DHCP clients. `wired.exclude` omits listed
MACs from IP configuration and delegated scopes, not mandatory driver draining;
an exact selector naming an excluded NIC is refused. Linked roles filter
exclusions before preference/ID ranking and reservation, including late binding.
A required linked role with no permitted prepared candidate refuses setup; a
permitted candidate waiting for carrier can remain pending. Missing `wired` or
`auto = false` keeps explicit-only behavior.

Generated names remain fixed until reboot; use an explicit MAC entry for a
persistent hardware role. Newly attached post-boot controllers are not added.
Stock route and numeric DNS selection are automatic within held scope;
management remains explicitly pinned to `eth0`. See [route preference](#automatic-route-preference).

Each explicit entry chooses exactly one of `select = "link"`, `driver = "virtio"` or
`mac = "..."`. Static address/prefix/gateway can replace DHCP. Explicit selectors
require a unique class-matching controller; ambiguous matches or one controller
selected twice are refused before binding publication. Optional absence omits a
binding, preserving the logical definition for a later attempt. Existing bindings
cannot be reassigned until reboot.

## Eligibility and preference

In automatic mode an explicit Ethernet linked role reserves its preferred
prepared controller even with down/unknown carrier; MAC preference and controller
ID fix that initial choice before automatic entries claim the remaining devices.
The role waits for known/up carrier before binding. Explicit-only link selection
considers prepared class-matching controllers with known reported up carrier
after complete discovery. VirtIO without negotiated STATUS has no
reported carrier and is excluded from link selection. The native explicit
VirtIO/MAC selectors retain their own matching rules; neither invents reported
carrier. Unsupported RTL revisions remain unselectable; failed preparation stays
visible in inventory.

An optional `prefer = {"MAC", ...}` list ranks eligible linked candidates, then
ascending boot-local controller ID breaks ties. In explicit-only mode, missing
or unlinked preferred entries do not delay another eligible candidate. IDs are
opaque and independent of MAC uniqueness. PCI inventory currently retains records in reverse scan order,
not ascending bus/device/function order; USB NCM IDs follow the PCI inventory.
There is no driver-family preference.

Use the private [image profile override](../development/configuration.md#image-network-profile)
for machine-specific selectors. No real MAC belongs in packaged profiles.

## Authority, binding and startup

Registry READ enumerates supported retained controllers without activating DMA,
transport or interrupt delivery. Records distinguish class, driver, identity,
preparation and fresh carrier knowledge/state, including for unselected hardware.
The USB worker can prepare NCM class state and carrier notifications before
selection. It drains and discards prepared NCM receive data until binding, so
an unselected adapter cannot stall its switch; Ethernet frames reach the stack
and transmit starts only after binding.

Only policy MANAGE can submit binding batches. The worker validates the complete
batch, including class, unique match and exclusive controller ownership, before
publishing associations. `NET_SELECT_CONTROLLER` uses an exact prepared
controller ID without requiring known/up carrier, so an automatic binding can
precede cable insertion. `NET_SELECT_LINKED_CONTROLLER` uses an exact retained
controller ID and checks complete discovery plus fresh prepared/up carrier before
first commitment. A failed check changes no binding; activation failure after
commitment preserves the association. Repeating an identical binding is
idempotent even after link loss. No automatic fallback or controller migration
is added.

Trusted boot-init prepares the profile and grants an explicit network owner the
required per-interface WRITE facets and pinned DHCP broadcast factories. That
owner services independent clients and retries still-unbound definitions while
other clients and the local shell can proceed. Ordinary children receive scoped
packet factories and registry READ, never implicit configuration or policy
MANAGE. Remote receives its selected READ facet and matching scoped factories;
it waits for that interface rather than whichever NIC acquires an address first.
Route and DNS policy are separate from explicitly pinned management selections.

## Limits and historical evidence

Selection does not move a binding when a cable changes ports, recreate concrete
transports after address retirement, or authenticate Wi-Fi. The ThinkPad
dock-facing DASH XID `502` remains unsupported; built-in XID `541` is supported.
The [link-selection qualification](../development/link-selection-qualification.md)
records historical native cold/PXE and QEMU/VFIO evidence at its listed revisions;
it does not qualify the multiple-interface public cut.

USB NCM identity-preserving recovery has its own
[retained-route contract](usb-cdc-ncm.md#retained-routes-and-recovery) and measured
coverage. It cannot substitute a different adapter under an existing grant.

## Automatic route preference

Owner accepted 2026-10-19. `routing = { auto = true }` ranks usable wired
interfaces before connected Wi-Fi, then compares the lower observed TX/RX rate
within the class. Unknown speed ranks last; equal class/rate retains the current
winner. Before any winner exists, configured interface order breaks the tie.
Owner correction (2026-10-20): connected routes compare class/rate preference
before prefix length; longest prefix breaks ties only within the same rank,
then the stable order breaks equal-prefix ties. This prevents a more-specific
lower-ranked Wi-Fi lease from overriding an on-link Ethernet route.
The worker maintains stable default and connected preference orders; each held
factory filters those orders by its immutable scope. An out-of-scope global
winner cannot prevent use of a permitted peer, and protocol factories with the
same scope agree. A usable interface needs a current
address and available transport, and a default needs a gateway. Reported failed,
expired or acquiring DHCP state is ineligible. There is no WAN-health probe.

The worker refreshes preference after driver/configuration service and on new
route/policy queries, observing carrier, rates, address/gateway and DHCP reports.
A returning equal-rate peer does not displace the current winner.
`routing.default = "ethN"` retains the named off-link default and excludes
`routing.auto = true`; connected routes still use the same preference-first
selection. Scope and explicit source/interface restrictions always apply first.
An explicit operation path/source or a single-interface factory always stays
pinned. Retained sockets/endpoints never migrate or replay uncertain traffic.
Remote, log and debugger remain explicitly selected, with no automatic fallback.
PXE boot hardware receives no preference or authority.

`dns = { server = "1.1.1.1" }` supplies numeric DNS through a reduced-rights copy
of the caller's already-held UDP scope. Each new query opens a routed endpoint.
Adding `interface = "ethN"` pins numeric DNS; omitting `server` with a named
interface uses that interface's DHCP contribution. Existing processes keep their
startup resolver address; packet authority never expands when selection changes.

NCM rates come from the device's ConnectionSpeedChange notification (upstream is
host TX, downstream host RX), including a speed notification preceding link-up.
RTL8111H decodes 10/100/1000 Mb/s from its current PHY status byte. Down/lost links
clear rate validity. VirtIO without negotiated-rate support and the current
AX200 backend report unknown. USB bus speed and advertised capabilities
are never substituted. [Qualification](../development/experiments/automatic-network-preference.md)
separates synthetic observations from native gigabit tie/fallback checks;
unequal-rate native preference remains unqualified.
