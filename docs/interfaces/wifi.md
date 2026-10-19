# Passive Wi-Fi controller and scan operations

The native AX200 implementation is opt-in (`wifi.ax200=1`, image option
`WIFI_AX200=1`). It prepares firmware/DMA once at boot on a dedicated BSP
worker. It implements passive channels 1–11 only; no active probes, association,
packet access, Wi-Fi IP interface, roaming or reset recovery. Hardware and RF
qualification remain in the [stage-2 record](../development/experiments/ax200-passive-scan.md).

## Authority and startup

`PROTOCOL_WIFI` has independently attenuable READ and SCAN rights. READ returns
presence/preparation/rfkill/failure, actual DMA and ring/counter status; it never
starts a scan or reveals nearby identities. SCAN starts one explicit operation.
The private POLICY_ONCE right belongs only to stock boot init: it cannot be
copied, forwarded or inherited. An optional explicit uppercase country (not ZZ)
and channel mask narrows passive permissions once. Later active/association work
must require that installed policy; it is not implemented here.

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
`wifi = { read = true, scan = true }` (each boolean independently optional).
Stock configurations omit both. Trusted session, Remote and mux startup carry
only the selected rights. The `wifi` bundle declares READ and SCAN separately;
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
