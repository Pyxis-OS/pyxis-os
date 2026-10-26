# AX200 firmware and passive scan qualification

Native stage-2 evidence, 2026-10-19. The ThinkPad successfully received six BSS
records at `f68aacba`; repeated scans, cancellation and matched after-change wired
controls and final restacked smoke pass. Hardware rfkill transition is not
testable on this machine with F8/Fn+F8 (owner disposition, 2026-10-19). Scope:
[AX200 controller](../../devices/ax200-wifi.md); implemented contract:
[Wi-Fi operations](../../interfaces/wifi.md).

## Before-code wired baseline (2026-10-19)

Owner/orchestrator warm-rebooted the ThinkPad into current main
`3bb300aea365d5986a068f4753efff0832bd7b6c`. Userland `a9f6c64a`, ports
`7604c85d`, fs `25337999`, lwIP `a1aadb91`; existing builder `3d69e83`, -j8,
default Kconfig, `REMOTE_BEACON=t14 LOG_LEVEL=info LOG_UDP=1`, debug disabled,
no game assets. RTL8111 was prepared/up and bound as net0; TSC selected.
Peer horse `192.168.0.213:18080`, same cable, idle input/quiet host reported.
The owner subsequently confirmed the ThinkPad was on AC for this baseline;
that confirmation, rather than the runner's conditions string, verifies AC.

Existing ttcp, three 64 MiB samples each:

| Workload | Samples (MiB/s) | Median | Range |
| --- | --- | ---: | --- |
| TX 8 KiB | 111.490, 111.595, 111.627 | 111.595 | 111.490–111.627 |
| TX 2 KiB | 101.176, 101.893, 102.969 | 101.893 | 101.176–102.969 |
| Discard RX | 113.111, 113.021, 113.126 | 113.111 | 113.021–113.126 |

```text
boot://ttcp.pxe -t -p 18080 -n 8192 -l 8192 192.168.0.213
boot://ttcp.pxe -t -p 18080 -n 32768 -l 2048 192.168.0.213
boot://ttcp.pxe -r -p 18080 192.168.0.213
```

All nine command exits/drains and guest byte counts passed; TX host counts
matched. RX checks count, not content integrity. Native idle/BSP CPU cost is
unmeasured. Repeat the same workload/configuration after driver work and
record verified power state for the comparison.

Raw results, exact image/tool hashes, pins/configuration/flags and the operator
clarification: `/shared/present/wifi-ax200-stage2/baseline/results/tcp-20261019-080205`.
The original PXE menu was restored and verified after collection; root payloads
and EFI were unchanged.

## Native radio results

The successful isolated candidate is `f68aacba34ee9dc28c60e574d227ee70d0421b93`
(`wifi/rx-mpdu-v1-size`), with userland `18fceef8`, ports `7604c85d`, fs
`25337999` and lwIP `a1aadb91`. Same builder, INFO/default Kconfig and wired
management profile as above; `WIFI_AX200=1` adds `wifi.ax200=1`. Explicit
diagnostic Development/Remote grants permit READ/SCAN; stock configs omit them.
The orchestrator runs existing tools over Remote; beta does not initiate reboots.

| Observation | Native result |
| --- | --- |
| Firmware | Unmodified pinned CC77, 1,372,160 bytes of firmware DMA |
| DMA owned | 3,649,536 bytes total; firmware sections share one allocation |
| Rings | 512 RX descriptors/buffers, 256 command entries, one command queue |
| Before scan | READY, rfkill clear, no failure; 16 interrupts / 16 packets |
| Passive scan | COMPLETE; six records; eight RX MPDUs, zero length rejects |
| After scan | READY, no failure; terminal completion drained; 27 interrupts / 29 packets |
| Queue high-water | RX 1 / command 1 in this run; observed use, not a capacity guarantee |
| Admission | Passive channel mask 1–11 after NVM/firmware intersection; no installed country |

The owner privately confirmed a known nearby network was present. No nearby
SSID/BSSID or firmware country value is recorded here. Results are immutable
native FILEs, not a borrowed receive-ring view. This run proves one completed
scan and wired Remote continuity, not cancellation, live rfkill transition,
association, repeated cleanup, leak freedom or native idle/BSP CPU cost.

Private result directory under `/shared/present/wifi-ax200-stage2/native/results/`:
`radio-20261019-132623`. Commands: `wifi status`, `wifi scan`, `wifi status`,
then `boot://log.pxe -l info | grep 'wifi:'`. The host runner verifies image/tool
hashes and records command outcomes and complete Remote drains.

## Matched after-change wired and lifecycle checks

On `f68aacba`, same RTL8111 cable/profile and 64 MiB ttcp commands, three samples
each. Conditions reported as AC/quiet horse; this is the frozen pre-registry
radio image, not the later current-main restack. Both runs selected TSC.

