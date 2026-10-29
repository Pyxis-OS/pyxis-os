# Wi-Fi controller, scan and connection operations

OPEN and WPA2-Personal/CCMP behavior is implemented. Native checks and their
limits are recorded in the [WPA2 qualification record](../development/experiments/ax200-wpa2.md)
and [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1285).

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

The opt-in RTL8922AE backend (`wifi.rtl8922ae=1`, image option
`WIFI_RTL8922AE=1`) reserves the function, validates the pinned format-4 asset
and prepares retained DMA backing at boot. READ does not start firmware or RF.
An explicit SCAN request elects the one startup attempt after effective regdb
and required platform/SAR inputs are available. WCPU plus BBMCU0 initialize
both MAC/PHY contexts and both RF paths. Fixed firmware/SAR/numeric-regdb limits
are applied before TX-capable PRE_NOTIFY/DACK/RXDCK/TSSI calibration. No probes,
association, null/data packets or ordinary automatic responses are enabled.
Both contexts calibrate on the lowest requested channel that permits initiation;
NO_IR-only startup refuses without borrowing a channel outside the request.

Default scans select the admitted channel 1–13 set; explicit subsets are exact.
The firmware period is 232 TU per channel within the existing 15-second deadline.
World channels 12–13 use Linux's NO_IR listen-only entries, without probes or
calibration there. Unsupported channel/rule profiles refuse before startup.
BE firmware offload completes the requested list; results publish only after
END/confirmed STOP and captured RX retirement. PPDU reports supply hopping
frame channels with separate correlation groups per BB. Unmatched metadata is
omitted, never relabeled from the last software ENTER notification. One-channel
requests retain their fixed-channel fallback. Results are passive, unauthenticated
observations; an empty completed FILE is valid.

