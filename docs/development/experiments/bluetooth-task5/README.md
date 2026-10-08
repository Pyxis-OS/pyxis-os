# First LE scan of the MX Master 3S

Task 5 completed on 2026-10-08. With the owner's mouse in pairing mode, Pyxis's
unmerged probe received an advertisement with HID service `0x1812` and mouse
appearance `0x03C2`, then a correlated scan response with complete name
`MX Master 3S`. The accepted observation window finished successfully and the
controller confirmed scanning disabled. No Bluetooth device address was logged
or published.

This is the bounded scan-result report. The investigation's final report and
milestone proposal wait for the owner to see these results; this task did not
implement connections, pairing, bond storage, GATT or pointer delivery.

## Revisions, build and configuration

The task branched from freshly fetched main
`9acf597fec897119256a0c0044785771b19f9132`, including
[merged #528](https://git.internal/PyxisOS/pyxis-os/pulls/528).
Matching SDK/userland/ports bundles from successful
[CI run 1232](https://git.internal/PyxisOS/pyxis-os/actions/runs/1232) passed the
existing verifiers. The separately published
[probe revision](https://git.internal/PyxisOS/pyxis-os/commit/3d0bc3c8aa79e1ede4ff8d366e759d8064eb3cf8)
is retained on `probe/bluetooth-le-scan`; that branch must not be merged.
After measurement, a name-redaction-only
[follow-up revision](https://git.internal/PyxisOS/pyxis-os/commit/9f12592e0098224d3cf2149d308d72393dbedb0e)
added variable-width address formats. It received source review and an ordinary
build, without another radio run; the observed ASCII mouse names follow the
same rendering path by inspection. The captures below use the measured revision
and its matching ELF, not that later image.

The probe history was re-authored and signed as Codex on 2026-10-08. The rewritten
radio-tested commit `3d0bc3c8aa79e1ede4ff8d366e759d8064eb3cf8` has exactly the same tree
as the measured commit `6c4329e` (tree `46b500e411d24ab0b4d9b95d086ed0fb7093279e`).
All rewritten probe trees and commit messages were verified unchanged. The
recorded ELF and GDB capture still correspond to that identical measured source;
the ELF retains its original build-revision metadata. This identity/signature
repair did not rebuild the binary or repeat the radio run.

The probe image was originally built from the clean measured revision:

```sh
PATH="$HOME/opt/pyxis-llvm/bin:$PATH" make -j16 image PREBUILT="sdk userspace ports"
```

The build passed without compiler warnings. Local Clang/LLD 23.1.3 uses fork
`41ab6043cc4fd63e0a358d1e60bba249a751d8ee`; the current CI bundles use fork
`49e2c1a1518b3e4687b52ceb6001069c1b6d261e`. This records the #528 reviewer's
toolchain note: the kernel probe is C-only and did not compile C++ inputs.
Dependency pins matched the selected main:

| Repository | Revision |
| --- | --- |
| userspace | `df78002764768b05b2843248d1e1f4cf6091d695` |
| ports | `a3b154b9d627d8ee389bfefce35a87346adf5a0b` |
| filesystem | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |

The physical ThinkPad ran Fedora/KVM, not nested virtualization, with QEMU
`10.2.2-1.fc44`, four CPUs and 2 GiB RAM. It used Q35, the Fedora raw OVMF pair
with fresh variables, ISO boot, standard VGA with display disabled, VirtIO RNG,
VirtIO networking and AX200 `8087:0029` passthrough. The command was:

```text
qemu-system-x86_64
  -machine q35 -accel kvm -cpu max -rtc base=utc -smp 4 -m 2G
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd
  -drive if=pflash,format=raw,unit=1,file=FRESH_VARS_COPY
  -cdrom PROBE_ISO -boot d -display none -serial file:SERIAL_CAPTURE -monitor stdio
  -gdb tcp:127.0.0.1:1234
  -netdev user,id=net,hostfwd=tcp:127.0.0.1:2323-10.0.2.15:2323
  -device virtio-net-pci,netdev=net,disable-legacy=on
  -object rng-random,id=rng,filename=/dev/urandom
  -device virtio-rng-pci,rng=rng,disable-legacy=on
  -device qemu-xhci,id=xhci
  -device usb-host,id=bluetooth,bus=xhci.0,port=1,vendorid=0x8087,productid=0x0029
```

Bluetooth stayed inactive/disabled, rfkill was unblocked, and the invoking user
had device-node access. The owner indicated pairing-mode readiness before boot.
The baseline CI image and post-scan guest both returned complete AX200 inventory
from `lsusb -n`, exit status 0 and complete remote final drain. One ordinary
connect/inventory/exit workload per image measured 0.05 seconds with a
0.01-second host timer. These single samples establish no performance result.

## Accepted scan profile and implementation

The owner accepted the
[profile](../../../devices/bluetooth-investigation.md#accepted-task-5-scan-profile) before code:
legacy 1M active scanning, 30 seconds after confirmed enable, 100 ms interval
and window, controller duplicate filtering enabled and no accept-list filter.
The controller's existing public scanner address is used over the air; addresses
remain out of captures. There is no random/privacy-address configuration.

The private endpoint is prepared on its BSP worker during enumeration. The
same worker runs the bounded probe afterward, so the scan does not consume the
enumeration deadline. Its preparation reuses the
[verified warm-firmware policy](../bluetooth-task4/README.md); it accepts the
observed operational build, confirms its version unchanged and does no upload.
Each HCI command has its own five-second deadline. Commands configure the base
event mask, LE Advertising Report mask, scan parameters, enable and disable.
The [HCI reference](https://github.com/torvalds/linux/blob/v6.18/include/net/bluetooth/hci.h)
defines their fields; advertising reports and scan responses can arrive during
the enable/disable command-completion waits and are consumed there too.

The consumer validates full HCI lengths, consecutive receive sequences, command
credits and matching status/opcode. Every variable-count advertising batch is
checked for complete record bounds and AD field lengths before candidate fields
are exposed. It parses 16-bit UUID lists/service data, shortened/complete names
and appearance. Complete names take precedence over shortened names.
Advertisements and scan responses correlate by address/type only in RAM within
64 advertiser records; table exhaustion, malformed data or stream discontinuity
aborts identification.

Candidate output contains only parsed name, name kind, HID/appearance evidence,
source-type flags and RSSI. Addresses are replaced before output; raw packets and
the address-keyed state aggregate are never dumped. Name bytes are escaped for
logging and address-shaped names are redacted, including dotted formats. These
run-local candidate numbers are not persistent device identifiers.

After a potentially submitted enable, every exit attempts disable using a fresh
command deadline. A terminal stream failure can prevent reply collection.
Likewise an unresolved enable can leave an ambiguous reply with disable's same
opcode. Both cases remain unconfirmed; the probe cannot claim cleanup success.
Independent source review found and corrected that ambiguity and dotted-name
redaction before this run; targeted re-review found no remaining findings.

## Measured identification and cleanup

The [serial capture](serial.txt) records successful status for all eight commands:
Reset `0x0c03`, Read Version `0xfc05` twice, Set Event Mask `0x0c01`, LE Set
Event Mask `0x2001`, LE Set Scan Parameters `0x200b`, and LE Set Scan Enable
`0x200c` once for enable and once for disable. Firmware remained operational,
variant `0x23`, build 193/week 33/year 24.

| Observation for anonymous candidate 4 | Advertisement | After scan response |
| --- | --- | --- |
| name | `MX Master` | `MX Master 3S` |
| AD name kind | shortened, `0x08` | complete, `0x09` |
| decoded HID service `0x1812` | present | retained |
| mouse appearance `0x03C2` | present | retained |
| source-type bits | `0x01` | `0x11` |
| RSSI | -37 dBm | -37 dBm |

Source bit 0 records the advertisement; bit 4 records its scan response. The
complete name combined with HID/mouse evidence satisfied the accepted criterion.
The shortened name alone was not an identification.

| Final counter/state | Observed |
| --- | --- |
| collected HCI events | 38 |
| decoded advertising/scan-response records | 30 |
| run-local advertiser records | 19 |
| scan-response records | 11 |
| largest collected event | 45 bytes |
| identified candidates | 1 |
| enable accounted and confirmed | true |
| disable confirmed | true, status/result 0 |
| final probe and scan result | `USB_OK` |

Post-scan [GDB inspection](gdb.txt) used only address-free scalar expressions.
It independently confirmed those counters and flags, sequence 38, empty copied
queue, both receives `INTERRUPT_POSTED`, stream `USB_OK` and controller running.
No target address, raw advertising packet or address-keyed debugger state was
captured. The unrelated VirtIO NIC address in the generic boot log was also
redacted in the retained serial file. File review then found no address-shaped
identifier in the retained output.

## Limits and delivery

This qualifies one prepared-host passthrough scan and confirmed disable. Controller
duplicate filtering reduces reports without proving freshness or losslessness;
19 address/type records do not establish a count of physical devices. Largest
event size 45 does not qualify multi-packet HCI transfers. Native periodic
hardware, extended scanning and cold upload remain outside this result. Malformed
input, table/FIFO overflow, terminal failure and ambiguous/unconfirmed cleanup
received source review; they were not forced in the guest.

The callback occupies its controller worker for the bounded scan; this is an
unmerged experiment, not a runtime Bluetooth service or a storage-concurrency
qualification. It exposes no public ABI or report UI and performs no connections,
pairing or GATT operations. All scan code stays on the separately published branch.

The mergeable task branch contains only this report, redacted captures and the
updated checklist/profile. The ordinary image was restored and has no scan-probe
symbols. QEMU/GDB exited and `btusb` rebound on both interfaces. Dependency pins
and compiler/container inputs did not change; no container rebuild is required.
The final investigation report and milestone proposal remain pending owner review
of these scan results.
