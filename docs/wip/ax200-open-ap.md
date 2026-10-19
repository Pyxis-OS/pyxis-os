# AX200 open test AP

Stage 3 assigned to Codex beta; all three decisions accepted and implementation
authorized by the owner (2026-10-19). Baseline precedes code. The owner
selected a private router guest SSID; use `<test-ssid>` here. The AP is enabled only for orchestrator-scheduled private native runs. [Wi-Fi](wifi.md) owns the stage order and accepted kernel/BSD-reference
placement. WPA2/SAE, roaming and other adapters remain later stages.

After this stage the owner can: explicitly associate one AX200 station with the
isolated open AP, obtain a separate `wlan0` address and exchange verified traffic,
while wired Remote stays primary.

## Inspected starting point

At main `63d84152` (stage 2, #984), firmware/NVM startup, passive scan,
controller READ/SCAN, cancellation and immutable scan FILEs work. There is no
station authentication/association state machine, data TX queue, Ethernet data
adapter or connection authority. A scan FILE is observation, not permission to
associate. Current country/channel installation only narrows passive scanning;
its bitmask alone does not establish permission or power limits for transmission.

The [multiple-interface core](../devices/network-interfaces.md) defines `wlanN`
but `include/kernel/net/controller.h` and `kernel/net/driver.c` bind only VirtIO,
RTL8111 and NCM. Interface addressing/DHCP, immutable packet scopes, epochs and
independent default/DNS/management selection already exist. Reuse those paths;
no second IP stack, route switch or implicit Ethernet fallback.

Keep the BSP radio worker as sole firmware/DMA/station owner. IRQ work stays
bounded; the network worker exchanges owned bounded Ethernet frames with it,
never borrowed RX DMA. Audit the reached pinned iwx/net80211 station, PHY,
management and data-queue closure before allocating it; retain notices and
measure actual DMA/rings. Ethernet service must progress during every radio
wait. No LinuxKPI, raw-frame grant, credential API or successful fake operation.

## Accepted decisions (owner, 2026-10-19)

1. **First link profile and target:** one explicit open infrastructure
   BSS on 2.4 GHz channels 1–11, 20 MHz, legacy/non-HT station operation first;
   no aggregation, HT/VHT/HE negotiation or active probes. The AP must admit a
   legacy station. Open System authentication and association are real bounded
   management exchanges; "open" supplies no encryption or peer authentication.
   Read a finite private profile FILE with explicit `security=open`, counted
   SSID, BSSID and channel; copy/validate before effects. Confirm the selected
   BSS passively; refuse security/identity/channel mismatch, hidden SSID,
   ambiguity or a secured/OWE target. No same-name roaming, automatic reconnect
   or downgrade. An explicit boot-init country is mandatory before *any* TX;
   check live firmware/NVM transmission, power/SAR and regulatory restrictions,
   not merely stage 2's passive mask. Missing/unknown policy or rfkill refuses.
   Default connect deadline: 30 s per attempt; bounded cleanup separately.
   Record the legacy profile's rate/throughput limits rather than imply peak Wi-Fi.

2. **Authority and connection lifetime:** add independently delegated,
   controller-scoped CONNECT authority through declared boot/session/bundle
   grants. READ/SCAN, IP configuration and packet rights imply none of it.
   One connection attempt/link per controller; another returns BUSY, never
   replaces it. The attempt is cancellable and waitable; successful completion
   yields a kernel-owned connection lease with separately attenuable observation
   and control. Owning control aliases keep the link; observational aliases
   cannot disconnect or keep it alive. Final owning close/CUT/session teardown
   disconnects and drains; explicit disconnect does likewise. A CLI can transfer
   the owning lease through the existing typed capture handoff, without a
   resident helper or implicit "current connection" slot. Copies/delegation
   require their actual transport rights and preserve revocation lineage.
   Failed publication must drain the unpublished link, never detach it silently.
   Intended use after implementation (not yet shipped):

   ```text
   link=$(wifi connect --profile home://wifi-test.lua)
   wifi status $link
   wifi disconnect $link
   unset link
   ```

   Status distinguishes attempted/associated/open/data-ready/failed; OPEN does
   not claim WPA authentication. AP loss, deauthentication, cancellation or
   revoked policy closes data admission without reconnect/replay. Uncertain
   firmware stop retains DMA and disables the controller until reboot, as in
   stage 2. Hardware management-frame responses after association remain legal
   only within the admitted policy.

3. **Network policy and qualification path:** boot configuration binds
   AX200 uniquely to `wlan0`; a trusted maintainer uses the existing per-interface
   DHCP/configuration path after association. CONNECT grants neither L3 WRITE,
   packet scopes nor policy MANAGE. Preserve `eth0` as default, DNS, Remote, UDP
   log and network GDB, without fallback. Test programs receive explicitly
   declared `wlan0`-only TCP/UDP/echo authority; no call-site authority wrappers.
   Report association/open security separately from address/DHCP readiness in
   `wifi status`/`net`. Link retirement clears the Wi-Fi address incarnation and
   invalidates its flows; stale DHCP work cannot restore it. No migration to
   Ethernet, AP or connection generation, and no transparent reconnect.
   Use a reachable isolated-test endpoint: guest isolation may block horse's
   LAN address. Keep isolation enabled; identify an owner-controlled reachable
   fixture before claiming end-to-end transfer qualification. Gateway reachability
   alone is not a verified payload-transfer result.

## Owner AP setup expectations

Configure the already chosen router **guest** network only; do not change the
owner's normal secured WLAN. Keep actual SSID/BSSID and country/profile bytes in
private results/configuration, never PRs, screenshots or logs.

1. Leave the guest network disabled until the native packet is ready and the
   orchestrator schedules the run. Then enable broadcast `<test-ssid>` on
   **2.4 GHz only**, a fixed locally permitted channel 1, 6 or 11, **20 MHz**.
   Keep the channel fixed for matched runs; supply the actual channel/BSSID
   privately. Permit legacy 802.11g stations; do not select an ax-only mode.
2. Security must be **Open/None**: no password, WPA/WPA2/WPA3, Enhanced Open/OWE,
   transition security or required PMF. Disable any captive portal/login page.
   If the router cannot supply that profile, stop and report it rather than
   weakening the normal network or silently selecting another security mode.
3. Keep **client isolation enabled** and LAN isolation enabled where offered.
   Enable guest DHCP with an available lease; its subnet must differ from wired
   Ethernet's. Supply the real country of operation privately for explicit boot
   policy; no inferred/default country. Agree a reachable test-fixture path;
   do not disable isolation simply to reach horse.
4. Keep ThinkPad on AC with Ethernet connected. Wired Remote/log remain primary.
   Record distance, AP channel/security and exact images privately. Disable the
   temporary open guest network after the attended qualification window.

## Delivery tasks (implementation assigned)

1. [x] **Baseline and resource closure:** before code, use existing tools on current
   main for repeated wired ttcp, native FILE/launch controls if their paths change,
   `wifi status` DMA/queue counters, and a passive scan. Record revisions,
   configuration, AC, repetitions/ranges. Native idle/BSP CPU cost remains the
   existing debt until accounting is available; throughput is not CPU evidence.
   Review additional PHY/MAC/station/TX budgets and bounds from the pinned source.
2. [ ] **Connection/control and station path:** install the accepted authority and
   lifetime, validate target/policy, configure firmware PHY/MAC/binding/station,
   then authenticate/associate. Bound hostile management/IE parsing, sequence
   and identity checks, timeouts and retirement. No data before admission.
   QEMU qualifies ordinary boot/absence/rights and unchanged Ethernet, not RF.
3. [ ] **Data and IP:** add the AX200 controller adapter and bounded TX/RX conversion,
   completion ownership/backpressure, `wlan0` addressing/DHCP and scoped data.
   Drop foreign-BSSID, malformed, unexpected protected or unsupported frame
   forms; do not feed unchecked management/aggregate data to Ethernet/IP. Surface
   actual MTU, rate and queue limits; no unrelated tuning or new global budgets.
4. [ ] **Native qualification:** owner-enabled isolated AP; association, DHCP,
   per-interface gateway check and deterministic payload/hash transfer through
   the agreed reachable fixture; repeated connect/disconnect, cancel/timeout,
   wrong target/security/policy refusal, AP-off loss and fresh explicit recovery.
   Check held-copy versus final-close behavior through owner-routed review.
   Repeat matched wired controls while associated and under bounded Wi-Fi load;
   record both throughputs/ranges, counters and actual DMA. Regressions require
   attribution; no peak-rate promise for the legacy profile.

Packets follow `/shared/present/RUNSHEETS.md`: short numbered setup, one-command
checks using existing tools, private raw results and a concise public summary.
Tell the orchestrator before any native/PXE action. Implementation and qualification are recorded in the
[stage-3 experiment](../development/experiments/ax200-open-ap.md). Country was
confirmed explicitly by the owner; only private profiles install it. The first scheduled native smoke refused TX at platform discovery; see the
experiment record. The guest AP is disabled between scheduled native attempts.
No known
compiler-container rebuild is needed; any newly required mirror entry must be
settled before fetching. The owner's standing rule starts the next assigned task
automatically on merge; no extra features enter this stage. Native/PXE actions
still require prior notice and the AP stays off until the orchestrator schedules
the prepared native packet.


## Native-smoke follow-up proposal (2026-10-19)

**Companion discovery — recommended default:** separate bounded read-only ACPI
PCI companion discovery from the PCI BAR-allocation resource snapshot. On the
ACPI worker, identify a unique enabled PNP0A03/PNP0A08 root by validated segment
and base-bus identity, then follow the retained PCI parent chain through unique
immediate-child _ADR matches. Missing methods use only specified ACPI defaults;
failed evaluations, ambiguous roots/children, exhausted bounds and malformed
identity remain refusals. Preserve the existing PCI allocation _CRS rejection;
this lookup authorizes neither resource allocation nor TX.

After companion discovery, capture the existing individual policy states. Use
firmware defaults only for proved-absent/disabled optional tables as already
implemented; retain refusal of locked, unknown, malformed or unsupported required
policy, and the explicit-country/NVM/firmware channel gates. Split terminal
failure detail into companion/platform versus country-MCC admission and expose
bounded source-state diagnostics without SSIDs, BSSIDs or device addresses.
Alternative: repair the full resource-allocation parser first; this couples radio
policy discovery to unrelated resource-placement support and is not needed to
read the namespace. No proposed fallback treats failed discovery as absent.

A separate payload-fixture decision remains after the link smoke: recommended
shape is an owner-controlled temporary HTTP fixture reachable by outbound guest
traffic, serving a fixed hashed artifact and a bounded upload sink; preserve
client/LAN isolation and avoid credentials/paid services. No endpoint is presumed
available or approved, and gateway reachability alone cannot close the stage.
