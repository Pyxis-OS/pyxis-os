# Initial net0 selection

Trusted session setup chooses one supported controller for `net0`. The packaged
profile uses link selection with DHCP:

```lua
return { net0 = { select = "link", dhcp = true, optional = true } }
```

Every net0 table chooses exactly one of `select = "link"`, `driver = "virtio"`
or `mac = "..."`. Explicit selectors keep unique-match semantics: ambiguous
VirtIO or MAC matches fail. Link selection also supports static address, prefix
and gateway settings instead of DHCP. It never changes an existing binding.

## Eligibility and preference

Automatic selection waits for complete controller discovery and considers only
prepared controllers with known, reported up carrier. VirtIO without negotiated
STATUS is excluded; an explicit driver or MAC selector can still use it with
VirtIO's assumed-up behavior. Unsupported RTL revisions are excluded. Failed
preparation remains visible but cannot be selected automatically.

An optional `prefer = {"MAC", ...}` array ranks currently eligible candidates
by the first matching entry. Missing or unlinked entries do not delay others;
duplicates are harmless. After preference rank, ascending boot-local controller
ID breaks ties, following PCI inventory order. IDs are the retained PCI
inventory position plus one; that inventory currently stores records in reverse
scan order, so this is not ascending bus/device/function order.
There is no driver-family preference. IDs do not depend on MAC
uniqueness, so two controllers with identical MACs can still be distinguished.
Use the private [image profile override](../development/configuration.md#image-network-profile)
for machine-specific addresses; no real MAC belongs in the packaged profile.

## Binding and startup

NET_CONFIG READ enumerates retained supported records in ascending ID order,
reporting inventory completeness, preparation, fresh carrier knowledge/up state,
binding, family and identity. ID zero ends enumeration; completeness remains
visible at the end. The worker samples hardware without activating transport,
DMA or interrupt delivery. The kernel owns observations and binding; preference
and waiting policy live in the setup session.

`NET_SELECT_LINKED_CONTROLLER` with WRITE binds an exact candidate after
checking complete discovery and fresh
prepared/up carrier. If that check fails, net0 stays unbound and selection retries.
After commitment, activation failure retains the binding until reboot.
Temporary active configuration instability keeps setup pending on that same
prepared controller until it becomes ready or stops. Repeating
the bound ID succeeds despite link loss. There is no fallback or runtime rebinding.

Selection and initial DHCP share an approximately ten-second monotonic foreground
budget. If no candidate is eligible, the local shell starts offline and the
trusted setup process keeps waiting. This applies regardless of `optional`, which
controls absent explicit selectors. Pending DHCP setup retains configuration,
UDP creation, clock/random, memory and diagnostic output authority, closing
bootstrap launch/input/directory grants after handoff. After binding and opening
port 68 it closes UDP creation authority and continues ordinary DHCP maintenance.
Pending static setup applies its settings, then exits. Ordinary children receive
READ and ordinary UDP OPEN only.

A remote launcher waits for an assigned address. A configuring remote owner
advances selection and DHCP itself while waiting; other remote sessions query
net0 without trying to bind. Both wait through temporary configuration
instability on the prepared bound controller; a permanent activation failure
ends setup/remote launch. Missing link is not an error or a new ten-second
DHCP allowance after foreground selection has consumed its budget.

## Limits

This selects the initial controller only. Cable movement afterward does not
change controllers or revalidate a lease on link-up. The ThinkPad dock-facing
DASH XID `502` remains unsupported; the built-in XID `541` is supported. Native
cold/PXE coverage and measured QEMU/VFIO behavior are recorded in
[qualification](../development/link-selection-qualification.md).
