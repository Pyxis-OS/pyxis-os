# Bluetooth mouse task 3: firmware readiness

Status: **assigned 2026-10-09; plan posted before implementation.**
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

Pyxis owns this dependency and image assembly. A focused parent recipe will
download only owner mirror URLs, check all four sizes/hashes, and stage unchanged
firmware under share/firmware/intel/ and license/provenance under
share/licenses/intel-bluetooth/ in the boot archive. Runtime needs no network or
disk mount. Missing/mismatched assets fail image assembly; no upstream fallback
or implicit host firmware input. Kernel compilation remains independent.
No ports/userland pin or compiler-container change is planned.

**Owner mirror URLs are required before consumption.** No matching entry was
found in the inspected mirror/cache catalogs. This is a missing build dependency,
not a new firmware-policy decision.

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
Task 3 will report one specific initialization phase/reason in klog, without a
second generic Bluetooth failure line. Success remains one upload/warm-skip
summary; details use ktrace. No addresses, raw packets or keys are recorded.

## Owner's next native batch

Collect after implementation is ready; no single native boot is requested now.
Booting Pyxis takes this session offline.

1. Record reviewed revision and image/ELF identity. Fully power off, then cold
   power on directly into Pyxis/PXE without Fedora first. Record the one upload,
   DDC and operational-readiness summary, or its specific failed phase/reason.
   Record USB inventory completeness.
2. Warm reboot Pyxis into that same image. Record warm skip or specific failure;
   distinguish reboot from cold power-on.
3. If necessary repeat with LOG_LEVEL=trace, retaining only parsed firmware,
   boot/security/build fields, progress counts and inventory state. Remove
   addresses before recording; no packet or memory dumps.
4. Return to Fedora. **Re-enable Fedora Bluetooth when Bluetooth work is done**
   with sudo systemctl enable --now bluetooth.service. Reload the Codex SSH key
   after reboot as usual.

QEMU passthrough can qualify warm skip and storage coexistence while Fedora has
initialized AX200; it does not establish Pyxis cold upload. Native results remain
owner-reported until supplied. This plan claims no task 3 completion.
