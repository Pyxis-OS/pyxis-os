# Initial interface selection

Trusted boot configuration defines ordered logical interfaces before spaces and
session owners start. Names identify configured roles; controller inventory order
does not assign names. `lo` is predefined, Ethernet uses `ethN`, and Wi-Fi uses
`wlanN`. A Wi-Fi definition can remain unbound: this control cut supplies no
radio backend, association or authenticated readiness. See the
[multiple-interface milestone](../wip/network-interfaces.md) for qualification.

The stock [network profile](networking.md#boot-configuration-and-use) declares:

```lua
interfaces = {
  { name = "eth0", class = "ethernet", select = "link", dhcp = true, optional = true },
}
```

Each entry chooses exactly one of `select = "link"`, `driver = "virtio"` or
`mac = "..."`. Static address/prefix/gateway can replace DHCP. Explicit selectors
require a unique class-matching controller; ambiguous matches or one controller
selected twice are refused before binding publication. Optional absence omits a
binding, preserving the logical definition for a later attempt. Existing bindings
cannot be reassigned until reboot.

## Eligibility and preference

Link selection considers prepared class-matching controllers with known reported
up carrier after complete discovery. VirtIO without negotiated STATUS has no
reported carrier and is excluded from link selection. The native explicit
VirtIO/MAC selectors retain their own matching rules; neither invents reported
carrier. Unsupported RTL revisions remain unselectable; failed preparation stays
visible in inventory.

An optional `prefer = {"MAC", ...}` list ranks eligible linked candidates, then
ascending boot-local controller ID breaks ties. Missing/unlinked preferred
entries do not delay another eligible candidate. IDs are opaque and independent
of MAC uniqueness. PCI inventory currently retains records in reverse scan order,
not ascending bus/device/function order; USB NCM IDs follow the PCI inventory.
There is no driver-family preference.

Use the private [image profile override](../development/configuration.md#image-network-profile)
for machine-specific selectors. No real MAC belongs in packaged profiles.

## Authority, binding and startup

Registry READ enumerates supported retained controllers without activating DMA,
transport or interrupt delivery. Records distinguish class, driver, identity,
preparation and fresh carrier knowledge/state, including for unselected hardware.
The USB worker can prepare NCM class state and carrier notifications before
selection; Ethernet bulk traffic starts only after binding.

Only policy MANAGE can submit binding batches. The worker validates the complete
batch, including class, unique match and exclusive controller ownership, before
publishing associations. `NET_SELECT_LINKED_CONTROLLER` uses an exact retained
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
System route, DNS, log and debugger selections are separate explicit policy.

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
