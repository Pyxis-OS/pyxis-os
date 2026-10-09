# Bluetooth HCI transport and controller state probe

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Task 3b completed on 2026-10-08. The real AX200 answered HCI Reset and Intel Read Version through Pyxis's
EP0/interrupt-IN transport in QEMU passthrough, both with checked HCI framing, matching opcodes and successful
status. The version identifies operational firmware after Reset on this prepared host; it does not establish cold
native startup state.

## Revisions and execution

Baseline main `e75ef1b427d535414bb9f7136f0f45f70441ddb2`, including [merged #524](https://git.internal/PyxisOS/pyxis-os/pulls/524),
with SDK, userland and ports bundles from successful [CI run 1215](https://git.internal/PyxisOS/pyxis-os/actions/runs/1215)
that passed the verifier. The [probe commit](https://git.internal/PyxisOS/pyxis-os/commit/9c79a51d999cd143113948faee57ef6377b78d88)
on `probe/bluetooth-hci-state` adds only the temporary HCI consumer to `kernel/usb/core.c` (replacing the earlier
ring-wrap consumer for this task); do not merge it. It was built from a clean checkout with the local Clang/LLD 23.1.3
toolchain, fork `41ab6043cc4fd63e0a358d1e60bba249a751d8ee`, using
`make -j16 image PREBUILT="sdk userspace ports"`, without compiler warnings. Pins: userspace
`28f8c1686a92e7872cab1e2e2cecae09a4a26070`, ports `e85d307336af66d6d5171c5f943ffe312fd086d5`, filesystem
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP `a1aadb91a50360ff5b52864f7cec810b8162ee85`.

Execution used the physical ThinkPad's Fedora/KVM (not nested) with QEMU `10.2.2-1.fc44`: Q35, four CPUs, 2 GiB, the
Fedora OVMF pair with a fresh variables copy, ISO boot, `-display none`, VirtIO RNG and networking with a loopback
forward for the remote shell, and AX200 `8087:0029` attached to `qemu-xhci` by VID/PID
(`usb-host,bus=xhci.0,port=1`), with a GDB port for the probe run. The invoking user had device access, Bluetooth stayed
inactive/disabled and rfkill unblocked. The baseline CI image and probe image both booted all configured spaces, and
the remote shell's `lsusb -n` exited 0 with complete AX200 inventory and both interfaces, with complete draining on
`exit`. No automation, self-test, fault injection or CI job was added.

## Bounded probe behavior

The consumer runs on the BSP controller worker during boot enumeration. It selects only the checked AX200 interface 0,
alternate 0, Bluetooth class `e0/01/01` and its unique interrupt-IN endpoint (observed `0x81`, packet 64, interval 1),
configures the host endpoint before SET_CONFIGURATION, and posts 257-byte receives through the merged private
interface. It sends only `03 0c 00` (Reset, opcode `0x0c03`) and `05 fc 00` (legacy Intel Read Version, opcode
`0xfc05`), each on EP0 with request type `0x21`, request, value and interface index zero and length three, following the
[Bluetooth USB interface request format](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/host-controller-interface/usb-transport-layer.html);
EP0 success and actual outbound length are checked separately from HCI success.

Each interrupt completion must have the next consecutive sequence and exactly `2 + parameter_length` bytes. Command
Complete carries credit, little-endian opcode and return parameters; Command Status carries status, credit and opcode.
The probe accepts one initial command allowance, replaces it with valid advertised credits and waits before sending
another command if credits are zero. Reset must complete successfully before Read Version, and unrelated framed events
are logged and processed for credit updates against the original deadline (five seconds per command, capped by the
30-second enumeration deadline). A matching nonzero status is command rejection even when the error reply is shorter
than the successful layout; a successful matching Command Status is explicitly unsupported for these Complete-based
commands; successful Reset requires one return byte and legacy Read Version ten. A malformed packet, sequence gap,
Hardware Error, timeout or terminal USB result ends the probe with no retry or recovery, and failed probes report
unknown state (this bounded probe's policy; framing, error and credit rules follow the
[HCI functional specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/host-controller-interface/host-controller-interface-functional-specification.html)).
Received class packets stay in the unmerged consumer.

## Observed replies and controller state

The serial capture recorded every event from the final probe run:

| Command | Sequence | Event bytes | Result | Command allowance |
| --- | ---: | --- | --- | ---: |
| Reset | 1 | `0e 04 02 03 0c 00` | matching Complete, status 0 | 2 |
| Intel Read Version | 2 | `0e 0d 01 05 fc 00 37 14 01 23 03 c1 21 18 00` | matching Complete, status 0 | 1 |

The version return fields, per Linux v6.18's
[legacy version layout](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.h):

| Field | Value |
| --- | --- |
| status | `0x00` |
| hardware platform / variant / revision | `0x37` / `0x14` / `0x01` |
| firmware variant / revision | `0x23` / `0x03` |
| build number / week / year / patch | 193 / 33 / 24 / 0 |

Linux's [legacy interpretation](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.c) maps firmware
variant `0x06` to bootloader and `0x23` to firmware; the probe applies those meanings only to the observed legacy
platform/AX200 profile and leaves other values or profiles unknown, so operational firmware is an interpretation of
measured bytes using that reference protocol. Pyxis did not load firmware or send an Intel reboot/download command.
Post-boot GDB showed `finished=true`, `result=USB_OK`, `PROBE_STATE_FIRMWARE`, sequence 2 and credit 1, an empty copied
queue, both receive owners `INTERRUPT_POSTED`, stream `USB_OK` and the controller running; receives and DMA stay owned
after the class probe finishes, so this is not a cancellation or reclamation path. QEMU and GDB exited and `btusb`
rebound.

## Review follow-up and limits

The #524 reviewer asked that task 4 explicitly detect controller state before loading firmware and that the 257-byte
resource explain its HCI origin. The checklist now handles already-operational, bootloader and unknown states, and
`settings.h` names the two-byte header plus 255-parameter limit; the ordinary image was rebuilt without HCI-probe
symbols. Only prepared-host QEMU passthrough was exercised and cold native startup remains unqualified. Bootloader
replies, unknown variants, zero-credit waits, shortened command rejections, malformed or unrelated events and terminal
failures have source review, not forced hardware observations; the six- and fifteen-byte events do not qualify
multi-packet framing or sustained traffic. No firmware filename was derived, file mirrored or firmware uploaded and no
LE scan was attempted; operational firmware here does not complete the firmware-load task. No public ABI, pin or
compiler/container input changed.