| Workload | Before median [range], MiB/s | After median [range], MiB/s | Median change |
| --- | --- | --- | ---: |
| TX 8 KiB | 111.595 [111.490–111.627] | 112.404 [110.906–112.418] | +0.7% |
| TX 2 KiB | 101.893 [101.176–102.969] | 101.652 [101.481–102.446] | −0.2% |
| Discard RX | 113.111 [113.021–113.126] | 112.143 [105.531–112.243] | −0.9% |

All nine transfer byte counts, command outcomes and Remote drains pass. No
material wired throughput change is established by this small sample; RX has
one low observation (105.531 MiB/s). This does not measure Wi-Fi throughput or
native idle/BSP cost. Private `native/results/tcp-20261019-134527` retains all
samples/configuration; the before-code directory is listed above.

`repeat-20261019-134533` passes three complete scans in fresh Remote sessions,
with READY/no failure after each. `cancel-20261019-134537` passes foreground
Ctrl+C, a newly observed matching firmware ABORTED terminal, complete Remote
drain, and one successful recovery scan with READY/no failure. The wrapper
checks these automatically; it does not infer cancellation merely from a host
process being killed. These are native observed cleanup/reuse checks, not an
allocation-leak proof. Nearby identifiers stay in private results only.

## Attributed bring-up corrections

| Evidence | Correction and limit |
| --- | --- |
| Initial preparation exhausted kernel VM range records; task stacks then failed despite free physical RAM | Consolidate 48 firmware-section mappings into one owned arena; all unpublished preparation allocations unwind, optional Wi-Fi yields to core boot. Subsequent native boot used 214/256 records, before the independent main-line range-limit increase. |
| Bare metal had zero IRQ/RX after firmware kick; identical firmware under VFIO reached ALIVE | Native root port `00:02.3` had COMMAND=3, blocking upstream DMA/MSI despite working CPU MMIO. PCI now owns verified ancestor BME activation before endpoint mastering. `a5298794` reached ALIVE with five interrupts/packets. |
| INIT completion arrived with a bounded nonempty payload | Treat it as an opaque completion as the pinned iwx reference does; transport bounds remain. `81ec7125` reached READY. |
| A scan delivered an MCC restriction-change hint | Bounded deferred profile refresh intersects permissions; it never installs a country. Valid restriction changes abort/drain affected scans; uncertain refresh fails closed. |
| Actual iteration notification was 912 bytes; terminal was 16 | Consume the shared checked 16-byte advisory iteration prefix, ignoring the bounded unused tail. Matching exact terminal completion and drain still gate reuse. `a4a32c58` completed an empty scan. |
| RX MPDU common prefix 20 bytes plus v1 tail 28 bytes | Correct the mistaken 52-byte extent to 48, with packed-size/offset assertions. `f68aacba` produced six records; existing CRC/security/channel/IE checks and passive scan settings remain. |

Linux v6.12 [RX ABI](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/fw/api/rx.h),
[scan ABI](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/fw/api/scan.h)
and [scan handler](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/scan.c)
were compared with the pinned iwx reference. Linux references use their BSD
alternative; notices are retained in `kernel/wifi/NOTICE`. No reference sources
were vendored. Temporary register/SRAM/IOMMU dumps and raw country logging are
removed from the review implementation. Non-identifying scan counters are trace-only.

### PCI inventory audit

ThinkPad boot inventory: `00:02.3` (AX200 bus 3) and `00:02.4` (bus 4) had
COMMAND=3, BME off. Bus 4 contains the Realtek RTS522A `10ec:522a` card reader
at `04:00.0`; its operation was not tested. `00:02.1`, `.2`, `.6`, `.7` and
`00:08.1` had COMMAND=7. The fix lives in PCI and preserves all other COMMAND
bits, windows, decoding, power and interrupt controls. Shared upstream gates
remain PCI-owned until reboot; no AX200-specific forwarding helper remains.

QEMU q35 root port `00:04.0` had COMMAND=7 in a read-only debugger observation.
The saved vela inventory identifies ports `00:1c.0`, `.3`, `.4`, but has no
boot-time COMMAND values: they remain unknown. Linux runtime BusMaster+ is not
evidence of firmware boot state. Read-only native evidence showed the AMD
IOMMU disabled and bootstrap DMA in low, consistently WB-mapped RAM; it did
not explain the initial failure. Upstream BME activation did.

### Reference differential limits

