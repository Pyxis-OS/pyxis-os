# Bluetooth investigation results

Tasks 1–5 completed on 2026-10-08 under the owner's accepted warm-host scope.
Pyxis enumerated the real Intel AX200, received HCI events, verified existing
operational firmware and identified the owner's MX Master 3S in an LE scan.
This establishes a path to implementing a mouse stack; it does not establish a
working Bluetooth mouse. Connections, pairing, bonds, GATT and pointer delivery
were not attempted.

The owner has seen the scan results and accepted the stack, security and closure
direction in the [mouse milestone proposal](../wip/bluetooth-mouse.md) on
2026-10-08. Its later policy decisions remain pending. The completed WIP is now
the [AX200 reference](../devices/ax200-bluetooth.md); retiring it merges
no probe code and authorizes no implementation.

## Evidence by task

The linked task reports are authoritative for revisions, commands, build inputs,
captures and detailed limits. This table summarizes their observations rather
than reproducing those artifacts.

| Task and evidence | QEMU passthrough result | Native Pyxis evidence |
| --- | --- | --- |
| [1. Attachment and inventory](experiments/bluetooth-task1/README.md) | QEMU detached both `btusb` interfaces, attached full-speed `8087:0029`, and Pyxis read both interfaces/all alternates. HCI has interrupt IN `0x81`, packet 64, interval 1, and ACL bulk `0x02`/`0x82`. Guest `lsusb -n` completed successfully. | The earlier [ThinkPad inventory](../targets/t14-gen1-amd/usb-bringup.md) identifies the same AX200 on AMD xHCI `07:00.4`. It is inventory evidence, not native transfer qualification. |
| [2. Interrupt-IN assessment](experiments/bluetooth-task2/README.md) | Source/specification assessment only: endpoint context, independent DMA ownership, copied buffering, progress in other transfer waits and explicit discontinuity were required. No traffic measurement in this task. | No new native check. |
| [3a. Shared interrupt IN](experiments/usb-interrupt-in/README.md) | Real events survived a six-second idle timeout, then Reset plus 270 Read Version replies. A final reply brought the sequence to 272 across the 255-TRB ring wrap; GDB observed capture/rearm inside an EP0 wait. Read-only emulated USB storage coexisted. QEMU removal with posted receives quarantined the controller, including its storage, and retained DMA. | Periodic reception, storage coexistence and removal remain unqualified natively. |
| [3b. HCI controller state](experiments/bluetooth-task3b/README.md) | Reset and Intel Read Version returned checked Command Complete events, matching opcodes, status 0 and command allowances 2 then 1. The version contained firmware variant `0x23`, build 193/week 33/year 24. | No native HCI observation. |
| [4. Warm firmware verification](experiments/bluetooth-task4/README.md) | Loading was deliberately skipped. A second checked Read Version returned the same ten-byte record; GDB confirmed skip/verification success, an empty copied queue and two posted receives. No firmware upload or DDC write occurred. | No cold-state capture, upload or native firmware-readiness result. |
| [5. First LE scan](experiments/bluetooth-task5/README.md) | One accepted 30-second active scan found shortened name `MX Master`, HID service `0x1812` and mouse appearance `0x03C2`; its correlated scan response supplied complete name `MX Master 3S`. All eight commands succeeded and disable was confirmed. Final totals: 38 HCI events, 30 advertising/scan-response records, 19 run-local advertiser records, 11 scan responses and one identified candidate. | No native scan or mouse-input result. |

The later passthrough runs used the physical ThinkPad running Fedora/KVM,
QEMU 10.2.2, Q35, four guest CPUs, 2 GiB RAM, fresh OVMF variables and an
emulated xHCI owning the real USB function. Task 1 used one guest CPU. These
were not nested-VM measurements, and emulated xHCI is not the native AMD host
controller. Fedora Bluetooth remained inactive/disabled. The detailed reports
record each matching ELF and dependency bundle, including the task 5 local/CI
compiler-fork difference; this summary makes no new build-equivalence claim.

## Observations and their interpretation

