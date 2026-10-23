# Wi-Fi controller, scan and connection operations

OPEN and WPA2-Personal/CCMP behavior is implemented. Native checks and their
limits are recorded in the [WPA2 qualification record](../development/experiments/ax200-wpa2.md)
and [technical debt](../technical-debt.md#ax200-wpa2-qualification-limits).

The native AX200 implementation is opt-in (`wifi.ax200=1`, image option
`WIFI_AX200=1`). It prepares firmware/DMA once at boot on a dedicated BSP
worker. It supports firmware-selected passive 2.4 GHz channels and one explicit
legacy OPEN or WPA2-Personal/CCMP station profile. The supported channel-number domain is 1–14; firmware
permissions determine which are usable, including 12/13. Channel 14 is passive-only
until a CCK-only station policy is implemented. No 5 GHz entry is used.
There are no active probes, WPA1/TKIP, SAE/PMF, roaming or controller-reset recovery.
WPA2 ordinary-loss recovery is bounded by the original owning lease and target;
OPEN remains explicitly reconnect-only. The recorded native/over-air qualification
limits remain unqualified; accepting them does not establish the missing checks.
The [stage-2 record](../development/experiments/ax200-passive-scan.md) covers passive
RF; [stage-3 qualification](../development/experiments/ax200-open-ap.md) distinguishes
built/source-inspected behavior from native evidence.
Firmware, resources and ownership are in the [AX200 controller](../devices/ax200-wifi.md) reference.

The opt-in RTL8922AE firmware-first cut (`wifi.rtl8922ae=1`, image option
`WIFI_RTL8922AE=1`) uses this same READ interface. It admits the pinned immutable
format-4 asset, resets the reserved function, and attempts normal firmware
startup once on a dedicated BSP worker, then masks interrupts, performs PCI DMA
pre-deinit and MAC power-off before a checked terminal reset. Reset
admission prefers advertised PCIe FLR, otherwise PM D3hot-to-D0 with NoSoftReset
clear. Missing reset support or unknown/malformed required live inputs refuse
activation. Successfully read live cut/security and board/PHY facts, retained
DMA/ring backing and firmware/stop outcome are reported without raw identity.
PREPARED describes preparation; firmware readiness alone keeps WIFI_READY clear.
The set-once country/channel policy is accepted for input inspection; scan and
connection requests remain unavailable and no RTL network interface is published. A clear RFKILL flag does not establish an observed
unblocked radio, and firmware startup does not establish RF silence. Published
backing and uncertain ownership remain retained until reboot; this cut has no
automatic retry. Native qualification remains separately scheduled. Selecting
both Wi-Fi boot options refuses preparation without touching either radio. See
the [RTL8922AE first-cut contract](../wip/rtl8922ae-wifi.md#stage-2-firmware-transport-before-scan).
`WIFI_STATUS.boot` carries validity-tagged reset support/selection, live cut,
security mode, selected firmware version and board/PHY facts. Invalid groups are
zero; absence is not an unsupported-device observation. Its independent state
bits retain completed startup, firmware readiness, DMA pre-deinit, MAC power-off
and PCI stop observations even if a later step fails. `wifi status` prints these
facts; the driver does not emit duplicate boot-fact log lines. Consumers must
be rebuilt with the current SDK when the status layout changes.
READ authority is unchanged. Its terminal detail distinguishes successful
firmware readiness and complete stop from a refusal. RFKILL is the last successful pre-reset GPIO observation, not continuous
monitoring after the worker returns; pending/failed startup does not establish it.

`WIFI_STATUS.radio` reports RTL input observations independently of RF readiness.
The pinned wireless-regdb rules cover the entire occupied 20 MHz channel, with
EIRP in mBm and all rule flags retained. Omitted country uses world `00`; an
explicit country selects its own rules, intersected with the host channel mask.
World channels 12–13 retain NO_IR. A matching rule is not a calibrated TX limit.
Table state describes structural decoding and records explicit reference RFE-0
selection. ACPI capture and individual Realtek SAR/antenna/TAS sources distinguish
pending, valid absence, valid data, unsupported, malformed and failed reads.
VALID means decoded input, not applied limits or RFK qualification; raw platform
tables stay private. These observers add no RF commands.

## Authority and startup

`PROTOCOL_WIFI` has independently attenuable READ, SCAN and CONNECT rights. READ returns
presence/preparation/rfkill/failure, actual DMA and ring/counter status; it never
starts a scan or reveals nearby identities. SCAN starts one explicit operation.
The private POLICY_ONCE right belongs only to stock boot init: it cannot be
copied, forwarded or inherited. On AX200, an optional explicit uppercase country
(not ZZ) and channel mask narrows passive permissions once. Association requires the installed channel policy plus checked live firmware/NVM
and platform transmit limits; a passive bitmask alone authorizes no TX. Country
is a host hint, not an MCC setter or a country-equality admission check. The
firmware LAR profile is authoritative; a mismatch is logged without refusal.
Country `00` (`WIFI_COUNTRY_WORLD`) selects wireless-regdb's world domain, the
rules used when no country is configured, with the same meaning for every
driver ([RTL8922AE](../wip/rtl8922ae-wifi.md)). On AX200 it is no country hint,
so nothing is compared with LAR; NO_IR channels 12 and 13 transmit only where
the LAR profile marks them active. Boot init installs `00` with channels 1-13
on the Install entry only, which reads no configuration
([decision 70](../wip/pkg-upgrade.md#decided)); an installed boot installs its
`wifi_policy`, whose country stays an explicit code.

AX200 passive listening follows the current validated firmware LAR/MCC profile even
without a configured country. Its first fourteen channel entries are 2.4 GHz;
VALID admits passive listening, ACTIVE is also required before TX. The live LAR
map replaces NVM's regulatory defaults, while hardware band/antenna capabilities,
platform power limits and rfkill remain gates. An explicit boot channel mask can
further narrow the set. A missing, rejected or malformed profile logs a passive-only
1–11 fallback and clears TX permission. That fallback is an engineering listen-only
profile, not the regulatory channel set for Romania or Germany. An uncertain
command/transport completion still quarantines the controller.

Internal MCC `ZZ` is a firmware query, not a Pyxis country policy. A bounded MCC
change hint queues a profile refresh outside RX delivery with the command slot
free, including while a station is running. Repeated hints are coalesced;
a bounded burst retires a link cleanly instead of disabling the controller.
Each valid response replaces the previous map, so a firmware-permitted
channel may become available again; it never installs a host country. New scans
are refused while restrictions are unresolved, and queued scans revalidate before
submission. If an update removes an in-flight channel, the scan aborts/drains and
returns UNAVAILABLE without a partial FILE. Unknown channel flags do not authorize
use; indoor/DFS/environment restrictions without a qualified TX interpretation
remain listen-only. Firmware and source notices/pins are in `firmware/ax200-wifi/`.

Boot configuration opts individual spaces in with
`wifi = { read = true, scan = true, connect = true }` (each boolean independently optional).
Stock configurations omit all three. Trusted session, Remote and mux startup carry
only the selected rights. The `wifi` bundle declares each right separately;
status therefore works with READ alone. No call-site grants are manufactured.
No nearby identifiers are written to kernel/trace logs. Explicit `wifi scan`
output escapes counted SSID bytes and prints BSSIDs only to its caller.

## Operation and result

`WIFI_SCAN_START` takes an optional passive channel subset and absolute monotonic
deadline. Zero selects the admitted set / 15-second maximum. Another controller
scan returns BUSY; not-ready/rfkill returns UNAVAILABLE. Admission reserves the
operation, maximum result backing and lineage before firmware effects.

The returned `PROTOCOL_WIFI_SCAN` capability is process-owned and cannot be
copied, moved, forwarded or delegated. STATUS observes; CANCEL and logical CLOSE
request cancellation. Exit also cancels before process-memory teardown. The BSP
worker aborts/drains asynchronously. `wait_many` COMPLETE means terminal drain
or quarantine, reserving nothing. TAKE is nonblocking: WOULD_BLOCK before
completion, otherwise the actual terminal status; local handle admission failure
is retryable. Success transfers one READ-only immutable native FILE once.

The FILE contains `wifi_scan_file_header` then exactly `count` 52-byte
`wifi_bss_record`s (ABI in `abi/wifi.h`). SSIDs are counted bytes; records are
bounded to 128, keyed by BSSID/channel. Duplicate observations retain strongest
signal and the first nonhidden name. Bounded RSN security flags report a valid
RSN element, PSK/SAE AKMs, pairwise/group CCMP and PMF/SPP requirements; they
are passive, unauthenticated beacon evidence, not proof of AP identity. Missing,
malformed, duplicate or conflicting RSN observations cannot satisfy the WPA2
precheck. The connection revalidates the selected target independently. Explicit
`wifi scan` prints these flags as `security=<decimal>` after the counted SSID.
Overflow is FILE_TOO_LARGE, without a
published prefix. A complete FILE can be deliberately delegated normally.

Cancellation/deadline/CUT do not release DMA. Firmware must emit a matching
terminal completion and drain; an iteration notification alone is insufficient.
Iteration v1/v2 share the checked 16-byte UID/count/status/channel/timestamp
prefix; optional advisory tail bytes are not interpreted. Terminal v1 still
requires its exact 16-byte body, matching UID, schedule/iteration and status.
Uncertain stop or firmware fault disables this controller until reboot, retains
all published DMA and reports OUTCOME_UNKNOWN. No automatic retry/reset or rfkill override.
Firmware sections borrow page-aligned slices of one owned firmware DMA
allocation; they do not each consume a kernel VM range. The ten fixed DMA
buffers (including one 2 MiB RX region) remain separate. Before AP startup, a
failed preparation releases all unpublished DMA owners and CPU MMIO mappings;
uncertain PCI ownership stays retained. Runtime failure never uses that unwind.
Other boot device preparations and core service/presentation stacks take priority
over optional Wi-Fi preparation/worker creation.

The 128-record/15-second bounds are initial tuning; abort drain has a separate
bounded two-second cleanup window. READ status remains available after failure.

## Connection attempts and leases

`WIFI_CONNECT_START` copies a bounded `wifi_target` and an absolute deadline
(default/max 30 seconds). The target contains counted SSID bytes, unicast BSSID,
a supported 2.4 GHz channel and explicit OPEN or WPA2-PSK security.
Trailing/reserved bytes must be zero. WPA2 also captures a sealed credential.
The selected BSSID is confirmed passively; unrelated BSSIDs, including others
with the same SSID, are ignored before consuming target-scan table capacity.
OPEN refuses secured/OWE targets; WPA2 requires compatible RSN/PSK/CCMP.
Hidden or incompatible changed profiles are refused. ERP protection,
short-preamble and short-slot changes update the MAC context outside RX delivery;
they do not by themselves disconnect the station. Duplicate data is suppressed
only when Retry is set and its sequence matches a bounded recent cache, with
group and unicast delivery tracked separately. An explicit boot country is installed once from archive
`wifi_policy = {country="<CC>", channels=0x3fff}`; neither the CLI nor its profile
can set it. Its country does not override firmware LAR: CONNECT queries the
current firmware profile, logs a configured/firmware country mismatch, and uses
the checked firmware map. Missing, unknown or unsupported limits and rfkill
refuse TX.

The local `PROTOCOL_WIFI_CONNECT` operation is process-owned, noncopyable and
nondelegatable. STATUS observes; CANCEL/final close/exit cancels. COMPLETE means
setup has a final result; failed setup is reported only after drain/quarantine.
TAKE reserves local admission before moving one `PROTOCOL_WIFI_LINK` grant.
Local admission failures preserve the result; successful collection is once.
A second controller attempt/link returns BUSY and does not replace it.

A link has independently attenuable READ (STATUS and wait CLOSED) and CONTROL
(DISCONNECT and owning lifetime). Only CONTROL aliases keep the radio connection
alive. Its driver/storage reference and READ observers do not. A derived owning
grant is prepared before radio effects, preserving the initiating CONNECT
lineage even while the result is held inside the attempt. Final owning close,
CUT or explicit disconnect closes data admission and asynchronously drains.
The closing execution group waits for that drain; a successfully transferred
lease can outlive the short-lived command/capture subgroup.

Trusted boot startup creates a revocable radio root for each configured session
with CONNECT. Its CUT-only `wifi_session_control` remains private through trusted
session/shell, mux and Remote successors; ordinary automatic launches never get
it. Remote connections share the configured server's root. Final configured-session
root close cuts derived links. Explicit user delegation still uses held rights;
no process acquires radio authority by naming a profile or interface.

`wifi connect --profile FILE` parses finite native literal table data (4 KiB),
requires an empty typed capture, and transfers the owning lease without a resident
helper. Status exposes generation/interface/controller identity and
attempting/associated/open/data-ready/terminal state, never nearby SSID/BSSID.
OPEN means unencrypted and unauthenticated; DATA_READY is L2, not IPv4 readiness.
`wifi disconnect $link` waits for retirement. Uncertain firmware stop fences the
device, retains DMA and disables Wi-Fi until reboot.

Typical explicitly configured use:

```text
link=$(wifi connect --profile home://wifi-test.lua)
wifi status $link
net
wifi disconnect $link
unset link
```

For OPEN, a clean AP-loss retirement permits a later fresh CONNECT on the same
boot; it does not reconnect or replay automatically. WPA2 ordinary AP loss keeps
the owning lease and retries the same BSSID/channel with fresh authentication;
channel changes require a new explicit profile. Native evidence and unrun edge cases
are in the [stage-3 record](../development/experiments/ax200-open-ap.md) and
[qualification debt](../technical-debt.md#ax200-open-link-qualification-follow-ups).

## Station/data limits

The BSP radio worker owns firmware, station and DMA lifetime. The network worker
exchanges copied bounded Ethernet frames (MTU 1500), with finite TX/RX queues,
one hardware TX ring and backpressure. No firmware wait runs in the network
worker or interrupt handler. Only non-QoS, nonaggregated, unfragmented legacy
20 MHz traffic is admitted; management exchanges are bounded and checked against
the explicit target. AP loss, deauthentication, target/profile/channel changes
and unresolved regulatory changes stop the current association. WPA2 ordinary
loss may retry the same explicit target, never roam or weaken its security.

Known enabled SAR limits are sent before TX. Unknown/locked/malformed platform
policy refuses; enabled PPAG also refuses without proven platform applicability.
There is no guessed power profile or UEFI-runtime fallback. Source/licence
provenance and resource measurements live in the stage-3 record and Wi-Fi NOTICE.

AX200 binds uniquely to configured `wlanN`. Its network incarnation is independent
of the lease generation, advances on admission and retirement even without an
address, and never wraps. Wi-Fi REPLACE/CLEAR/DHCP-report operations carry that
exact sampled incarnation; stale work is UNAVAILABLE. REPLACE additionally needs
current data admission. Ethernet uses generation zero. Retirement clears the
address/DNS contribution, invalidates concrete flows and fences queued data.
Readiness distinguishes OPEN from confirmed WPA2 authentication. A bounded rekey
pauses ordinary admission with backpressure while preserving the L3 incarnation. DHCP leases retain their
work's incarnation; a later ACK cannot select a newer one. Userspace observes
changes through existing 100 ms maintenance polling (tuning, not a latency
contract); its native CPU cost remains unmeasured.


### ACPI policy sources

Read-only companion discovery does not depend on BAR-allocation approval. A
unique enabled root and unique intermediate PCI bridge chain identify the
physical function. Up to 16 enabled final endpoint aliases at that exact _ADR
contribute policy; no first-node selection or AML-name exception. Proved absence
on one cannot erase a present source on another. Repeated validated tables and
Intel _DSM function values must agree; conflicts, failed evaluation, unsupported
required sources and exhausted bounds refuse TX. Query support combines disjoint
implemented functions, not conflicting function values. GLAI is checked across
all aliases before reading ACPI RF tables; unread locked UEFI policy still refuses.
The immutable result contains no AML pointers. Existing country/NVM/firmware
transmission gates are unchanged.

Wi-Fi DHCP lease publication also enforces the
[connected-route and address-collision safeguards](../devices/network-interfaces.md#wi-fi-lease-admission).
An open link is not a trusted network; the future network trust-profile policy
is a direction, not current isolation.

## WPA2 auth and credential boundary

`WIFI_SECURITY_WPA2_PSK` selects WPA2 on the same CONNECT
operation. It never downgrades a protected target to open. The CONNECT request
carries an additional credential FILE handle (zero for OPEN). It captures native
READ authority on an immutable, erased-on-release RAM credential snapshot of the
exact `wifi_credential` shape; an exported or ordinary mutable FILE is refused.
Both the original CONNECT and credential CUT lineages bound the owning lease.
The credential is not a path, shell variable or profile/environment text.

`WIFI_CREDENTIAL_SEAL` requires CONNECT and copies only the caller's supplied
passphrase (8–63 bytes) or explicit raw PMK (32 bytes) into that anonymous backing.
It publishes a READ-only FILE after reserving handle admission, wipes copied
request bytes and erases all backing pages on final release. The caller must
wipe its own source. This constructs a caller-owned item; it grants no access to
another credential and is not a persistent credential service.

The separate private `WIFI_RIGHT_SUPPLICANT` setup right is absent from ordinary
`WIFI_RIGHTS`. Trusted boot delegates only this private opener on the configured
session's revocable root; the session uses it to obtain fresh atomic RUN/OBSERVE
auth pairs. The ordinary radio grant retains only
the declared READ/SCAN/CONNECT masks. `PROTOCOL_WIFI_AUTH` RUN is claimed by one
trusted leaf process; after CLAIM it cannot be copied/delegated. OBSERVE can
query status and wait for closure, but cannot read events, send EAPOL or manage
keys. Neither owns a link. Closing RUN or its worker, or cutting the configured
root, closes admission and wakes the radio to drain.

READ is nonblocking (`WOULD_BLOCK`) and transfers copied BEGIN/EAPOL/DISASSOC
events. BEGIN carries one owned READ credential, selected target/RSN IEs and a
fresh association generation/deadline. It reserves a local handle before dequeue;
a local limit leaves the event retryable. No provider calls or borrowed ring
pointers survive. A full EAPOL event queue drops the newest frame and increments the RX-drop
counter; an unauthenticated burst cannot close the broker. SEND, KEY, AUTHORIZE and FAIL admit one copied command at a time (`BUSY`
before effects) into the sole BSP radio worker. EAPOL carries at most 1500 octets (the copied event envelope is 2048 bytes).
Commands check generation, root and worker ownership. A parked command owns no unbounded caller buffer, and copied
key/request bytes are erased after confirmed completion or safe retirement.

The worker uses upstream RSN/WPA2-PSK/CCMP with PMF disabled. Required-PMF/SAE-only
APs are refused; optional-PMF transition-mode APs may negotiate WPA2. The driver
independently requires confirmed PTK/GTK before controlled-port authorization.
`WIFI_LINK_AUTHENTICATED` distinguishes that state from `WIFI_LINK_OPEN`; ordinary
IP/DHCP/ARP remains blocked until usable. EAPOL is not exposed as a raw network
socket. Protected frames rejected by the security check, including unknown key
IDs, malformed CCMP, MIC failures and replayed PNs, are dropped and counted without
retiring the connection. Uncertain installed
key state closes the port; ordinary AP loss is separately
recoverable under the same held owning authority. Initial/recovery authentication
and rekey are bounded; explicit disconnect/final owning close/CUT stops recovery.

The [controller reference](../devices/ax200-wifi.md#wpa2-personal) describes the
source/profile and recovery policy; the [qualification record](../development/experiments/ax200-wpa2.md)
distinguishes native, QEMU, host-fixture and source-inspected evidence.

Worker supervision uses a session-private SUPPLICANT-only opener, consumed from
trusted startup and excluded from ordinary launch inheritance. The session
creates a fresh auth broker/leaf worker with bounded backoff after worker loss;
optional authentication failure does not stop Ethernet or the session. The old
controlled port stays closed and its keys are retired before selecting a newly
claimed broker through the held link's unchanged lineage. The owning link and
credential then drive automatic same-target reconnection with fresh handshake
state; final close/CUT or uncertain key retirement still stop it (chronium,
2026-10-21). A replacement never reuses a dead worker's claimed RUN grant.
QEMU qualifies worker replacement/broker lineage; recovery of a held native
radio link after worker death remains unqualified.

After confirmed key/station retirement, a missing replacement authentication
worker has a 60-second wait bound (two maximum session-backoff intervals).
Expiry terminates the held link with `CALL_TIMED_OUT`; it does not retain the
sole radio worker indefinitely. Pending firmware regulatory refreshes are retried
while the port is closed, and fresh transmission still revalidates policy.
Unprotected WPA2 EAPOL does not enter the 802.11 retry cache; upstream RSN handles
its handshake replay/duplicate rules.
