# Link-based net0 selection

Status: proposal. DHCP is implemented; these decisions are settled when this milestone starts. It is
an opt-in selector mode beside `driver` and `mac`, for example
`net0 = { select = "link", dhcp = true }`.

- **Link for unbound controllers.** Today only the bound controller reports link.
  Lookup needs a read-only link state for prepared, unbound controllers. Their PHY
  is already up and negotiating.
- **Waiting at bind:** a bounded wait for any candidate's link (autonegotiation
  takes seconds).
- **Several ports with link:** an ordered preference list in the profile;
  otherwise the first one to come up.
- **Bind once until reboot,** as now. Moving the cable later doesn't rebind;
  runtime rebinding is a separate, larger step.

On the ThinkPad this pays off only when a second port is supported. The onboard
XID `502` behind the dock jack is a DASH controller and would need its own
identification, preparation and management-firmware coordination (see the
[RTL8111 driver](../devices/rtl8111.md) limits). Suggested order: DHCP, then
optionally the dock controller, then link selection. Link selection can go first
if it's wanted for QEMU setups with several NICs.