The returned version bytes are observations. Interpreting Intel variant `0x23`
as operational firmware follows the source reference linked in tasks 3b/4.
Fedora's host journal records an earlier bootloader state, SFI upload and DDC
application with the same build. Together these support the inference that
Fedora's firmware survived passthrough attachment and HCI Reset. They do not
prove that a USB reset always preserves firmware or that Pyxis can initialize
a controller after a cold boot.

Task 4 records installed SFI/DDC aliases, sizes and hashes as host provenance,
not a selected Pyxis pin. No firmware blob, mirror dependency or uploader was
added. The owner accepted this warm verification for the investigation only;
[cold upload and running-version policy](../technical-debt.md#bluetooth-cold-firmware-upload-and-running-version-policy)
remain open for a persistent stack.

Task 5's complete name and HID/appearance fields satisfied the agreed scan
identification criterion. The complete name arrived in an active scan response.
That is neither cryptographic peer authentication nor evidence of the mouse's
pairing capabilities or GATT report layout. The 19 address/type records are
run-local correlation entries, not a physical-device count. Duplicate filtering
does not prove freshness or losslessness. The largest event was 45 bytes; these
runs do not qualify large multi-packet HCI events or sustained ACL traffic.

The radio-tested scan source is signed revision
[`3d0bc3c`](https://git.internal/PyxisOS/pyxis-os/commit/3d0bc3c8aa79e1ede4ff8d366e759d8064eb3cf8).
Its tree is identical to the measured revision, so the recorded ELF still
corresponds; its embedded revision metadata retains the original hash. The
later privacy-only revision
[`9f12592`](https://git.internal/PyxisOS/pyxis-os/commit/9f12592e0098224d3cf2149d308d72393dbedb0e)
received a build and source review, without a second radio run. The task 5
report preserves that distinction and the tree-verification provenance.

## Implemented boundary and remaining qualification

Only [private kernel interrupt-IN support](../devices/usb-interrupt-in.md) is
merged as reusable code. HCI/state, firmware-skip and LE-scan consumers remain
on the unmerged probe branches identified by their reports. There is no
production Bluetooth class binder, raw-USB ABI or mouse stack on main.

The shared implementation admits boot-present, root-connected full-speed
devices, with one stream per device, two receives up to 257 bytes and eight
copied entries. STALL is terminal, FIFO overflow is a discontinuity, and active
removal can stop unrelated storage on the controller. Uncertain DMA ownership
retains backing until reboot. Overflow, STALL, malformed data and ambiguous scan
cleanup were source-reviewed; those failures were not forced in these runs.
Observed removal establishes shutdown, not recovery or reattachment.

The small matched remote-session checks in task 3a all measured 0.03 seconds at
0.01-second resolution. Task 5's one baseline and one post-scan session each
measured 0.05 seconds. These are limited completion checks, not throughput,
latency or native-performance results; they cannot isolate interrupt-copy cost.

Bluetooth addresses stayed out of the retained scan output, repository, docs and
PRs. Candidate identity was correlated privately in RAM; captures used parsed
fields and scalar debugger inspection. The scan made no connection and saved
no keys. All investigation QEMU/GDB jobs exited and both host interfaces rebound
to `btusb`; Fedora Bluetooth was disabled at the end of those probe runs. That is
historical cleanup, not a claim about the host's current service state. The
owner-run native-check batch, including re-enabling Fedora Bluetooth, follows
this investigation and has not been performed here.

## Owner-reported Linux pairing

On 2026-10-08 the owner reported that Fedora needed `bluetoothctl` to pair the
MX Master 3S: it did not appear in KDE's scan. Pairing required no PIN or
confirmation. The first pairing attempt failed and the second worked. This is
the owner's account, without a supplied protocol capture or a new Pyxis run.

The lack of PIN/confirmation fits Just Works, by inference; it establishes
neither Secure Connections support, negotiated key size nor exact IO capability.
Combined with Pyxis's measured scan, the account supports explicit discovery and
selection, with another enrollment attempt authorized by a user action after
failure. It does not justify an automatic pairing retry loop or legacy fallback.
The proposed early SMP feature gate remains unmeasured.
