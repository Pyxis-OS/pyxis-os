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
   failed DHCP client cannot displace a usable peer.
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
