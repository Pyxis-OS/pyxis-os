# Wi-Fi controller, scan and open-link operations

The native AX200 implementation is opt-in (`wifi.ax200=1`, image option
`WIFI_AX200=1`). It prepares firmware/DMA once at boot on a dedicated BSP
worker. It supports passive channels 1–11 and one explicit legacy open-station profile.
There are no active probes, WPA, roaming, automatic reconnect or reset recovery.
The [stage-2 record](../development/experiments/ax200-passive-scan.md) covers passive
RF; [stage-3 qualification](../development/experiments/ax200-open-ap.md) distinguishes
built/source-inspected behavior from native evidence.
Firmware, resources and ownership are in the [AX200 controller](../devices/ax200-wifi.md) reference.

## Authority and startup

`PROTOCOL_WIFI` has independently attenuable READ, SCAN and CONNECT rights. READ returns
presence/preparation/rfkill/failure, actual DMA and ring/counter status; it never
starts a scan or reveals nearby identities. SCAN starts one explicit operation.
The private POLICY_ONCE right belongs only to stock boot init: it cannot be
copied, forwarded or inherited. An optional explicit uppercase country (not ZZ)
and channel mask narrows passive permissions once. Association requires that installed policy plus checked live firmware/NVM and
platform transmit limits; a passive bitmask alone authorizes no TX.

Countryless passive listening uses channels 1–11 intersected with checked live
NVM/firmware restrictions. Installed policy can narrow that set. Unknown
restrictions and rfkill refuse. Internal firmware MCC `ZZ` is not a Pyxis country
policy. A bounded MCC change hint queues a profile refresh on the BSP worker,
outside RX delivery and with the command slot free. The verified response can
only narrow the retained channel mask; it never installs a country. New scans
are refused while restrictions are unresolved. If a valid update removes an
in-flight channel, the scan aborts/drains and returns UNAVAILABLE without a
partial FILE; the controller remains usable with the narrower set. Malformed,
excessive or uncertain refresh still fails closed. Firmware and source
notices/pins are in `firmware/ax200-wifi/`.

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

The FILE contains `wifi_scan_file_header` then exactly `count` 48-byte
`wifi_bss_record`s (ABI in `abi/wifi.h`). SSIDs are counted bytes; records are
bounded to 128, keyed by BSSID/channel. Duplicate observations retain strongest
signal and the first nonhidden name. Overflow is FILE_TOO_LARGE, without a
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

`WIFI_CONNECT_START` copies a bounded `wifi_open_target` and an absolute deadline
(default/max 30 seconds). The target contains counted SSID bytes, unicast BSSID,
channel 1–11 and explicit OPEN security. Trailing/reserved bytes must be zero.
The selected BSS is confirmed passively; ambiguity, secured/OWE, hidden or changed
profiles are refused. An explicit boot country is installed once from archive
`wifi_policy = {country="<CC>", channels=0x7ff}`; neither the CLI nor its profile
can set it. Missing, unknown or unsupported limits and rfkill refuse TX.

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

## Station/data limits

The BSP radio worker owns firmware, station and DMA lifetime. The network worker
exchanges copied bounded Ethernet frames (MTU 1500), with finite TX/RX queues,
one hardware TX ring and backpressure. No firmware wait runs in the network
worker or interrupt handler. Only non-QoS, nonaggregated, unfragmented legacy
20 MHz traffic is admitted; management exchanges are bounded and checked against
the explicit target. AP loss, deauthentication, target/profile/channel changes
and unresolved regulatory changes retire instead of roaming or replaying.

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
READINESS distinguishes OPEN from WPA authentication. DHCP leases retain their
work's incarnation; a later ACK cannot select a newer one. Userspace observes
changes through existing 100 ms maintenance polling (tuning, not a latency
contract); its native CPU cost remains unmeasured.
