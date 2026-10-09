# Bluetooth mouse task 3: firmware readiness

Status: **implementation ready for review; warm passthrough qualified, native cold evidence pending.**
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

Code inspection identifies fail_adapter(CALL_UNAVAILABLE), not wire HCI status.
Both revisions share this code. A decoded non-warm version would also log
“running firmware outside development profile”. Incomplete inventory, ambiguous
candidate/attachment, rejected initialization reply, invalid capabilities and
unclean logical cleanup can share the generic emitter. The sole line cannot
identify its caller; the exact cold branch remains unconfirmed.
Task 3 now reports one specific initialization phase/reason in klog, without a
second generic Bluetooth failure line. Success remains one upload/warm-skip
summary; details use ktrace. No addresses, raw packets or keys are recorded.

## Owner's next native batch

Collect after implementation is ready; no single native boot is requested now.
Booting Pyxis takes this session offline.

1. Record reviewed revision and image/ELF identity. Fully power off, then cold
   power on directly into Pyxis/PXE without Fedora first. Record the one upload,
   DDC and operational-readiness summary, or its specific failed phase/reason.
   Success should read “AX200 USB ready (cold upload, DDC, development firmware)”.
   Record USB inventory completeness.
2. Warm reboot Pyxis into that same image. Record warm skip or specific failure;
   distinguish reboot from cold power-on. The expected skip summary is
   “AX200 USB ready (warm skip, DDC, development firmware)”.
3. If necessary repeat with LOG_LEVEL=trace, retaining only parsed firmware,
   boot/security/build fields, progress counts and inventory state. Remove
   addresses before recording; no packet or memory dumps.
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
before the post-change measurements. [Artifact identities](artifact-identities.txt)
identify both images, ELFs and the unattached original storage fixture.

The [baseline kernel](baseline-kernel-provenance.txt),
[SDK](baseline-sdk-provenance.txt), [userspace](baseline-userspace-provenance.txt)
and [ports](baseline-ports-provenance.txt) records identify its inputs. The
[warm kernel](warm-kernel-provenance.txt), [SDK](warm-sdk-provenance.txt),
[userspace](warm-userspace-provenance.txt) and [ports](warm-ports-provenance.txt)
records show the current kernel and matching reused bundles. No public ABI or
dependency pin changed in this task; no stale kernel bundle was used.

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

| Boot/capture | Payload reads (ms) | Complete consumption (ms) |
| --- | --- | --- |
| [Baseline 1](baseline-1-storage.txt) | 134.151, 137.390, 138.994 | 135.423, 140.378, 140.432 |
| [Baseline 2](baseline-2-storage.txt) | 115.015, 125.017, 126.362 | 117.182, 126.046, 127.545 |
| [Baseline 3](baseline-3-storage.txt) | 123.352, 122.972, 123.500 | 125.015, 125.786, 124.897 |
| [Warm 1](warm-1-storage.txt) | 115.532, 126.117, 124.397 | 117.723, 128.266, 126.908 |
| [Warm 2](warm-2-storage.txt) | 139.219, 153.324, 154.710 | 141.271, 155.968, 155.815 |
| [Warm 3](warm-3-storage.txt) | 118.080, 124.911, 123.632 | 119.821, 126.923, 125.261 |

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

| Boot/capture | Actual interval (s) | QEMU user/system (s) | One-CPU cost | BSP/AP1/AP2/AP3 timer deltas |
| --- | ---: | --- | ---: | --- |
| [Baseline 1](baseline-1-idle.txt) | 30.000093 | 3.68 / 4.17 | 26.17% | 7949 / 3616 / 3616 / 3616 |
| [Baseline 2](baseline-2-idle.txt) | 30.000371 | 4.07 / 4.04 | 27.03% | 7773 / 3616 / 3616 / 3616 |
| [Baseline 3](baseline-3-idle.txt) | 30.000101 | 3.72 / 4.33 | 26.83% | 7767 / 3616 / 3616 / 3616 |
| [Warm 1](warm-1-idle.txt) | 30.000310 | 3.97 / 4.25 | 27.40% | 7966 / 3616 / 3615 / 3615 |
| [Warm 2](warm-2-idle.txt) | 30.000115 | 3.74 / 3.95 | 25.63% | 6788 / 3615 / 3616 / 3616 |
| [Warm 3](warm-3-idle.txt) | 30.000139 | 4.10 / 4.06 | 27.20% | 7921 / 3616 / 3616 / 3616 |

Idle median/range: baseline **26.83 / 26.17–27.03%**, warm
**27.20 / 25.63–27.40%**. This variation does not isolate a firmware-path cost.

Each [warm state 1](warm-1-state.txt), [2](warm-2-state.txt) and
[3](warm-3-state.txt) had HCI/firmware READY, cold=false, zero upload commands and
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
before SFI or DDC. [Parsed evidence](handoff-failure.txt) retains the mismatch;
storage still verified. This failed controller boot is excluded from the
successful readiness comparison, not discarded. The host journal also showed
btusb initialization and command timeouts across rebinds. A late host completion
is an inference consistent with those observations, not proven packet origin.

The two subsequent manual launches allowed at least 20 seconds for host rebind
initialization before attachment and passed. That pause is a measurement
precondition, not a controller recovery policy or proof of quiescence. Unknown
completions remain terminal; no ignore, drain-and-reset, automatic retry or USB
re-enumeration recovery was added. Native cold/warm boots still need the owner
batch above. Actual ACL, encrypted reconnect and pointer use remain later tasks.