Fedora 6.19.10 used the exact same decompressed CC77 bytes (SHA-256 recorded in
the asset closure). Its mmiotrace contained probe-time context publication and
ALIVE even though a later interface-up/cleanup step failed. It exposed the LTR
encoding correction to `0x88fa88fa`, which alone did not fix native startup.
A power-off/start also reproduced the failure. VFIO supplied a working upstream
DMA/interrupt configuration and reached ALIVE; it was differential evidence,
not proof that every guest mapping or reset assumption was correct.

Raw captures remain under the private packet's `native/results/` and
`vfio-draft/results/`; failed candidates and full register dumps are not copied
into the repository. Public PR comments retain the diagnostic chronology.

## Review-head integration and remaining checks

The implementation is restacked onto current main, including the delivered
network-interface registry and package views. Wi-Fi protocols use unoccupied
ABI tags; startup delegation preserves those main-line additions. Userland
`bc8de46c` contains main `abd7aa81`. The earlier native image remains separately frozen for the matched controls;
it is not relabelled as the restacked review head.

The complete current-main kernel/SDK/userland/ports INFO image build passed,
followed by clean image assembly at `0215ec06`. Four-CPU q35/KVM, 2 GiB,
VirtIO Ethernet/RNG control passed status, an absent-radio scan refusal (exit 1),
an ordinary command and a fresh Remote reconnect. Read-only GDB confirmed
AX200 prepared=false, DMA=0 and no pending scan. Raw control:
`qualification/final-restack-control/verified` under the private stage-2 packet.
No owned QEMU/GDB jobs remain. Ordinary QEMU cannot emulate AX200 DMA or RF.
All four parent CI checks pass at `8bc2658b`, including the build/pins against
userland `bc8de46c`; the dependency has no independent CI tasks. The later
qualification-only head `91824349` also passed all four checks.

The separately hash-verified `native/final-integration` packet contains
`0215ec06`, userland `bc8de46c`, ports `e9906abb`, fs `f3f22b6e`, lwIP `a1aadb91`
and the same compiler/INFO/default Kconfig. After the frozen-image controls
passed and `8bc2658b` CI was green, it was staged to the default PXE entry with
hash verification and its own byte-for-byte backup of the prior f68 selection.
The orchestrator owns the reboot/run; beta has not rebooted the target.
Its current network registry names Ethernet `eth0`; the frozen comparison's
`net0` is not silently reinterpreted. The final smoke is integration evidence,
not a matched throughput comparison against the earlier network base.

Hands-free packet commands (on horse, in
`~/shared/present/wifi-ax200-stage2/native`):

```sh
python3 run.py tcp --conditions 'AC; after successful passive scan; same RTL8111 cable; quiet horse'
python3 qualify.py repeat --conditions 'AC; same RTL8111 cable; quiet horse'
python3 qualify.py cancel --conditions 'AC; same RTL8111 cable; quiet horse'
```

The repeat wrapper requests three scans with fresh Remote sessions and verifies
terminal success/READY and session cleanup. The cancellation wrapper sends
foreground Ctrl+C during a scan, checks a new firmware ABORTED terminal, then
requires one successful recovery scan and READY. It refuses to call a late
interrupt a cancellation success. These use shipped tools, not new guest tests.
The frozen diagnostic image retains the non-identifying completion lines needed
by that check; the final review image uses trace-level diagnostics instead.

### Final restacked native smoke and rfkill limit

`native/final-integration/results/radio-20261019-135103` passes on image
`0215ec06` / userland `bc8de46c`: five records; known owner network confirmed
privately; READY/no failure before and after, terminal completion drained.
Observed interrupts/packets went from 18/18 to 27/28; runtime DMA/ring values
match the earlier candidate. This validates current-main registry/startup/tool
integration; it is not a new matched network-throughput measurement.

On this T14 Gen 1 AMD, owner presses of F8 and Fn+F8, then a separate plain F8,
left `rfkill=0`, READY=1. Host checks returned TRANSITION=UNQUALIFIED and did not
attempt a refusal test without an asserted line. Private results:
`rfkill-refuse-20261019-135407` and `rfkill-refuse-20261019-135450` under the
final packet. The key does not assert the AX200 hardware-rfkill line in these
Pyxis runs; a software/EC hotkey is a possible explanation, not an identified
implementation. No physical radio switch is known. Owner disposition: hardware
rfkill is not testable here; keep the [qualification debt](https://git.internal/PyxisOS/pyxis-os/issues/1282).
Live assertion, scan refusal and release/recovery remain source-inspected,
not natively qualified. BIOS device absence or Linux software rfkill is not a
substitute. Overflow, malformed firmware events and allocation failure are also
source-inspected, not qualified by injected faults. Native idle/worker utilization
remains [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1288).

The final image remains staged at the owner's request. Its packet retains the
byte-for-byte backup of the prior f68 selection; root payloads, EFI and other
menu entries remain unchanged. No QEMU/GDB/server jobs remain running.
