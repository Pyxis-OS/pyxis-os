# Bluetooth mouse task 3: firmware readiness

Status: **bounded BOOT bulk-IN retirement implemented; native default-level rerun pending.**
The pre-code plan was posted in [#564](https://git.internal/PyxisOS/pyxis-os/pulls/564)
at signed fbbb72b before implementation.
Branch bluetooth/firmware-readiness starts from fresh main 52451d3, after
[#552](https://git.internal/PyxisOS/pyxis-os/pulls/552) merged.
This is task 3 of [the mouse milestone](../../../wip/bluetooth-mouse.md).
Connections, pairing, bonds and input remain later assignments.

## Exact asset and packaging plan

Pin linux-firmware release **20260309**, commit
c822cbbb14ce5b8ee1f27346220640ac350bbf34. These bytes match the installed
Fedora assets in the [investigation](../bluetooth-task4/README.md#deferred-cold-upload-and-asset-provenance).
The requested SFI and DDC are regular upstream files, identical to Fedora's
deduplicated targets ibt-20-0-3.sfi and ibt-0040-0041.ddc.
WHENCE labels them BT_CyclonePeak_A0_REL53636; that is not running-build evidence.

All four upstream URLs use this root:
https://gitlab.com/kernel-firmware/linux-firmware/-/raw/c822cbbb14ce5b8ee1f27346220640ac350bbf34/

| Upstream path | Bytes | SHA-256 |
| --- | ---: | --- |
| intel/ibt-20-1-3.sfi | 801016 | fd51628c307a1b03fbd7156f0d2a7f5af67b7202d4f358f5d4d807911982c3cc |
| intel/ibt-20-1-3.ddc | 9 | fe272982577efdc289cfe3e8bedafca607c0b7d6c9df1aab1880e22ef930d077 |
| LICENCE.ibt_firmware | 2040 | 5181b0b51efc79d5acb2c9bb92042878fdbad97a92114d4ab5e32e2b5b52fce4 |
| WHENCE | 421898 | b763b1df1341d86664e8e881f5214333d5f904c31e8cc58bb5302774f0369630 |

The [complete Intel license](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/LICENCE.ibt_firmware)
permits unmodified binary redistribution subject to its notice, disclaimer and
other conditions. Ship its complete text, pinned WHENCE and provenance.
Firmware is separate from kernel source licensing and stays outside Git.

Pyxis owns this dependency and image assembly. The focused parent recipe downloads
only owner mirror URLs, checks all four sizes/hashes, and stages unchanged
firmware under share/firmware/intel/ and license/provenance under
share/licenses/intel-bluetooth/ in the boot archive. Runtime needs no network or
disk mount. Missing/mismatched assets fail image assembly; no upstream fallback
or implicit host firmware input. Kernel compilation remains independent.
No ports/userland pin or compiler-container change is planned.

The owner supplied the raw-gitlab mirror on 2026-10-09, recorded in
[metadata.json](../../../../firmware/ax200/metadata.json). The cache prefix is:
https://repo.internal/repository/raw-gitlab/kernel-firmware/linux-firmware/-/raw/c822cbbb14ce5b8ee1f27346220640ac350bbf34/
Its binary availability remains a build dependency, not a new firmware-policy
decision. The fetcher must succeed and verify all four files before consumption.

## Controller plan under accepted decisions

Recognize AX200 legacy platform/variant and matching cold version/boot parameters
before loading. The current Fedora boot reports bootloader 0.3, device revision 1,
secure boot/OTP/API enabled, debug lock disabled and minimum build 1/week 10/2014;
Linux selects ibt-20-1-3.sfi. These are Linux observations, not Pyxis measurements.
Read actual controller fields; do not invent fixed lock-value rejection.
Unknown cold identifiers or running builds return to the owner.

Use a bounded kernel state machine: version/boot parameters, validated RSA SFI
and embedded boot/build metadata, secure-send over owned asynchronous bulk OUT,
real bootloader events on independently framed bulk/interrupt IN, real secure
result, soft boot with USB completion plus real vendor boot notification,
successful pinned DDC records and operational version/capability confirmation.
No fabricated Command Complete for soft boot. Every DDC completion must succeed,
following assigned DDC/fail-closed scope. Finite command, upload, secure-result
and boot deadlines are settings; never wait in the xHCI worker.

The Linux successful soft-boot path does not reattach USB; Pyxis must qualify
that on the target. No hard reset, automatic retry or re-enumeration recovery.
Unexpected removal keeps retained-DMA/quarantine limits and returns for review.
Keep endpoint-local framing and transition to runtime ACL only at confirmed boot.

Known operational firmware skips SFI upload; apply only this profile's DDC.
The existing build 193/week 33/2024 remains provisional development with acquire
opt-in. Cold-loaded firmware also remains development-only until the accepted
transport, encrypted bond/reconnect, HID/pointer and native evidence qualifies
production. No reset enforces a pin on unknown firmware. #548 policy is unchanged.
Protocol references: [Intel initialization](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/bluetooth/btintel.c),
[field definitions](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/bluetooth/btintel.h)
and [USB transport](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/bluetooth/btusb.c).

## Owner-reported cold evidence

Cold PXE power-on boots of main 114f2ac and 183f793 on 2026-10-09 reportedly showed
only “Bluetooth HCI: unavailable (status 6); reboot required”.

The owner-reported integration cold boot on 2026-10-09 now identifies the path:
“Bluetooth HCI: unavailable: USB inventory incomplete (phase inventory, call
status 6); reboot required”. The full log is on horse at
/shared/batch-2026-10-09/caelum.log. Direct reading requires a Tailscale SSH check;
this record uses the supplied owner observation.

The owner identifies the built-in Realtek DASH EHCI function 02:00.4
(10ec:816d, programming interface 0x20), present docked and undocked. Current
Fedora sysfs confirms its vendor/device and class 0x0c0320. Source inspection
confirms usb/core.c records every non-xHCI function as unsupported, marks global
inventory incomplete, and previously passed that global flag to HCI sealing.
The owner attributes the morning boots to this same gate. Their generic line
alone could not distinguish it; the new integration line confirms inventory
failure before the firmware state machine. No native firmware upload ran.

**Accepted option 1, 2026-10-09:** readiness requires the hosting xHCI's final
controller record to be COMPLETE and exactly one identified AX200. Unsupported
controllers elsewhere remain recorded; global USB state can remain INCOMPLETE.
No hosting-controller failure is relaxed. Candidate counting precedes transport
admission, so an unsupported second AX200 cannot disappear from selection.
Success remains one upload/warm-skip summary, failures one specific phase/reason,
with parsed detail in ktrace. No Bluetooth addresses, raw packets or keys are
recorded. Fresh native cold power-on and warm reboot are required after the fix.

## Owner's next native batch

After review, collect one default-level cold boot and a warm reboot. Build the
kernel/image with `make -j16 image LOG_LEVEL=info LOG_UDP=1`; for PXE, verify
`log.udp=1` is on the kernel command line. Use the existing UDP listener before
power-on and preserve the actual ELF/image identity. The diagnostic trace batch
below already identified completion code 4. An info-level kernel compiles ktrace
out; another diagnostic run, if needed, must explicitly use `LOG_LEVEL=trace`.
Booting Pyxis takes this session offline.

1. Record reviewed revision and image/ELF identity. Fully power off, then cold
   power on directly into Pyxis/PXE without Fedora first. Record the one upload,
   DDC and operational-readiness summary, or its specific failed phase/reason.
   Success should read “AX200 USB ready (cold upload, DDC, development firmware)”.
   Record the AX200 hosting controller's inventory completeness (07:00.4 on this
   ThinkPad). The separate EHCI 02:00.4 should remain unsupported; global USB
   INCOMPLETE is compatible with Bluetooth readiness when the hosting xHCI is
   COMPLETE.
2. Warm reboot Pyxis into that same image. Record warm skip or specific failure;
   distinguish reboot from cold power-on. The expected skip summary is
   “AX200 USB ready (warm skip, DDC, development firmware)”.
3. Default-level success needs the cold/warm readiness summaries and usable
   hosting-controller/storage observations. If another trace run is needed,
   retain the unusual/rejected interrupt/async bulk IN completion code, residual,
   requested length, slot/DCI and receive/ring indices; BOOT publication and USB
   collection; transport-failure flags; and the parsed vendor boot notification.
   Successful-short examples are limited to one per stream. Record whether real
   notification and USB retirement were both observed. Remove addresses before
   recording; no packet or memory dumps.
4. Return to Fedora. **Re-enable Fedora Bluetooth when Bluetooth work is done**
   with sudo systemctl enable --now bluetooth.service. Reload the Codex SSH key
   after reboot as usual.

QEMU passthrough can qualify warm skip and storage coexistence while Fedora has
initialized AX200; it does not establish Pyxis cold upload. Native results remain
owner-reported until supplied. This plan claims no task 3 completion.


## Implementation and recoverable status

The private firmware module and HCI integration implement the planned bounded
flow. Startup uses the canonical device-recipient HCI USB request type 0x20/index 0
and the zero-parameter Intel Read Version command, accepting only
the AX200 legacy 10-byte profile. Cold postboot version must match the selected
image metadata before standard HCI Reset; the full tuple must remain unchanged
after Reset. DDC must complete before capabilities/readiness. No production
qualification is implied by a successful loader.

A confirmed firmware restart (USB retirement plus real boot notification and
drained partial-event boundaries) begins the same one-command initial HCI startup
window used at first attachment. It is initialization allowance, not a generated
Command Complete or an interpretation of the unqualified boot num_cmds field.
The next real command response establishes advertised credits. Native operation
of that boundary remains a measurement gate.

Bulk completion sequence continuity is preserved in both framing transitions,
including zero-length completions before cold identification. Interrupt and
bulk event reassembly are independent; framing stays fixed for an entire copied
USB completion. Notifications are armed at actual USB publication, preventing a
queued but unpublished command from accepting an unrelated boot result. Secure
success can precede final USB/CC retirement only after the final SFI fragment
has actually been published. No wait, recovery or allocation is added to the
xHCI worker.

Read-only inspection of the installed, byte-identical SFI confirms RSA CSS
version 0x00010000, 3233 bounded HCI container records, 3232 four-byte-aligned
groups and one seven-byte boot-parameter command with build 193/week 33/year 24.
This inspects provenance/container metadata; host files are not build inputs
and no upload or controller observation is implied.

Ordinary kernel builds pass with pinned Clang23.1.3/fork49e2c1a. Independent
source audit corrected the entering-bootloader bulk sequence boundary and
confirmed publication/retirement/deadline paths by inspection. No tests,
fault injection, forced cold transition or new benchmark infrastructure were
added. The unmodified baseline image/ELF from successful main52451d3 CI run1349
was saved before code changes; its attached measurements await restored access.

The owner cache URL is recorded in firmware/ax200/metadata.json. An initial fetch
failed closed with SFI HTTP 404; after the owner's cache correction all four
downloads matched their sizes/hashes. The ordinary local image build and
[CI run 1380 at c631a90](https://git.internal/PyxisOS/pyxis-os/actions/runs/1380)
passed. Extraction from the built boot archive independently matched all four
packaged assets. No upstream or host-file fallback was used. The AX200 remains
/dev/bus/usb/004/003; the owner restored access, and Fedora Bluetooth remains
inactive/disabled during passthrough.

Baseline bundles, immutable image/ELF, a private npfs fixture and remote-init
script are prepared in /tmp/pyxis-bluetooth-firmware-readiness. Matched warm checks
are recorded below, with passing corrected-code CI run 1389. Pending work is
review and the owner's cold/warm native batch. Task 3 remains unchecked for
that native evidence; the PR stays draft. No later task is assigned or started.
All QEMU, debugger and remote-client jobs are stopped.

The earlier 0x21 request was measured warm-only. Linux uses 0x20 for the AX200
combined path in both states (secure-send alone uses bulk OUT). The canonical
request and legacy query are now qualified warm, including both DDC records.
No cold outcome is inferred from warm passthrough.

Known unexpected firmware boot/secure-result notifications after initialization
terminate continuity; they cannot silently restart the owned runtime controller.
No automatic upload or recovery follows that failure.

Resume review at signed d796271 found no concrete correctness defect in the
publication, retirement, notification-race or endpoint-transition paths by
independent source inspection. This is not controller validation. Exact-head
CI run [1373](https://git.internal/PyxisOS/pyxis-os/actions/runs/1373) passed
change detection and filesystem checks, but image build failed explicitly with
“AX200 owner mirror_root is missing in the metadata”. The published source
remained a draft awaiting cached binaries and USB access; those dependencies
are now supplied.

The first task 3 warm boot at c631a90 failed closed with “invalid Intel version
length (phase read version, call status 6)”. No SFI upload or DDC command was sent;
both receive streams remained posted and the storage workload passed. The
initial query incorrectly supplied TLV selector 0xff while expecting a legacy
reply. Linux's AX200 combined path explains that operational firmware supports
both formats and selects the zero-parameter legacy command for its legacy
bootloader setup. This implementation now uses that query throughout. The
failure establishes neither an unknown firmware build nor a cold-upload result.

The next warm boot at afbbfc7 verified the expected operational version but
failed closed on the first DDC response length. Scalar debugger inspection
measured three return bytes: success status and a two-byte identifier of zero.
The [BlueZ Intel decoder](https://raw.githubusercontent.com/bluez/bluez/master/monitor/intel.c)
defines this three-byte DDC Config Write reply. The parser now requires that
format and successful status; its identifier is parsed trace metadata, not an
invented echo check. The outstanding opcode and dual USB/HCI retirement still
correlate each serialized DDC command. Neither failed boot is a readiness result.

## Measured warm qualification, 2026-10-09

The corrected measured revision is signed **9b75b3a**. Its ordinary local image
build and [CI run 1389](https://git.internal/PyxisOS/pyxis-os/actions/runs/1389)
passed. The saved main **52451d3** baseline image/ELF was assembled from matching
successful CI run 1349 bundles before implementation; the actual attached
measurements were taken after code was written, against that unchanged image,
before the post-change measurements. Raw captures and generated provenance are
retained locally in /tmp/pyxis-bluetooth-firmware-readiness; this summary keeps
the measured revisions, configuration, commands, results and limits.

Both builds used userspace 362574b1ed93827bac9219c23ee511afdf8d07f5,
ports a642f07382e14bd233ac1be2b6a814e95c32d835,
filesystem b427df29f865bc361b8da92bcd74e114581e9a32 and
lwIP a1aadb91a50360ff5b52864f7cec810b8162ee85. The warm image used the current
kernel with matching reused SDK/userspace/ports bundles. No public ABI or
dependency pin changed in this task; no stale kernel bundle was used.

| Measured ELF | SHA-256 |
| --- | --- |
| Baseline 52451d3 | 7a6f0c3a5e5799df7217c127c810b1b9870f0ebfd7d33584b8760756cf6a1f7c |
| Warm 9b75b3a | aaa0b2331b01339373a0e87be5184e5cedf20da94115ac8545609a0c0de9455d |

Both images used ThinkPad Fedora KVM, QEMU 10.2.2, Q35, cpu=max, four single-thread
cores, 2 GiB RAM, standard VGA with display disabled, fresh Fedora OVMF variables,
VirtIO RNG/network, USB AX200 at qemu-xhci port 1 and the private disk at port 2.
The inventory maps these to full-speed root port 5 and SuperSpeed root port 2.
Firmware code/variables were /usr/share/edk2/ovmf/OVMF_CODE.fd and OVMF_VARS.fd.
Each launch checked the single 8087:0029 device remained /dev/bus/usb/004/003,
read/write access and inactive Fedora Bluetooth. No device node changed.

The 68 MiB GPT fixture has a 64 MiB npfs system volume, 8 MiB journal and the
existing 1 MiB iobench fixture. Each guest received a fresh writable file copy,
while the workload's [space grant](storage-boot.lua) was read-only. The original
disk was never attached. Preparation corrected an override that redefined the
system volume and replaced the installed pyxis space instead of adding another
network owner; the rejected preparation boot is not a measured workload.
The formatter and structural checker passed on the corrected private pool.

Manual remote-shell commands were:

```text
lsusb -n
cat system://README.txt
iobench read system://iobench.bin --buffer 65536 --rounds 3
exit
```

All commands succeeded in every listed boot. Inventory was complete; all one
warmup plus three 1 MiB read samples verified. Repeated samples within each boot
use the existing npfs cache; they are not cold-media or concurrent radio traffic.
Profiling was off and no local build ran during measurements.

| Boot | Payload reads (ms) | Complete consumption (ms) |
| --- | --- | --- |
| Baseline 1 | 134.151, 137.390, 138.994 | 135.423, 140.378, 140.432 |
| Baseline 2 | 115.015, 125.017, 126.362 | 117.182, 126.046, 127.545 |
| Baseline 3 | 123.352, 122.972, 123.500 | 125.015, 125.786, 124.897 |
| Warm 1 | 115.532, 126.117, 124.397 | 117.723, 128.266, 126.908 |
| Warm 2 | 139.219, 153.324, 154.710 | 141.271, 155.968, 155.815 |
| Warm 3 | 118.080, 124.911, 123.632 | 119.821, 126.923, 125.261 |

Payload median/range: baseline **125.017 / 115.015–138.994 ms**, warm
**124.911 / 115.532–154.710 ms**. Complete-consumption median/range: baseline
**126.046 / 117.182–140.432 ms**, warm **126.923 / 117.723–155.968 ms**.
Warm boot 2 was slower; ambient host scheduling was not isolated and its cause
was not measured. These data establish coexistence, not a throughput improvement
or an explanation of that variation. No optimization follows from this sample.

After each remote workload exited, scalar GDB reads bracketed 30-second idle
intervals. GDB detached outside the timed /proc/PID/stat CPU interval; host
monotonic time and 100 ticks/s supplied elapsed and user/system CPU. The debugger
counter window also includes its short inspection time. CPU cost is the entire
QEMU process relative to one host CPU, not isolated Bluetooth cost or a native
Pyxis measurement. All six intervals had zero additional xHCI IRQs, commands and
events, unchanged endpoint sequences, READY and no terminal failure.

| Boot | Actual interval (s) | QEMU user/system (s) | One-CPU cost | BSP/AP1/AP2/AP3 timer deltas |
| --- | ---: | --- | ---: | --- |
| Baseline 1 | 30.000093 | 3.68 / 4.17 | 26.17% | 7949 / 3616 / 3616 / 3616 |
| Baseline 2 | 30.000371 | 4.07 / 4.04 | 27.03% | 7773 / 3616 / 3616 / 3616 |
| Baseline 3 | 30.000101 | 3.72 / 4.33 | 26.83% | 7767 / 3616 / 3616 / 3616 |
| Warm 1 | 30.000310 | 3.97 / 4.25 | 27.40% | 7966 / 3616 / 3615 / 3615 |
| Warm 2 | 30.000115 | 3.74 / 3.95 | 25.63% | 6788 / 3615 / 3616 / 3616 |
| Warm 3 | 30.000139 | 4.10 / 4.06 | 27.20% | 7921 / 3616 / 3616 / 3616 |

Idle median/range: baseline **26.83 / 26.17–27.03%**, warm
**27.20 / 25.63–27.40%**. This variation does not isolate a firmware-path cost.

All three warm boots had HCI/firmware READY, cold=false, zero upload commands and
bytes, two DDC records from nine bytes, firmware build 193/week 33/year 24, one
command credit and 3/3 ACL credits with 251-byte payloads. Twelve interrupt
completions supplied initialization replies; neither endpoint held a partial
frame. Both interrupt and bulk slots were POSTED, with USB_OK and empty copied
queues. Each boot had exactly one Bluetooth summary: warm skip, DDC, development
firmware. Independent source re-review of the two format corrections found no
concrete defect; that review does not qualify native cold operation.

### Passthrough handoff failure retained

An immediately relaunched guest at the same corrected code revision received a
successful Command Complete for **0x100e**, while Pyxis had published only its
initial **0xfc05**. It correctly failed closed with receive continuity lost,
before SFI or DDC. The native call status was 16; both receive streams remained
POSTED, and storage still verified. This failed controller boot is excluded from
the successful readiness comparison, not discarded. The host journal also showed
btusb initialization and command timeouts across rebinds. A late host completion
is an inference consistent with those observations, not proven packet origin.

The two subsequent manual launches allowed at least 20 seconds for host rebind
initialization before attachment and passed. That pause is a measurement
precondition, not a controller recovery policy or proof of quiescence. Unknown
completions remain terminal; no ignore, drain-and-reset, automatic retry or USB
re-enumeration recovery was added. Native cold/warm boots still need the owner
batch above. Actual ACL, encrypted reconnect and pointer use remain later tasks.

## Hosting-controller follow-up, 2026-10-09

Signed code revision **a2e81782** implements the accepted controller-local
inventory gate. Kernel/image builds passed with the same compiler and reused
bundles; [CI run 1446](https://git.internal/PyxisOS/pyxis-os/actions/runs/1446)
passed all jobs. Independent source review confirms that only the final core
COMPLETE record qualifies the host, including descriptor/descendant, budget,
deadline and hardware-failure checks. Identified candidates count before binding,
and calls before successful attachment return unavailable without queuing.

An interactive QEMU boot used the earlier four-CPU/2-GiB KVM configuration plus
`-device usb-ehci,id=bt_other`. AX200 and the private storage fixture remained on
qemu-xhci. Inventory retained two controllers: xHCI COMPLETE, EHCI UNSUPPORTED,
global USB INCOMPLETE. HCI was sealed, hosting-complete, attached to one AX200 and
READY, with zero SFI upload commands, two completed DDC records, one command
credit and three ACL credits. The single Bluetooth summary reported warm skip,
DDC and development firmware. This is warm passthrough, not native cold evidence.

The existing commands above verified text reading and all storage samples.
`lsusb -n` correctly exited 1 for global incompleteness while listing both
controller states; the text/iobench commands exited 0. Payload median/range was
**125.306 / 116.379–129.631 ms**, complete consumption
**127.999 / 118.784–131.731 ms**. During a 30.000412-second idle observation, xHCI
IRQ/command/event deltas were all zero; HCI stayed READY, endpoint sequences and
partial counts were unchanged, both receive pairs stayed POSTED and queues empty.
No new performance comparison or runtime incomplete-host injection was performed.
Raw captures remain local in /tmp/pyxis-bluetooth-host-inventory. QEMU/debugger
jobs are stopped. The owner must repeat native cold power-on and warm reboot
with a fresh image; #564 remains draft and task 4 remains unassigned.

## Native BOOT completion follow-up

Owner-reported 2026-10-09 image: main 51cbec9 plus #564 at a1d97dc5 and #578.
Hosting inventory passed; SFI validation/secure upload completed, then BOOT
failed with call status 19 and “failed or invalid async bulk IN transfer
completion”, halting xHCI 07:00.4. A direct warm reboot from Pyxis into the same
image reached warm skip/DDC READY. This establishes that the uploaded firmware
became operational; cold BOOT retirement/readiness still failed.

Source inspection narrows the reported rejection to completion semantics after
matching a posted receive TD and IN endpoint. SHORT_PACKET with a bounded
residual, including zero bytes, was already accepted; bounded STALL retained DMA
and stopped rearm without this immediate controller-halt message. HCI frames
split across successful transfers were already reassembled. The old path rejected
SUCCESS with nonzero residual, oversized residuals and all other error codes.
The native code/residual is unknown; phase BOOT alone does not prove reset
publication or receipt of its real notification.

[Linux v6.18 xHCI](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/host/xhci-ring.c#L2535-L2541)
normalizes SUCCESS with nonzero residual to short transfer. The bounded correction
applies to retained interrupt and async bulk IN only: require the owned TD and
SUCCESS/SHORT_PACKET, require residual <= requested, copy exactly requested minus
residual and use existing FIFO/sequence retirement. It adds no BOOT-only exception.
OUT/control, malformed bounds, STALL, other error and DMA retention are unchanged.
This fixes a concrete compatibility rejection; it is not yet the proven native
cause. New parsed traces identify remaining errors and BOOT publication/retirement
without logging addresses, payloads or keys.

[btintel_boot](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.c#L1782-L1823)
arms booting, sends reset and waits five seconds for the real boot notification;
it does not restart receive URBs specially for successful boot.
[btusb](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btusb.c#L1310-L1465)
parses successful receives and resubmits non-unlink errors while running, using
Linux's endpoint recovery/retirement machinery. Pyxis cannot safely copy that
resubmission alone. Transaction/babble/stopped or malformed completions remain
fail-closed; a trace showing them needs a reviewed ownership-safe recovery design.
Real boot notification, USB completion, clean frame boundaries and operational
version confirmation remain mandatory. The single trace batch above determines
whether this correction resolves the native failure. #564 stays draft.


### Warm regression check of the completion correction

Measured code/image revision: 2cf50703, `LOG_LEVEL=trace`, with the earlier
ThinkPad Fedora KVM/Q35 configuration (four CPUs, 2 GiB, real warm AX200,
private USB storage and an unsupported `usb-ehci`). The hosting xHCI was COMPLETE,
EHCI stayed UNSUPPORTED and `lsusb -n` correctly returned 1 for global inventory
incompleteness. Bluetooth reached warm skip/DDC/development READY: one candidate,
zero upload commands, two DDC commands, command credit 1, ACL credits 3 and
payload limit 251 bytes. Text retrieval and all three 1-MiB storage samples
verified; trace-build timing is not a performance comparison.

Read-only GDB found both interrupt and bulk receive pairs POSTED, queues empty,
no partial HCI frame and no terminal failure. A separate idle observation with
a 30-second quiet wait kept those scalar observations and xHCI interrupt/command/
event counters unchanged. Neither stream had recorded a
SUCCESS with nonzero residual, so this run does not exercise the compatibility
case or the cold BOOT transition. Info/trace kernel builds and the trace image
build passed; matching SDK/userspace/ports bundles were reused. No public ABI,
dependency pin or firmware asset changed. Raw captures stay local.
QEMU, debugger and remote-client jobs are stopped. Fedora Bluetooth remains
inactive for the investigation; re-enable it when Bluetooth work is finished.


## Native transaction-error transition, 2026-10-09

Owner-reported diagnostic image: main e6cc8a3 plus #564 de83ce0c, trace level;
full log was supplied at `/shared/batch-2026-10-09/caelum-3.log` on horse. SFI
validated at 801016 bytes/build 193/33/2024, secure notification succeeded, and
3237 upload commands carried 801012 bytes in 2.756 seconds. BOOT control ticket 8
was published and USB-retired with 11/11 bytes. The next owned boot bulk IN TD
reported completion 4 (USB Transaction Error), residual/requested 4096/4096,
slot 2/DCI 5, receive 1/ring index 177, with both copied queues empty. The host
halted and HCI failed at BOOT. Warm reboot ran operational type 35, the same
uploaded build, both DDC records and warm skip READY. These are owner-reported
native observations, not this agent's QEMU measurements.

This establishes the rejected USB error and zero-byte count; losing the bootloader
bulk event path as firmware switches is the transition interpretation. The earlier
SUCCESS/residual correction was not the native cause and remains bounded Linux
compatibility behavior, not a claim that xHCI normally specifies such SUCCESS.

Implemented exception: exactly one owned zero-byte transaction error during a
real published cold BOOT, with no copied bulk prefix or partial boot event. End
bulk event framing, transfer its sequence authority to ACL framing and retain
both boot receive owners. Consume the error/ERDP, suppress IN rearm, then use the
outer worker's existing bounded command path for Reset Endpoint TSP=1 and Set TR
Dequeue at the current producer frontier/cycle. TSP=1 preserves the USB toggle;
a transaction error does not establish a device STALL. No CLEAR_FEATURE or retry
of the errored TD is introduced. Both command completions must precede buffer
reuse, and the original BOOT deadline is not extended.

[The xHCI specification, sections 4.6.8–4.6.10](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf)
describes halt/reset, transfer-state preservation and dequeue cache invalidation.
After the fence, IN remains suspended until real interrupt boot notification,
BOOT USB retirement and clean event boundaries permit fresh operational receives.
HCI BOOT retirement is held until the host fence/rearm, preserving real-notification
and operational-version gates. Class/event-drain progress still never waits;
hardware commands run in the outer worker while their waits drain other events.
A second/data-bearing error, unknown owner, queued/partial prefix, missed deadline
or failed host command remains fail-closed. No runtime retry policy was added.

The next owner batch uses the default info level for one cold power-on and direct
warm reboot of the reviewed image. Native cold readiness and operational bulk
reuse remain qualification gates; this code inspection is not a native pass.
Task 3 stays open and #564 draft. Task 4 is not assigned.


### Warm QEMU regression of the transaction-error fix

Measured code/image revision: 82a6fe85, default `LOG_LEVEL=info`, ThinkPad Fedora
KVM/Q35, four CPUs/2 GiB, real warm AX200, private USB storage and unsupported
EHCI as above. The actual node remained `/dev/bus/usb/004/003` with read/write
access; Fedora Bluetooth stayed inactive. Hosting xHCI COMPLETE, EHCI UNSUPPORTED
and `lsusb -n` status 1 preserved the accepted inventory behavior. The single
Bluetooth klog summary was warm skip/DDC/development READY. Read-only GDB found
one candidate, zero upload commands, two DDC commands, command credit 1, ACL
credits 3/payload 251, no partial frame/terminal failure and no boot-transition
exception used. Both receive pairs were POSTED, queues empty, IN not halted and
boot state NONE. Across a separate 30-second quiet wait all those scalars and
xHCI IRQ/command/event counts were unchanged (895/8/903).

Text retrieval and the existing three-sample 1-MiB storage workload verified.
Payload median/range was 145.111 ms / 137.711–146.172 ms; complete consumption
146.844 ms / 139.435–147.282 ms. These are unprofiled warm fixture observations,
not native radio throughput or a comparison with the earlier trace-level run.
Info/trace kernel builds and the default-level image build passed, using matching
SDK/userspace/ports bundles and the current kernel. CI 1482 passed all existing
jobs for 82a6fe85. Independent source re-review resolved the framing-sequence and
notification-origin findings and found no remaining blocking issue.

QEMU's warm controller did not exercise the expected code-4 transition or its
reset/dequeue fence; no fault injection was performed. Native default-level
cold/warm qualification remains required. Raw captures are local in
`/tmp/pyxis-bluetooth-boot-transition`; no raw evidence files were added. QEMU,
GDB and remote clients are stopped; the ThinkPad is free. Fedora Bluetooth is
still inactive. Re-enable it when Bluetooth work is finished. Task 4 remains
unassigned; stopping for review of #564.
