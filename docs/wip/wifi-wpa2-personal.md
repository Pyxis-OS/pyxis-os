# AX200 WPA2-Personal

Status: accepted contract, owner decisions 2026-10-20; implementation assigned
(chronium, 2026-10-21) after 0.0.7. This is
[Wi-Fi stage 4](wifi.md#qualification-and-proposed-stages), following the
[open-link implementation](../interfaces/wifi.md). It proposes one explicit
2.4 GHz RSN/WPA2-PSK connection, CCMP-128 for pairwise and group traffic, and
existing firmware regulatory/platform gates. Ethernet remains management/default/
DNS. Ordinary AP loss/drop reconnects to the same configured network with the
held key while owning authority survives; no roaming, WPA1/TKIP/WEP, enterprise
EAP, SAE, FT, PMKSA reuse, WPS, P2P, mesh, MLO or new band. No silent security fallback.

## Source and native adapters

Pin upstream **wpa_supplicant 2.12**, BSD-3-Clause, release archive
[`wpa_supplicant-2.12.tar.gz`](https://w1.fi/releases/wpa_supplicant-2.12.tar.gz):
SHA-256 `08e23937e16d0155e55cab2b51f51fbe10d80a1aa91c4e15442645059b737ef6`
([FreeBSD distinfo](https://github.com/freebsd/freebsd-ports/blob/main/security/wpa_supplicant/distinfo),
read through the owner cache). The earlier unpinned FreeBSD sample is inspection
material, not this source pin. Upstream identifies 2.12 as fixing the
[2026 parsing](https://w1.fi/security/2026-1/missing-ml-parsing-validation.txt) and
[PMKSA-context](https://w1.fi/security/2026-2/missing-network-context-validation-for-pmksa-caching.txt)
issues. Mirror the full archive and detached signature before builds; verify hash,
signature and reached-file licences, including the internal crypto closure. No upstream download fallback or vendoring now.

Build the upstream `src/rsn_supp` WPA state machine and its reached RSN IE/KDE,
common, utility and crypto closure as a private library in a trusted leaf
supplicant worker. Keep four-way/group-key handshakes and replay/reinstallation
protections. Do not import the generic daemon, config-file writer, control
socket/D-Bus, Linux/BSD socket drivers or a second kernel handshake state machine.
Exclude unsupported features at build and negotiation boundaries; the full
2.12 build closure still needs the first implementation audit.

| Adapter | Proposed native boundary |
| --- | --- |
| Driver | One association-generation-bound auth capability: selected-AP EAPOL, CCMP key install/remove and controlled-port authorization; no MMIO, raw 802.11 or arbitrary network access |
| Events/OS | Owned receiver + `wait_many`, monotonic absolute deadlines, native allocation and explicit erasure; no fake socket/select/signal operations |
| Random | Declared kernel random/clock grants; no entropy fallback or predictable nonce; bounded native reads within the handshake deadline |
| Config | One copied explicit target and credential item, with strict RSN/AKM/cipher validation; no global `wpa_supplicant.conf`, config writeback or environment discovery |
| Crypto | Pinned upstream internal SHA-1/HMAC/PBKDF2/PTK-PRF, EAPOL MIC and AES key-unwrapping closure; kernel CSPRNG replaces the optional userspace entropy pool; no new crypto construction or OpenSSL dependency |

Preserve upstream callback/key flags and error handling. Unsupported algorithms,
key activation or command versions fail explicitly, never successful stubs.
The ports recipe owns source/pin/notices; userland owns adapters/worker; Pyxis
owns auth/key capabilities, AX200 and controlled-port enforcement.

## Credential authority and lifetime

Accepted: explicit **local no-echo provisioning**, volatile until configured-session
end. The user enters a passphrase or PMK outside shell command parsing; the
trusted provisioner seals one anonymous private RAM credential FILE before
publishing it to the selected worker through declared item-scoped READ authority.
Accept an 8–63-byte passphrase or a raw 32-byte PMK with an explicit format tag;
no mutable backing or unbounded input. It is reachable
from no filesystem root/cwd/alias and is not an ordinary shell binding. A hidden
filename or Unix mode is not privacy. No credential bytes in argv, environment
text, Lua/profile/archive, history, stdio, status/describe/error bodies, logs or
raw EAPOL/key dumps at any level. Public profiles contain only nonsecret target
selection; examples use placeholders, never the owner's SSID or key.

Trusted startup owns the leaf worker's execution group and delegates auth scope
for its configured session; the worker has no launcher or directory-root grants.
An attempt's private auth facet binds the initiating CONNECT lineage and exact
AP/generation. Ordinary CONNECT/link/network holders receive no credential or
key-management authority. Auth observers/worker references do not keep owning
links alive. Final link close/CUT cancels auth and retires keys; worker exit or
auth-channel closure closes data admission. Session end cuts links and stops the
worker. Nothing is left owned only by the short-lived `wifi connect` command.

The private credential item can authorize a fresh explicit connection during the
session, including automatic recovery from ordinary AP loss/drop. Reboot/session
end requires entry again: this is a usability cost, not a persistence solution.
The worker erases transient passphrase/PMK/PTK copies when no longer needed; final item release explicitly
erases its backing. This must be enforced, not inferred from ordinary allocator
freeing. Erase copied request/command/key buffers after safe consumption/drain.
Only temporal keys and replay counters enter the driver, never the passphrase or
PMK. This trusts kernel/boot/debug administrators; it is not encrypted RAM or
at-rest security. The parked [credential service](credentials-and-biometrics.md)
is the direction for stored credentials and explicit item-use/raw-export
authority; no TPM, biometric unlock, persistent vault or placeholder
credential-store API now.

## EAPOL, keys and the data port

Association alone is not authenticated readiness. A bounded copied EAPOL channel
accepts only EtherType `0x888e` for this AP and association generation. It is not
a general raw-packet grant and cannot be reached through ordinary TCP/UDP.
Before authorization, only the upstream handshake's required control traffic is
allowed; DHCP, ARP and ordinary IP/data remain blocked in both directions.

The BSP worker owns CC77 key commands and DMA. Check firmware API/command versions
before selecting the pinned [iwx reference](../devices/ax200-wifi.md#reference-source-and-firmware)
`ADD_STA_KEY` layout (v2: 76 bytes, not v1's 64),
and require confirmed success; do not assume the newer `SEC_KEY_CMD` layout.
Implement CCMP TX with firmware IV/PN ownership, replacing today's unconditional
encryption-disable flag. RX requires protected CCMP, decrypt completion and MIC
success, correct IV/padding bounds, and **host per-key/per-TID 48-bit PN replay
checks** as in pinned iwx; hardware decryption alone is insufficient. Bound PN
lifetime to key identity, reject wrap, and never reset counters on retransmission.

Open the port only after upstream handshake completion **and** confirmed PTK/GTK
installation. A link result becomes usable only then; status distinguishes
association, authentication and data readiness. Never pass plaintext ordinary
frames on a protected link, including after key loss. The kernel independently
checks key presence/generation before honoring authorization.

Group/pairwise rekey uses upstream MIC/replay validation and bounded key slots.
Exclude extended key ID and RX-only PTK activation in this slice. The legacy
reference fixes PTK/GTK slots; do not assume overlapping PTKs. Pause ordinary
admission while keeping EAPOL available, drain TX and use the verified replacement
sequence; retain an old GTK only where checked key-ID/slot semantics allow it.
Unsupported activation fails closed. Replayed message 3 must not reinstall a key
or reset PN. Resume only after confirmation, keeping the same link incarnation
on success. On loss/drop or failed authentication: close the port first,
invalidate L3/queued traffic, drain TX, retire temporal keys and erase their
material. Ordinary AP loss/drop automatically reconnects to the same configured
network with the held credential, fresh nonces/keys and a new association
generation; data stays closed until authentication completes again. Recovery
remains scoped to the original target/owning lease and configured session;
explicit disconnect, final owning close, CUT or session end stops it. Never widen
AP selection or revive revoked authority. Report authentication rejection rather
than falling back to weaker security. Bound each attempt and back off recovery
attempts; no busy loop or unbounded queued data.

Fail closed on real security uncertainty (MIC failure, replay or unexpected key
state), helper/auth-channel loss, or uncertain firmware/key retirement. The last
case keeps DMA and disables Wi-Fi until reboot; do not label an ordinary lost AP
as such uncertainty. Initial authentication uses the existing 30-second CONNECT
budget; recovery attempts and rekey have bounded deadlines, with precise timing
recorded during implementation. Ethernet stays usable during failure/recovery.

Accepted PMF stance: **not negotiated in this WPA2 slice** (no MFPC/MFPR); refuse
PMF-required/SAE-only APs. An AP offering optional PMF may negotiate WPA2 without
it. A WPA3-Personal + WPA2-PSK[AES] transition-mode AP with PMF capable but
not required must work as a WPA2 station. Unprotected deauthentication remains
spoofable; this is not WPA3/PMF delivery.

## Delivery and qualification after the tag and owner go

1. Mirror/audit the pinned source closure; build the native adapters and settle
   bounded copied event/key layouts. Capture fresh open-link/wired/idle controls
   with existing tools before changing the data path, only in a quiet window.
2. Implement key/auth transport and closed-port invariants, then integrate the
   supplicant and private credential provisioning. Update SDK, startup, `wifi`/
   `net` reporting and reference docs together; linked dependency PRs, exact pins.
3. QEMU checks adapters, crypto/state-machine fixtures, authority, cancellation
   and retirement; no QEMU RF claim. Route lifetime/adversarial work through the
   orchestrator/Daybreak. Native ThinkPad, wired Remote primary, private WPA2-PSK
   AP with CCMP pairwise/group and PMF optional/off: correct key + DHCP/scoped
   hashed payload; wrong key/no usable port; explicit reconnect and automatic
   recovery after an ordinary AP loss/drop, both with fresh nonces/generation;
   AP-triggered group rekey with continued protected payload;
   interrupted handshake/worker exit/final-close/CUT and fresh recovery.
   Confirm replayed handshake messages cannot reset keys/PN. Capture over-air
   controlled test traffic with a suitable independent monitor to establish no
   plaintext ordinary data before/after auth, rekey or failure; driver counters
   alone are not proof. If unavailable, leave that gate open. Keep credentials,
   EAPOL/key dumps and nearby identities out of published artifacts. Repeat
   matched wired/data-path costs; report native versus nested and all limits.

The owner's native AP is WPA3-Personal + WPA2-PSK[AES] transition mode, PMF
capable and not required. For the native fixture, read the passphrase only at run
time from the owner's private mode-0600 host file, outside all checkouts and
shared directories. Enter it only through the ordinary Wi-Fi no-echo prompt over
Remote (chronium, 2026-10-21), with client echo and secret-input recording disabled.
Do not record the secret-entry interval in transcripts; discard/redact any
capture containing it before storage. Never copy it into staged packets, images,
argv/environment, logs, PRs or documentation; it is input data, never a shell
command. No TLS fixture or separate provisioning path. This temporary native-run
exception adds no persistent Pyxis credentials; chronium will change the test key.

## Owner decisions (accepted 2026-10-20)

1. **Subset/PMF:** upstream 2.12 RSN/WPA2-PSK + CCMP-128, internal crypto/native
   adapters and hardware CCMP with host replay checks; PMF off and required-PMF
   APs refused; transition-mode WPA2 with optional PMF is supported. PMF/SAE
   remain stage 6.
2. **Credentials/owner:** local no-echo, anonymous private RAM item, declared
   worker-only raw READ, session-owned leaf worker and explicit erasure; no disk
   persistence. Re-entry after reboot/session end is accepted friction; stored
   credentials follow the parked credential direction. The native-only Remote
   prompt exception above does not authorize a general secret-export service.
3. **Failure/rekey:** gated data until handshake+keys, bounded in-place rekey,
   automatic reconnect to the same network with the held key after ordinary AP
   loss/drop; fail closed for real security uncertainty (MIC, replay, unexpected
   key state). Preserve owning-lease/session cancellation and keep Ethernet usable
   throughout.
