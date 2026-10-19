# Automatic wired binding and network preference

Owner-assigned 2026-10-19, as two delivery PRs. This builds on the implemented
[multiple-interface core](../devices/network-interfaces.md) and
[boot profile](../devices/networking.md#boot-configuration-and-use).

## Accepted decisions (owner, 2026-10-19)

1. Automatically bind supported boot-discovered wired NICs, including those with
   carrier down. Explicit entries reserve their names/controllers first; remaining
   devices receive unused `ethN` names by ascending boot-local controller ID.
   Explicit per-MAC exclusions omit IP configuration and packet delegation, not
   mandatory driver draining. `wired.auto = false` retains explicit-only behavior.
   Bindings/scopes stay fixed for the boot; new post-boot adapter admission is out
   of this slice. Names requiring cross-boot hardware identity use explicit MACs.
2. In the second delivery, prefer wired over connected Wi-Fi, then higher truthful
   negotiated speed using the lower TX/RX rate. Unknown speed ranks last within
   its class; retain the current winner on ties. Apply this to equal connected
   prefixes too. Re-evaluate on carrier/address/rate changes or DHCP failure;
   never migrate existing sockets, replay traffic or substitute another NIC under
   a held interface grant. A usable address/path is required; an acquiring or
   failed DHCP client cannot displace a usable peer. Off-link traffic requires a
   usable gateway; no WAN-health probing is promised. Automatic numeric DNS
   (stock resolver `1.1.1.1`) uses only the caller's already permitted initial
   interface set, so a new query can follow routing without widening authority.
   Explicit interface/DHCP DNS remains available; existing programs do not learn
   changed DHCP resolver addresses in this slice. Explicit pins never fall back.
3. Management remains explicitly pinned: Remote, log and debugger do not follow
   automatic egress. PXE boot hardware gets no implicit preference or authority.
   Wi-Fi association remains separately configured; raw advertised wireless maxima
   or USB bus rate are not negotiated link-speed evidence.

## Delivery

- [ ] **PR 1 — boot binding.** Expand the initial wired inventory, exclusions,
  explicit reservations and generated naming before definitions/scopes are
  published. Carry the resolved initial plan through declared network-owner
  grants. Preserve route, DNS, equal-prefix and management selection otherwise.
  After this task the owner can: boot with independent DHCP on every supported
  wired NIC without listing each one in the profile.
- [ ] **PR 2 — observation and preference.** Add truthful negotiated-speed
  observation for NCM, RTL8111 PHY status and AX200 where available; use unknown
  where the backend cannot report it. Add route/DNS selection and re-evaluation
  under decision 2, with explicit pinned choices still available.
  After this task the owner can: unplug the preferred wired path and send new
  ordinary traffic through the next usable path without moving management.

## First delivery limits and qualification

The stock first-delivery profile uses `wired.auto = true` and still pins route,
DNS and management to `eth0`. A down `eth0` does not select a working `eth1`.
A generated-policy name absent from the inventory means no selected path, so
zero-NIC and all-excluded configurations can boot locally. Discovery has a
bounded ten-second setup wait; exhausting memory, the 254 non-loopback interface
limit or discovery deadline refuses setup rather than truncating the plan.

The exact-controller binding selector requires a prepared retained controller,
without a carrier-up precondition. Explicit link selectors keep their existing
fresh prepared/known-up test. No new packet/configuration authority is inferred
from an observed name or controller ID. Private initial-plan text is useful only
alongside the explicitly delegated configuration/DHCP facets.

Qualify PR 1 with multi-NIC QEMU: all wired interfaces, carrier-down then up,
exclusion, explicit-only control, explicit reservations, zero-NIC/all-excluded
local boot and honest malformed/conflicting-profile refusal. Compare ordinary
launch controls with exact-main artifacts. Native checks are scheduled separately;
no ThinkPad/PXE run belongs to this first implementation window.

The [first-delivery experiment record](../development/experiments/automatic-wired-binding.md)
records exact-main controls, QEMU cases, variation and unrun native checks.

## Open behavior choice before PR 1 delivery

Source review after the stock/MAC-profile qualification found an interaction
not covered by those checks. With `wired.auto = true`, an explicit Ethernet
`select = "link"` role can have no eligible controller at initial discovery.
It reserves its name but no controller; automatic entries then bind the prepared
wired controllers, including down ones. Later linked selection skips those bound
controllers, so the explicit role can remain unbound after a cable returns.
This is source-inspected, not a QEMU-qualified pending-role result.

**Proposed default, not accepted:** in automatic mode, reserve the preferred
prepared controller for each explicit linked role at boot even when carrier is
down/unknown; use its existing MAC preference and controller-ID tie-break.
Still require known/up carrier for the linked role's actual binding. This fixes
the initial choice instead of letting whichever candidate links later win.
Explicit-only mode retains current selection unchanged. The alternative is to
keep eligible-only reservation and document that automatic bindings can prevent
a pending linked override from acquiring a controller; use an explicit MAC entry
to reserve a down NIC reliably.

The owner must choose before this edge is implemented or PR 1 is called complete.
The implementation/qualification already published covers the stock empty
explicit list, explicit MAC reservations and explicit-only linked control.
