# AX200 firmware and passive scan

Stage 2 assigned to Codex beta, owner go 2026-10-19. **Scope proposal only;
no driver code or native/PXE action yet.** The [Wi-Fi plan](wifi.md) and
[stage-1 resource/asset record](ax200-resource-closure.md) remain authoritative.
Placement, source/firmware pins, BSP ownership and uncertain-stop retention
are settled; this note does not reopen them.

## Delivery scope

AX200 only: boot preparation, bounded firmware/container parsing, self-load,
ALIVE/INIT_COMPLETE, live NVM/rfkill, one passive 2.4 GHz scan and checked
completion/cancellation. Confirm actual DMA extents, ring/queue counts and
high-water use against the derived stage-1 profile. Prove command-only scan
initialization rather than assuming the full reference's data queues are absent.
Package the pinned unmodified bytes as `iwlwifi-cc-a0-77.ucode`, with the complete
Intel notice/provenance; this filename is an image choice, not a firmware ABI.

Use native PCI/MSI-X/DMA and a dedicated BSP worker, preserving Ethernet
progress. IRQ entry only acknowledges/publishes bounded work. No LinuxKPI,
association, credentials, active probes, data TX, Wi-Fi IP interface, automatic
scan, roaming or suspend. A small `wifi` bundle provides status and explicit
scan; ordinary `net` observation must not initialize or scan the radio.

## Owner decisions proposed (defaults)

1. **Activation and regulatory policy:** an early boot opt-in enables bounded
   resource reservation before AP startup. Trusted boot init alone holds the
   non-inherited authority to install this controller's country/channel policy
   once, then start firmware preparation. No default country, timezone/AP
   inference or live country change. The native packet must state the owner's
   actual country explicitly. First scans use only channels 1–11, further
   restricted by a pinned country profile and checked NVM/firmware permissions;
   unsupported/unknown policy, missing channel evidence or rfkill refuses scan.
   Configured policy can narrow, never widen, the hardware restrictions. The
   reference's `iwx_init_hw` uses `MCC_UPDATE("ZZ")`; do not copy that as explicit
   Pyxis policy. Firmware country-response/version handling and a sourced
   country profile must be established before the first scan command.

2. **Authority and observation:** a native controller-scoped radio capability
   has independently attenuable READ and SCAN rights. READ gives preparation,
   rfkill and failure/resource status, never nearby network identities or an
   implicit operation. SCAN permits an explicit operation and access to its own
   results; it grants no association, packet or country-policy authority.
   Trusted boot/session configuration selects recipients; stock sessions get
   no SCAN automatically. The native diagnostic Development/Remote session
   explicitly delegates it to the declaring `wifi` bundle, through ordinary
   launch grants. No call-site authority wrapper. SSID/BSSID output is escaped,
   opt-in and retained only by the caller; none is written to boot/trace logs.
   Completed result snapshots may be deliberately copied/delegated as FILEs.

3. **Operation ownership and failure:** one scan per controller, BUSY for another;
   a process-owned, nondelegatable waitable operation handle owns the request.
   Closing it or process exit requests cancellation. Completion publishes an
   immutable native FILE result; no borrowed DMA or partially filled result is
   exposed. Initial tuning: 128 BSS records and a 15 s overall scan deadline,
   separately reported from firmware preparation. Overflow returns TOO_LARGE
   with no published prefix. Deadline/cancel drains a firmware abort/completion
   before reuse; uncertain completion or fencing disables this controller until
   reboot with backing retained. No automatic reset/retry or rfkill override.
   READ status remains available after failure. These capacities/time budgets
   are starting tuning, not architectural limits.

No public operation layout or placeholder ABI is installed by this proposal.
The two process/result lifetimes above are deliberate authority choices; ABI
field placement and helpers follow after acceptance.

## Qualification and native coordination

Use existing tools. Before driver code, retain a current-main wired baseline
with the stage-1 ttcp commands and matched configuration; repeat after firmware
readiness and scans. Stage 1 measured 111.154/103.038/113.072 MiB/s median
(TX 8 KiB/TX 2 KiB/RX); those historical samples are a reference, not an A/B
for newer networking changes. The [idle/worker baseline debt](../technical-debt.md#wi-fi-idleworker-cost-baseline)
remains: throughput does not establish native CPU cost.

Ordinary builds and QEMU can qualify absent-device behavior, unchanged Ethernet
and bounded host-side parsing where applicable, not AX200 DMA/RF. Native gates:
owner power-on plus warm reboot, actual NVM/rfkill/regulatory admission, runtime
memory/queue confirmation, passive scan of a known owner AP, cancellation and
wired Remote continuity. Keep nearby identities in local results only. No
native/PXE staging, reboot or device action before telling the orchestrator;
packets follow `/shared/present/RUNSHEETS.md`. Ask the owner before any new
behavior, authority or lifetime choice. No compiler-container rebuild is needed.
