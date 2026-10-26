# AX200 WPA2-Personal qualification

Stage 4 closes on 2026-10-22 with the [implemented controller/profile](../../devices/ax200-wifi.md#wpa2-personal),
[native interface](../../interfaces/wifi.md#wpa2-auth-and-credential-boundary) and
four [accepted qualification limits](https://git.internal/PyxisOS/pyxis-os/issues/1285).
These are observed native/QEMU/host results, not independent over-air proof.

## Configuration and baseline

ThinkPad T14 Gen 1 AMD, AX200/CC77 `77.f39cc7f9.0`, onboard RTL8111 wired
management, existing builder `3d69e83`. Native protected profiles use the trace
ring, INFO console, UDP logging off and t14 Remote beacon. The main AP offers
WPA2-PSK/CCMP within WPA2/WPA3 transition mode, optional PMF; the test station
negotiates WPA2 without PMF. AP identities and credential bytes remain private.
Native prompt entry over unencrypted wired Remote is a chronium-accepted test
limit, not secure transport. No secret-input interval is retained.

Before-code revision `d2cf7091`, AC/quiet horse, 2026-10-21, three 64 MiB
transfers per mode: TX8k 103.5 [102.1,104.2], TX2k 92.1 [91.5,92.5],
RX 99.7 [99.2,100.1] MiB/s. Record under `/shared/present/wifi-ax200-stage4/`:
`baseline/results/baseline-20261021-092843`; PXE restoration verified.

## Native functional checks

Commands and result records below are in that same shared packet root. The
orchestrator ran the reviewed packet runners over wired Remote; ordinary
`wifi connect --profile FILE` prompts supplied credentials at runtime.

| Runtime revision / packet result | Observed result |
| --- | --- |
| `198593ee`, `next-review-198593ee/results/connect-20261021-101305` | Correct-key association and independent wlan0 DHCP; authenticated/data-ready, not OPEN |
| `d32c985a`, `next-recovery-d32c985a/results/payload-20261021-114321` | 256 KiB upload/echo and download hashes pass; wlan0 source proved; Ethernet policy unchanged |
| `7a58a243`, `qualification-7a58a243/results/batch-20261021-124747` | Generated wrong key refused; owning aliases/final close, wrapper CUT, Remote close, foreground handshake interruption and fresh reuse pass; repeated scoped payload passes |
| `a206621f`, `guest-ap-loss/results/guest-loss-20261022-084001` | Chronium switched only the guest AP off/on at runner markers; original owning lease automatically recovers with a fresh association, confirmed PTK/GTK install traces, DHCP and unchanged Ethernet; cleanup passes |

Protected candidates use ports `96a0cdf0`, userland `a8d61de7` for the first two,
`57293d0` for the batch, and `afa8836` for guest loss. PXE restoration was verified.
A fresh-association GTK install is not a group rekey or proof of nonce contents.
The batch qualifies wrapper CUT, not initiating-radio-root CUT or direct native
CANCEL. Allocation/erasure completeness was not fault-injected.

## QEMU, host fixtures and source inspection

Four-CPU q35/KVM/4 GiB QEMU with public inputs checks production PBKDF2, AES
unwrap/HMAC vectors, sealed credential READ/refused WRITE/final close, FILE
bytes/EOF and explicit absent-radio refusal. Read-only GDB observes claimed
brokers; worker replacement and session-opened broker lineage are exercised
separately. QEMU has no AX200 and establishes no RF result.

Daybreak's production `libwpa-rsn.a` host fixture drives public M1/MIC-valid
AES-wrapped M3/group messages: one initial PTK/GTK install, replay-counter/MIC
checks and acknowledgements without same-key reinstallation pass. An unchanged
`ax200_security.c` fixture with a transport stub checks same-key no-op without
host PN reset, changed-GTK ordering/rollback and rejection without advancing PN.
Native callbacks, generation gates and controlled-port handling were inspected.
No confirmed replay/key-reinstallation failure was found. The RX-accounting Low
is fixed: rejected protected frames, including unknown key IDs, count as drops
without fatal teardown. Public records: `daybreak-replay-public/`.

## Cost and accepted limits

The [FILE-cost record](ax200-wpa2-file-read.md) retains the regression attribution,
same-kernel QEMU correction and final six-cohort native comparison. Native
payload/whole B/A is 0.983/0.984; TCP ratios are 0.990/1.005/0.980, with horse
busy and ranges retained. Lua OPEN is recorded as noisy/unattributed. The runtime
comparison is A `d2cf7091` / B `181f74ab`; later delivery restacks do not relabel it.

Chronium accepts native GTK rekey (AP interval zero), held-link worker-death
recovery, independent over-air confidentiality/replay and firmware PN behavior
as unqualified limits (2026-10-22). Consequences and revisit points live in
technical debt. Host key/PN checks, payload hashes and reassociation traces do
not substitute for those missing native/over-air observations. Existing rfkill,
RF-channel and CPU-accounting limits remain separately recorded.
