# Bluetooth HCI transport and controller state probe

Task 3b completed on 2026-10-08. The real AX200 answered HCI Reset and Intel
Read Version through Pyxis's EP0/interrupt-IN transport in QEMU passthrough.
Both replies had checked HCI framing, matching opcodes and successful status.
The version identifies operational firmware after Reset on this prepared host;
it does not establish cold native startup state.

## Revisions and execution

The baseline was main `e75ef1b427d535414bb9f7136f0f45f70441ddb2`, including
[merged #524](https://git.internal/PyxisOS/pyxis-os/pulls/524). Its successful
[CI run 1215](https://git.internal/PyxisOS/pyxis-os/actions/runs/1215) supplied
matching SDK, userland and ports bundles. Their existing verifier passed.
The unmerged
[probe commit](https://git.internal/PyxisOS/pyxis-os/commit/9c79a51d999cd143113948faee57ef6377b78d88)
is on `probe/bluetooth-hci-state`, based on that main revision. It adds only
the temporary HCI consumer to `kernel/usb/core.c`; do not merge this branch.
It replaces the earlier ring-wrap qualification consumer for this task's work.

The probe image was built from a clean checkout of that commit using the local
Clang/LLD 23.1.3 toolchain, fork
`41ab6043cc4fd63e0a358d1e60bba249a751d8ee`:

```sh
PATH="$HOME/opt/pyxis-llvm/bin:$PATH" make -j16 image PREBUILT="sdk userspace ports"
```

The final build passed without compiler warnings. Dependency pins stayed at
the selected main revision:

| Repository | Revision |
| --- | --- |
| userspace | `28f8c1686a92e7872cab1e2e2cecae09a4a26070` |
| ports | `e85d307336af66d6d5171c5f943ffe312fd086d5` |
| filesystem | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |

Execution used the physical ThinkPad's Fedora/KVM, not nested virtualization,
with QEMU `10.2.2-1.fc44`, four CPUs and 2 GiB RAM. The invoking user had access
to AX200 `8087:0029`; Bluetooth remained inactive/disabled and rfkill unblocked.
The command used a fresh writable copy of Fedora's OVMF variables:

```text
qemu-system-x86_64
  -machine q35 -accel kvm -cpu max -rtc base=utc -smp 4 -m 2G
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd
  -drive if=pflash,format=raw,unit=1,file=FRESH_VARS_COPY
  -cdrom IMAGE_PATH -boot d -display none -serial file:SERIAL_CAPTURE -monitor stdio
  -netdev user,id=net,hostfwd=tcp:127.0.0.1:2323-10.0.2.15:2323
  -device virtio-net-pci,netdev=net,disable-legacy=on
  -object rng-random,id=rng,filename=/dev/urandom
  -device virtio-rng-pci,rng=rng,disable-legacy=on
  -device qemu-xhci,id=xhci
  -device usb-host,id=bluetooth,bus=xhci.0,port=1,vendorid=0x8087,productid=0x0029
```

The probe run also enabled `-gdb tcp:127.0.0.1:1234` and used its matching ELF.
The baseline CI image and probe image both booted all configured spaces. The
existing remote shell's `lsusb -n` exited 0 with complete AX200 inventory and
both interfaces; `exit` produced a complete final drain. No new boot automation,
self-test, fault injection or CI job was added.

## Bounded probe behavior

The consumer runs on the existing BSP controller worker during boot enumeration.
It selects only the checked AX200 interface 0, alternate 0, Bluetooth class
`e0/01/01` and its unique interrupt-IN endpoint. The observed endpoint was `0x81`,
packet 64, interval 1. It configures the host endpoint before SET_CONFIGURATION,
then posts 257-byte receives using the merged private interface.

It sends only `03 0c 00` (Reset, opcode `0x0c03`) and `05 fc 00` (legacy Intel
Read Version, opcode `0xfc05`). Each uses EP0 with request type `0x21`, request,
value and interface index zero, and length three. This follows the
[Bluetooth USB interface request format](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/host-controller-interface/usb-transport-layer.html).
EP0 success and actual outbound length are checked separately from HCI success.

Each interrupt completion must have the next consecutive sequence and exactly
`2 + parameter_length` bytes. Command Complete carries credit, little-endian
opcode and return parameters; Command Status carries status, credit and opcode.
The probe accepts one initial command allowance, replaces it with valid advertised
credits and waits before sending another command if credits are zero. Reset must
complete successfully before Read Version. Unrelated framed events are logged
and processed for credit updates against the original deadline, which is five
seconds per command capped by the existing 30-second enumeration deadline.

A matching nonzero status is command rejection, even when the error reply is
shorter than the successful layout. A successful matching Command Status is
explicitly unsupported for these Complete-based commands. A malformed packet,
sequence gap, Hardware Error, timeout or terminal USB result ends the probe;
there is no retry or recovery. Failed probes report unknown state. Those terminal
choices are this bounded probe's policy. Framing, command-error and credit rules
follow the [HCI event/flow-control specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/host-controller-interface/host-controller-interface-functional-specification.html).
Successful Reset requires one return byte; successful legacy Read Version
requires ten. Received class packets stay in the unmerged consumer.

## Observed replies and controller state

The [serial capture](serial.txt) records every event from the final probe run:

| Command | Sequence | Event bytes | Result | Command allowance |
| --- | ---: | --- | --- | ---: |
| Reset | 1 | `0e 04 02 03 0c 00` | matching Complete, status 0 | 2 |
| Intel Read Version | 2 | `0e 0d 01 05 fc 00 37 14 01 23 03 c1 21 18 00` | matching Complete, status 0 | 1 |

The version return fields, interpreted using Linux v6.18's
[legacy version layout](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.h), were:

| Field | Value |
| --- | --- |
| status | `0x00` |
| hardware platform / variant / revision | `0x37` / `0x14` / `0x01` |
| firmware variant / revision | `0x23` / `0x03` |
| build number / week / year / patch | 193 / 33 / 24 / 0 |

Linux's [legacy interpretation](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.c)
maps firmware variant `0x06` to bootloader and `0x23` to firmware. The probe applies
those meanings only to the observed legacy platform/AX200 profile; other values
or profiles remain unknown. Thus operational firmware is an interpretation of
measured bytes using that reference protocol. Pyxis did not load firmware or
send an Intel firmware reboot/download command.

Post-boot [GDB inspection](gdb.txt), with entered expressions retained, showed
`finished=true`, `result=USB_OK`, `PROBE_STATE_FIRMWARE`, sequence 2 and credit 1.
The copied queue was empty, both receive owners remained `INTERRUPT_POSTED`, the
stream was `USB_OK`, and the controller remained running. Receives and DMA remain
owned after the class probe finishes; this is not a cancellation/reclamation path.
All QEMU and GDB jobs exited and `btusb` rebound after the runs.

## Review follow-up and limits

The reviewer on #524 requested that task 4 explicitly detect controller state
before loading firmware, and that the generic 257-byte resource explain its
HCI origin. This delivery adds both: the checklist now handles already
operational, bootloader and unknown states, and `settings.h` names the two-byte
header plus 255-parameter limit. The ordinary image was rebuilt after returning
to the report branch; its ELF has no HCI-probe symbols.

Only prepared-host QEMU passthrough was exercised; cold native startup remains
unqualified. Parsing of bootloader replies, unknown variants, zero-credit waits,
shortened command rejections, malformed/unrelated events and terminal failures
has source review, not forced hardware observations. Six- and fifteen-byte events do not qualify
multi-packet event framing or sustained traffic. No firmware filename was derived,
file mirrored, firmware uploaded or LE scan attempted. These remain later tasks;
finding operational firmware here does not complete the firmware-load task.

The mergeable PR contains this report, captures, checklist update and the resource
comment. The HCI consumer remains separately published and unmerged. No public
ABI, dependency pin or compiler/container input changed; no container rebuild
is required.
