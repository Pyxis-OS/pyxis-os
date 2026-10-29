# RTL8922AE channel attribution and startup observations

Task 1 under the [accepted discovery plan](../../wip/rtl8922ae-wifi.md#next-discovery-slice--accepted-chronium-2026-10-29).
The known nearby channel-6 network was missing from the earlier aggregate
result; that omission remains unexplained. No receiver-field or dwell change
is justified by the source audit below.

## Baseline before implementation

Exact main kernel `c65430c61e6b1ed91d5951c6edb9a85d951f4b8d`, published builder
`pyxis-llvm23.1.3-8bdf136`, four-CPU q35/KVM/host CPU, 4 GiB, OVMF,
virtio/slirp, no radio or disk. The deliberately frozen prior control archive
uses userland `8cefca4`; it is a fixed workload fixture, not a claim of a full
current-main userspace image. Kernel identity and archive marker were checked.
Ordinary wired/log/status, invalid/absent scan paths and GDB inspection passed;
the owned guest, clients and monitor were retired. Three existing iobench
invocations each verified three 1 MiB archive reads with a 4096-byte buffer,
before any task-1 code changes. Raw evidence stays local in
`rtl8922ae-attribution-20261029/baseline/`.

The existing native reference remains the previous packet's verified
`aebf0ca52` kernel: 1+6 firmware visits, snapshots of 4/5 records all attributed
to channel 1, power readbacks 11/12 and intact wired/PXE management. Its helper
discarded BSSIDs and raw metadata. That evidence cannot show whether the known
channel-6 BSSID was received, rejected or labelled incorrectly.

## Pinned Linux comparison

Reference: v6.18 `7d0a66e4bb9081d75c82ec4957c50034cb0ea449`,
`drivers/net/wireless/realtek/rtw89/`:

- BE/v2 IE01 still carries the channel index at `w0[23:16]`
  (`txrx.h:602`, `core.c:1838–1862`). The BE IE lengths, MAC/user/count/PLCP
  prefix, PHY prefix and 2.4 GHz low-nibble decoding match the native code.
- Linux writes the encoded startup channel to BB `R_MAC_PIN_SEL`,
  `B_CH_IDX_SEG0` (`rtw8922a.c:1270–1271`). ENTER handling updates software
  state, not that register (`mac.c:5069–5082`, `chan.c:174–189`). Matching
  nonzero PPDU metadata can then replace the initial scan frequency
  (`core.c:3099–3100`, `rtw8922a.c:2674–2706`). Host source does not establish
  whether firmware updates the report tag while hopping; a stale tag is a
  hypothesis, not a reason to add a write on ENTER.
- Ordinary 2.4 GHz, zero-SSID Linux scans use period 45, zero dwell, no probes
  or null TX, and packet IDs `0xff` (`fw.c:7451–7495,7774–7820`). The current
  1+6 channel-list fields match. NO_IR/RADAR uses a separate listen-only path;
  it is not selected merely because the SSID list is empty.
- Both native CMAC management filters/forwarding and standalone PPDU enables
  are present. Ordinary Linux scan filtering names the scanning interface's
  MAC, as does the native MAC0 request. Linux additionally enables inline
  PHY/RSSI reports on both MACs under DBCC (`core.c:5524`, `mac.h:1289–1296`);
  native setup currently does this only on MAC0. Native candidates accept a
  zero inline-PHY length, so this difference is not a proven missing-BSS cause.
- A candidate whose DS/HT channel conflicts with its matched PPDU channel is
  omitted before publication. A final result alone cannot distinguish that
  rejection from missing reception. Keep C2H MAC index and RXD BB selector
  distinct when collecting evidence.

## Implemented observation boundary

Four new flags use the existing extended PHY state word: initial WLMAC sample,
active value seen, startup power cycle attempted and startup power cycle
completed. Completion is recorded only after the reference power-off and fresh
power-on both succeeded. Failure preserves the observed progress; firmware
readiness or later terminal shutdown does not create a completion fact.
There is no extra MMIO read, retry, allocation or log producer. The 544-byte
released prefix and 600-byte full reply stay unchanged. The matching SDK reader
validates flag implications, and `wifi status` distinguishes unobserved,
non-active, incomplete and completed history without claiming RF silence.

The task retains the current scan profile. Native proof of the startup branch
and the reason for the missing channel-6 network are pending; task 1 is not
complete. Network identifiers belong only in private qualification input and
evidence, not this record or public logs.

## Software checkpoint

Production candidate `6f1ae9cd94367a156037ec0ff042b6021f467921` pins userland
`2dfa643f695f90ff32f953c931cec542c409c31b`. Published-builder kernel and selected
C runtime/SDK, fresh Lua development prefix, CLI, selected bootstrap/control
programs and private helper build successfully. No compiler rebuild was needed.
The local SDK prefix is for these C programs; the normal parent CI checks the
complete SDK/image integration.

The exact candidate passes the same no-radio QEMU configuration, identity and
marker checks, wired/log/status reads, invalid/absent requests and GDB inspection.
The fixed iobench binary is reused for the control observations. All owned guest,
debugger, Remote and monitor processes were retired. This cannot exercise a
Realtek startup or settle channel attribution.

The private helper now copies per-BSSID observations from its already-authorized
immutable SCAN FILEs before erasing them. Local host tooling retains raw output
outside the shared handoff tree, with private directory/file permissions and a
separate expected-BSSID input; public records contain counts and comparison
results only. This can identify accepted records, not candidates discarded
before FILE publication.

## Approved private pre-filter capture (chronium, 2026-10-29)

The accepted production status contract adds no kernel log lines. A separate,
unmerged packet-only diagnostic image may use bounded trace-level output to capture the
missing evidence: beacon/probe BSSID and advertised DS/HT channel before
rejection; raw IE01 channel and RXD BB/counter/rate/GI/BW; match/rejection outcome;
and C2H ENTER channel/MAC index. Keep MAC index distinct from BB selector. Do not
collect SSIDs or payload data, borrow DMA pointers, write channel tags, or add a
new public diagnostic operation or grant.

At most 512 copied per-frame observations in fixed backing across the approved scan pair,
retaining fitting records and reporting capture drops. Use only the private
diagnostic branch; none of its trace code enters the production PR. Raw BSSIDs
and traces stay in horse's private evidence, with redacted aggregate results in
the handoff. No synthetic frames, forced hardware state or extra RF requests.
Chronium explicitly allowed this temporary diagnostic exception. Record BSSID,
PPDU channel, firmware-reported channel/band and scan step before filtering.
The diagnostic code must never merge into the kernel/userland production PRs
and is removed after the experiment. The approved packet has not yet run.
