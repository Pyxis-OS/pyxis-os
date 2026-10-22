# AX200 WPA2-Personal

Status: accepted contract, owner decisions 2026-10-20; implementation assigned
(chronium, 2026-10-21) after 0.0.7. This is
[Wi-Fi stage 4](wifi.md#qualification-and-proposed-stages), following the
[open-link implementation](../interfaces/wifi.md). It covers one explicit
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
issues. The mirror-only archive hash and reached-file licences, including internal crypto,
are checked. The detached signature is retained, but signer-key trust has not
been established; signature verification is not claimed. No upstream fallback
or vendoring.

Build the upstream `src/rsn_supp` WPA state machine and its reached RSN IE/KDE,
common, utility and crypto closure as a private library in a trusted leaf
supplicant worker. Keep four-way/group-key handshakes and replay/reinstallation
protections. Do not import the generic daemon, config-file writer, control
socket/D-Bus, Linux/BSD socket drivers or a second kernel handshake state machine.
Exclude unsupported features at build and negotiation boundaries; the reached 2.12 closure is recorded in the ports recipe.

| Adapter | Native boundary |
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
end. The CLI prompts for a passphrase outside shell command parsing; the
trusted provisioner seals one anonymous private RAM credential FILE before
publishing it to the selected worker through declared item-scoped READ authority.
The SDK accepts an 8–63-byte passphrase or a raw 32-byte PMK with an explicit format tag;
no mutable backing or unbounded input. It is reachable
from no filesystem root/cwd/alias and is not an ordinary shell binding. A hidden
filename or Unix mode is not privacy. No credential bytes in argv, environment
text, Lua/profile/archive, history, stdio, status/describe/error bodies, logs or
raw EAPOL/key dumps at any level. Public profiles contain only nonsecret target
selection; examples use placeholders, never the owner's SSID or key.

The configured session owns the leaf worker's execution group under trusted
startup and holds its auth scope; the worker has no launcher or directory-root grants.
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
before selecting the pinned [iwx reference](../devices/ax200-wifi.md#reference-source-and-firmware).
The pinned CC77 image advertises `SEC_KEY` v1 (80-byte payload) and `ADD_STA_KEY`
v3: use the checked `SEC_KEY` profile with confirmed transport completion. The
legacy 76-byte `ADD_STA_KEY` v2 profile is used only for that checked version/API;
unknown versions refuse. Do not infer a payload layout from the firmware name.
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

Discard and count received frames that fail MIC/PN checks; these untrusted
frames do not invalidate an established key. Fail closed on real security
uncertainty in the authenticated handshake or installed key state, or uncertain
firmware/key retirement. Worker/auth-channel loss closes the port and drains old
keys before the accepted fresh-worker automatic recovery below. Uncertain
retirement keeps DMA and disables Wi-Fi until reboot; do not label an ordinary lost AP
as such uncertainty. Initial authentication uses the existing 30-second CONNECT
budget; recovery attempts and rekey have bounded deadlines, with precise timing
recorded during implementation. Ethernet stays usable during failure/recovery.

Accepted PMF stance: **not negotiated in this WPA2 slice** (no MFPC/MFPR); refuse
PMF-required/SAE-only APs. An AP offering optional PMF may negotiate WPA2 without
it. A WPA3-Personal + WPA2-PSK[AES] transition-mode AP with PMF capable but
not required must work as a WPA2 station. Unprotected deauthentication remains
spoofable; this is not WPA3/PMF delivery.

## Delivery and qualification

1. [x] Mirror/audit the pinned source closure, build native adapters and settle
   bounded copied event/key layouts. Nested before-code controls are captured;
   the native before-code baseline completed on 2026-10-21. Native idle/BSP accounting
   is unavailable with existing tools, as recorded in technical debt.
2. [ ] Implement and qualify key/auth transport and closed-port invariants, then integrate the
   supplicant and private credential provisioning. Update SDK, startup, `wifi`/
   `net` reporting and reference docs together; linked dependency PRs, exact pins.
3. [ ] QEMU and native qualification: adapters, crypto/state-machine fixtures, authority, cancellation
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
command. Remote currently uses unencrypted TCP: no echo prevents display/history
recording but does not protect the passphrase on the network. Chronium accepts
this native-test limitation (2026-10-21); use only the trusted wired test LAN.
No TLS fixture or separate provisioning path. This temporary native-run
exception adds no persistent Pyxis credentials; chronium will change the test key.
Read the private AP-selection file only at native run time as well (chronium,
2026-10-21). Keep its SSID and scan output in memory; do not copy them into
packets, logs, PRs or reports. Quiet Remote input also disables shell history.

### Worker recovery review decision (chronium, 2026-10-21)

The configured session retains a private SUPPLICANT-only opener on its existing
revocable Wi-Fi root. It creates fresh brokers/workers with 1–30 s backoff;
optional Wi-Fi startup or worker failure does not stop the session or Ethernet.
Uncertain worker-group cleanup retains ownership and disables replacement.
After worker loss, the radio first retires the old station/keys. The owning link
and sealed credential retain the selected network intent; once a fresh worker
claims a broker on that same root lineage, reconnect is automatic with a new
association, handshake/nonces and keys. No old key or handshake is resumed.
Final close/CUT and genuine security/retirement uncertainty still stop recovery.


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
   loss/drop; fail closed for uncertain installed-key state. Untrusted protected
   frames failing MIC/PN or other receive checks are counted drops, without
   retiring the connection. Preserve owning-lease/session cancellation and keep
   Ethernet usable throughout.

## Current qualification checkpoint

The ordinary image, SDK and worker/startup builds pass with the existing
`3d69e83` builder. Four-CPU nested QEMU starts both configured workers; GDB
observes one RUN holder each. Production-library PBKDF2, AES unwrap and HMAC
known vectors, plus synthetic credential seal/READ/refused WRITE/close pass.
The ordinary no-echo prompt accepts public fixture input and the absent AX200
returns an explicit failure; a fresh Remote shell still works. These are not RF,
full handshake, key-reinstallation, lifecycle/erasure or native cost evidence.
Raw local checks stay under `/shared/present/wifi-ax200-stage4/qemu-functional/`.

Before-code nested controls and the frozen native baseline packet are retained
under `/shared/present/wifi-ax200-stage4/baseline/`. Native baseline completed
on the ThinkPad, AC/onboard Ethernet/quiet horse, revision `d2cf7091`, builder
`3d69e83`, three 64 MiB samples per mode (2026-10-21):

| Mode | Median [min, max], MiB/s |
| --- | --- |
| TX 8 KiB | 103.5 [102.1, 104.2] |
| TX 2 KiB | 92.1 [91.5, 92.5] |
| RX | 99.7 [99.2, 100.1] |

Record: `baseline/results/baseline-20261021-092843`; PXE restored. The first
WPA2 attempt on `e869a7cc` failed before a usable link. Review found a stale
OPEN-only driver admission check; QEMU's absent-device check had masked it.
The old runner did not retain the failed stage or whether prompt entry occurred.
The corrected runner records only fixed stage/status/prompt-state metadata,
redacts interface MACs and omits raw scan and output frames from shared records.
QEMU confirms the exact profile-writing command with a public fixture; hardware
association had not yet been qualified at that checkpoint; the first native
success is recorded below. Rekey/recovery and over-air qualification remain pending. Native/PXE
actions are coordinated before staging; the independent over-air monitor gate
remains open if suitable hardware is unavailable.

### First native protected association (2026-10-21)

ThinkPad candidate `198593ee`, userland `a8d61de7`, ports `96a0cdf0`, builder
`3d69e83`: reboot/status, WPA2 association and independent wlan0 DHCP passed.
The held link reports `associated=1 open=0 data-ready=1 authenticated=1`.
Record: `next-review-198593ee/results/connect-20261021-101305` under the stage-4
packet; PXE restore verified by the orchestrator. Shared results contain no MACs
or AP identifiers. This is the first native WPA2 association, not a payload,
rekey/recovery, lifecycle or independent over-air qualification result.

The finite data check below uses existing `tcp` in an opt-in temporary bundle
with a declared wlan0-only CONNECT grant: deterministic 256 KiB upload/echo and
256 KiB download, hashes at both ends, fixture peer equal to the wlan0 DHCP
address, Wi-Fi frame-counter evidence and unchanged wired policy. It adds no
target program or API and establishes no throughput or over-air plaintext claim.


### Replacement-worker review check (2026-10-21)

Candidate `d32c985a`, with the same dependency heads and builder, builds and
passes the existing four CI jobs. Four-CPU q35/KVM/4 GiB QEMU used an ordinary
public-fixture CONNECT: GDB observed a live broker selected through the session's
restricted opener before the absent-device refusal. A single-task supplicant
was identified by its fixed argv and stopped at syscall entry through the task
stop flag. Normal retirement closed its broker; the session launched a fresh
worker, whose claimed broker a second CONNECT selected successfully. The shell
remained usable. Records: `qemu-functional/lows-worker-{gdb.txt,public.json}` under
the stage-4 packet directory. No private native input was used.

This qualifies supervised worker replacement and initial/replacement broker
lineage on QEMU, not a held radio link's reauthentication or key retirement.
The missing-worker 60-second bound, retry of pending MCC refresh during that
wait, and exclusion of unprotected WPA2 EAPOL from the retry cache are inspected
changes; their RF paths are not exercised by the absent-device check. Native
network GDB is read-only, so it cannot perform the worker-termination injection.
The protected-payload packet is `next-recovery-d32c985a`; its native result is
recorded below. Native rekey and recovery qualification remain.


### Native protected payload (2026-10-21)

ThinkPad candidate `d32c985a`, userland `a8d61de7`, ports `96a0cdf0`, builder
`3d69e83`: packet check, verified staging, reboot and status passed. The scoped
payload check completed both 256 KiB upload/echo and download hashes, proved the
fixture's source was wlan0, and preserved Ethernet addressing and policy. Data
counters advanced by 432 completed TX frames and 573 queued RX frames. The held
WPA2 port remained authenticated; this is a correctness result, not throughput
or independent over-air evidence.

Records under `next-recovery-d32c985a/results/`:
`reboot-20261021-114206` and `payload-20261021-114321` (including
`route-evidence.json`, `fixture.json` and `PAYLOAD_DONE`). The orchestrator
verified byte-for-byte PXE restoration. Shared records contain no AP identifiers
or credential input. This run qualified protected payload; the subsequent batch
below extends negative/lifetime coverage. QEMU worker replacement does not
substitute for held-radio recovery. Repeat matched wired controls before closing
the stage.


### Remaining coordinated qualification

The hands-free batch below passed generated wrong input through the ordinary
prompt, owning aliases/final close, held-link wrapper CUT, Remote-close
retirement, trace-confirmed handshake interruption and a fresh scoped payload. Passive scan admission confirms the sole radio worker is reusable;
these observed checks do not prove allocation/erasure completeness or initiating
radio-root CUT. Native/PXE actions remain scheduled through the orchestrator.

Trace-only events report association generation and confirmed CCMP key installs
(group flag/key ID only). Same-key no-op/replayed installs emit no install event.
A bounded rekey watch needs an additional GTK install without a new association,
followed by authenticated readiness and protected payload. Chronium confirmed
Group Key Update Interval = 0 on the native AP (2026-10-21): no spontaneous GTK
update occurs, so native group rekey remains **unqualified**. Do not run the watch
or change that router setting. Revisit on a suitable test AP with a nonzero
interval, or with separately approved controlled rekey testing. No keys, packets
or AP identifiers are logged. Daybreak's host replay/key-reinstallation results
and their native limits are recorded below.

For AP loss, a separate guest-network procedure is accepted (chronium,
2026-10-21), replacing the one-client-disconnect proposal. Its separate private `guest.ssid`
and `guest.psk` host files must be owner-held regular mode-0600 files, read only
at run time. Before credential input, passive RSN evidence must include PSK with
CCMP pairwise/group ciphers, and must not require PMF/SPP. SAE-only, ambiguous
or malformed evidence refuses without reading the passphrase. This permits a
WPA2/WPA3 transition BSS only when its WPA2 subset is supported. The
runner holds one lease through loss/recovery over one reverse-beacon Remote
connection; Ethernet stays primary. After initial authentication/DHCP it prompts
chronium to turn only the guest network off, observes port/address loss, then
prompts him to turn it on. Everything else is hands-free: require the same owning
lease, a fresh association/key-install trace, authenticated readiness and DHCP;
never silently select another BSSID/channel or issue a replacement CONNECT.
Guest LAN isolation stays unchanged; this test needs no LAN payload fixture.
No main-AP, group-rekey or firewall changes. Native guest loss/recovery passed
on 2026-10-22, as recorded below. Passive security metadata remains
unauthenticated and actual CONNECT independently revalidates it.

### Native negative/lifetime batch (2026-10-21)

ThinkPad `7a58a243`, userland `57293d0`, ports `96a0cdf0`, builder `3d69e83`;
trace ring, console info, UDP logging off. Reboot/status and all six batch checks
passed; the orchestrator verified PXE restoration. Record:
`qualification-7a58a243/results/batch-20261021-124747` under the stage-4 packet.

| Check | Native result |
| --- | --- |
| Generated wrong passphrase | Explicit connection failure (`CALL_UNAVAILABLE`); no advertised WLAN port/address after failure |
| Owning aliases | Link survives first close; final owning close retires it |
| Held-link wrapper CUT | Both aliases denied; worker becomes reusable |
| Remote close | Owning lease retires; fresh Remote/CONNECT succeeds |
| Handshake interruption | Foreground capture terminated; retirement trace is HANDSHAKE (4), `CALL_ABANDONED` (28) |
| Fresh protected payload | Both 256 KiB hashes pass, wlan0 source proved, Ethernet policy unchanged |

`completed.json`, `wrong-key.json`, `interruption.json` and
`final-payload/route-evidence.json` contain the bounded evidence. Final payload
counters advance by 433 completed TX and 583 queued RX frames. No pending-port
sample was taken during the wrong-key attempt; no timing/throughput or over-air
claim follows. Passive scan admission and fresh connections establish observed
retirement/reuse, not allocation/erasure completeness. This does not qualify
initiating radio-root CUT, direct native CANCEL, held-radio worker recovery,
authenticated replay/key reinstallation or group rekey. Group rekey remains
unqualified with interval zero.


### Native guest AP-loss recovery (2026-10-22)

ThinkPad `a206621f`, userland `afa8836`, ports `96a0cdf0`, builder `3d69e83`;
trace ring, console info, UDP logging off. The passive guest RSN precheck passed
before credential entry. Chronium switched only the guest network off at the
ready marker and on after association, controlled-port and address loss were
observed. The original nonterminal owning lease recovered automatically, with
a new association generation, confirmed PTK/GTK install traces and DHCP.
Ethernet policy stayed unchanged; explicit disconnect/release and temporary
profile cleanup passed. The saved PXE menu was restored byte-for-byte.

Result: `guest-ap-loss/results/guest-loss-20261022-084001` under the stage-4
packet directory (`evidence.json`, `progress.jsonl`, `DONE`). No new CONNECT or
silent retarget was issued. This qualifies ordinary AP-loss recovery; it does
not establish nonce contents, group rekey, held-link worker-death recovery or
independent over-air confidentiality. The new GTK install belongs to a fresh
association, not a group rekey. Final matched wired costs and the remaining
security qualification above stay open.

### Daybreak host replay/key-install qualification (2026-10-22)

Inspected parent `4a768238` (runtime `a206621f`), userland `afa88368`, ports
`96a0cdf0`. Public synthetic host fixtures and results are in
`daybreak-replay-public` under the stage-4 packet directory. The RSN fixture
linked the pinned production `libwpa-rsn.a`, with synthetic observation callbacks,
and drove `wpa_sm_rx_eapol` with M1, MIC-valid AES-wrapped M3 and group-key
messages. Initial PTK/GTK installation occurred once; same-counter M3/G1 was
rejected, while authenticated fresh-counter M3/G1 was acknowledged without
reinstallation. Bad MIC did not consume the counter; a valid message at that
counter then succeeded. A changed GTK installed once; unprotected M1 alone did
not invoke key callbacks. These observations exercise the upstream state machine,
not the native callback/firmware path.

A second host fixture compiled an unchanged copy of `ax200_security.c` with a
transport stub. Identical key+ID made no firmware call or host PN reset;
conflicting ID was refused. Changed GTK issued ADD before old REMOVE; command
uncertainty preserved tracked state, with rollback of the new GTK after old-key
removal failure. MIC failure, old PN and unknown key ID did not advance accepted
PN. Native `rsn.c` callbacks, station port/queue handling and `wifi_auth.c`
generation gates were source-inspected. No replay/key-reinstallation failure or
dependency-code finding was confirmed.

Daybreak found one RX-accounting omission: unknown-key/malformed protected frames
returned without incrementing `rx_dropped`. The station now counts every non-OK
security-check result and keeps the existing nonfatal discard behavior. No
plaintext or authorization bypass was found. Firmware PN retention, over-air
replay resistance and native GTK rekey remain **unqualified**; the host fixture
and QEMU without AX200 cannot establish them. Independent over-air no-plaintext
evidence and the other native limits above remain open.

### Matched FILE-cost attribution (2026-10-22)

The native interleaved wired packet reproduced a FILE payload regression:
209.75 to 475.02 microseconds per 1 MiB, while OPEN remained about 5.8
microseconds. It is attributable to the WPA2 SDK's full reply-scratch erase in
`file_read`, whose new `memset_explicit` uses volatile byte stores. Same-kernel
QEMU interventions separate this from intervening main/clock work; see the
[record](../development/experiments/ax200-wpa2-file-read.md). No production
performance fix has been applied; erasure must be preserved. This regression
remains open. The comparison is not a native idle/BSP cost measurement.