Healthy firmware/DMA stays retained and idle RX is drained/dropped. Another
exact-mask request needs no startup/RFK; a changed mask is unsupported in this
slice. Uncertainty/rfkill takes the existing checked terminal-stop path, retaining
backing if stop cannot be confirmed. No RTL network interface or CONNECT path.
The common power image intersects every requested channel's signed ceilings before
RFK/START and is checked again after result collection closes. These historical
endpoint checks do not measure radiated power or prove continuous enforcement
inside closed firmware. Native qualification and remaining gates are in the
[RTL8922AE milestone](../wip/rtl8922ae-wifi.md#delivery-and-qualification).

After MAC power-on, the driver configures and checks active-low GPIO9 before
security/firmware startup. Current rfkill, cancellation and deadline checkpoints
bound long sleeps to 10 ms; inaccessible GPIO is never treated as unblocked.
The unpowered AFE/power-on portion cannot observe that live GPIO. Cleanup has
its own bounded deadline and is not cancelled: gate MAC activity, mask MSI,
pre-deinitialize DMA, power off the MAC and check the terminal PCI reset.
Reset admission prefers advertised FLR, otherwise D3hot-to-D0 with NoSoftReset
clear. Backing and late-interrupt context survive success or uncertainty until
reboot; no retry or spontaneous restart on rfkill unblock is provided yet.
Selecting both Wi-Fi boot options refuses preparation without touching either.

`WIFI_STATUS.boot` carries validity-tagged reset, live cut/security, selected
firmware version, board facts and independent startup/stop observations.
The extended `wifi_status_full_reply.phy` adds historical phase, selected channel/region, actual
power-image readback count and path SAR bounds in quarter-dBm, attempted GPIO
checks, RFK report/completion and checked-stop facts. They describe the attempt,
not current radiated power or scan readiness. Invalid numeric groups are zero;
stop-confirmed and stop-uncertain are exclusive. The final operation status is
valid only at COMPLETE. RFKILL is the last successful pre-reset observation,
not continuous monitoring after stop. `wifi_status_reply` keeps its released
544-byte layout. WIFI_STATUS accepts that minimum, writes
`min(capacity, sizeof(struct wifi_status_full_reply))` bytes and reports the
copied size. A smaller capacity returns BUFFER_TOO_SMALL without a reply.
The full reply composes the released prefix as `base` followed by the PHY facts;
partial extensions cannot be treated as a complete PHY observation. Libpyxis
`wifi_status()` requests the prefix, while `wifi_status_full()` requires and
validates the complete snapshot. READ authority is unchanged. Detailed facts
replace duplicate boot log lines.

The PHY state also retains validity-qualified startup history: initial WLMAC
state observed, active value seen, power-cycle attempted and power-cycle
completed. Completion means the startup power-off and ensuing fresh power-on
both succeeded, not merely that a second state read was non-active. Firmware
readiness and terminal cleanup do not set it. An unobserved initial state is
distinct from a sampled non-active state; neither is a physical RF-off proof.
The reply sizes stay 544/600 bytes. Matching SDK readers validate the additional
flags and their implications; no new command, authority or log producer is added.

MAC refusal details retain the first failed substep, BAR offset/access width,
observed value, expected value and comparison mask before cleanup. `wr` is an
exact write readback; `rd` is an inaccessible pre-read (the target fields were
not written); `en` is a required function-enable check. `poll` carries the last
value actually sampled, not a post-reset reread; `none` means the poll deadline
expired before any sample, so its zero observed value is invalid. The original
readiness checks, write comparisons and deadlines are unchanged.

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

`radio.tas` records the first precise rejection (query-0, function-5 or alias),
its observed object type and validity-tagged length/support mask. Alias conflict
is independent of the first rejection. TAS is disabled in the initial profile.
`radio.sar` reports a fixed 2.4 GHz envelope in eighth-dBm for paths A/B and
1TX/2TX, with the recognized WRDS/RWRD formats and profile counts. Every valid
RWRD profile constrains the result; present WRDS intersects it independently.
The CID-specific conversion includes the 2TX reduction. RWSI cannot choose a
less restrictive profile, and TAS cannot boost it. Bad required inputs produce
no numeric envelope; present geographic constraints remain unsupported until
interpreted. This remains a SAR input, separately from board/regulatory/antenna
limits and their programming or calibration enforcement.

## Authority and startup

`PROTOCOL_WIFI` has independently attenuable READ, SCAN and CONNECT rights. READ returns
presence/preparation/rfkill/failure, actual DMA and ring/counter status; it never
starts a scan or reveals nearby identities. SCAN starts one explicit operation.
The private POLICY_ONCE right belongs only to stock boot init: it cannot be
copied, forwarded or inherited. Country `"00"` names the world domain
(decided chronium, 2026-10-23); other countries use uppercase alpha2 excluding ZZ.
RTL uses the pinned regdb world domain when host policy is omitted. AX200's
live firmware LAR remains authoritative. Association requires the installed channel policy plus checked live firmware/NVM
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

The FILE contains the 24-byte `wifi_scan_file_header` then exactly `count` 52-byte
`wifi_bss_record`s (ABI in `abi/wifi.h`). SSIDs are counted bytes; records are
bounded to 128, keyed by BSSID/channel. Duplicate observations retain strongest
signal and the first nonhidden name. Bounded RSN security flags report a valid
RSN element, PSK/SAE AKMs, pairwise/group CCMP and PMF/SPP requirements; they
are passive, unauthenticated beacon evidence, not proof of AP identity. Missing,
malformed, duplicate or conflicting RSN observations cannot satisfy the WPA2
precheck. The connection revalidates the selected target independently. Explicit
`wifi scan` prints these flags as `security=<decimal>` after the counted SSID.
RTL8922AE keeps what fits when its 512 pending candidates or 128 final records
fill. Its successful immutable header carries `WIFI_SCAN_FILE_TRUNCATED` and
`dropped_records`; SCAN STATUS also reports `WIFI_SCAN_TRUNCATED`. The count is
capacity-dropped candidate observations, not unique BSS identities. UINT32_MAX
is a saturated lower bound; malformed/unmatched metadata is a separate omission.
Existing records can still merge after the final array fills. This bounded
truncation is not a firmware/transport failure. Other backend error handling is
unchanged. A result FILE can be deliberately delegated normally.

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
[qualification debt](https://git.internal/PyxisOS/pyxis-os/issues/1284).

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
