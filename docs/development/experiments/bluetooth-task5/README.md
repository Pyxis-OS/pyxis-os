# First LE scan of the MX Master 3S

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Task 5 completed on 2026-10-08. With the owner's mouse in pairing mode, Pyxis's unmerged probe received an
advertisement with HID service `0x1812` and mouse appearance `0x03C2`, then a correlated scan response with
complete name `MX Master 3S`. The accepted observation window finished successfully and the controller confirmed
scanning disabled. No Bluetooth device address was logged or published. This is the bounded scan-result report;
it implemented no connection, pairing, bond storage, GATT or pointer delivery.

## Revisions, build and configuration

Branched from main `9acf597fec897119256a0c0044785771b19f9132`, including
[merged #528](https://git.internal/PyxisOS/pyxis-os/pulls/528), with SDK/userland/ports bundles from successful
[CI run 1232](https://git.internal/PyxisOS/pyxis-os/actions/runs/1232) that passed the existing verifiers. The
published [probe revision](https://git.internal/PyxisOS/pyxis-os/commit/3d0bc3c8aa79e1ede4ff8d366e759d8064eb3cf8)
stays on `probe/bluetooth-le-scan`, which must not be merged. It was re-authored and signed on 2026-10-08, and
that commit has exactly the same tree as the measured commit `6c4329e` (tree
`46b500e411d24ab0b4d9b95d086ed0fb7093279e`), so the recorded ELF and GDB observations still correspond to it
without a rebuild or a repeat radio run. A later name-redaction-only
[revision](https://git.internal/PyxisOS/pyxis-os/commit/9f12592e0098224d3cf2149d308d72393dbedb0e) adding
variable-width address formats received source review and an ordinary build but no radio run (the observed ASCII
mouse names follow the same rendering path by inspection); the observations below use the measured image.

The image was built from the clean measured revision with
`make -j16 image PREBUILT="sdk userspace ports"` and no compiler warnings. The local Clang/LLD 23.1.3 uses fork
`41ab6043cc4fd63e0a358d1e60bba249a751d8ee` while the CI bundles use `49e2c1a1518b3e4687b52ceb6001069c1b6d261e`
(the #528 reviewer's toolchain note: the kernel probe is C-only and compiled no C++). Pins: userspace
`df78002764768b05b2843248d1e1f4cf6091d695`, ports `a3b154b9d627d8ee389bfefce35a87346adf5a0b`, filesystem
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP `a1aadb91a50360ff5b52864f7cec810b8162ee85`.

The physical ThinkPad ran Fedora/KVM (not nested) with QEMU `10.2.2-1.fc44`: Q35, four CPUs, 2 GiB, the Fedora
raw OVMF pair with fresh variables, ISO boot, display disabled, VirtIO RNG and networking, and AX200
`8087:0029` passthrough (`-device usb-host,bus=xhci.0,port=1,vendorid=0x8087,productid=0x0029` on
`qemu-xhci`) with a GDB port. Bluetooth stayed inactive/disabled, rfkill was unblocked, the invoking user had
device-node access, and the owner indicated pairing-mode readiness before boot. The baseline CI image and the
post-scan guest both returned complete AX200 inventory from `lsusb -n` (exit 0, complete remote drain); one
connect/inventory/exit workload per image measured 0.05 s at a 0.01 s timer, which establishes no performance
result.

## Accepted scan profile and implementation

The owner accepted the [profile](../../../devices/ax200-bluetooth.md#accepted-task-5-scan-profile) before code:
legacy 1M active scanning, 30 seconds after confirmed enable, 100 ms interval and window, controller duplicate
filtering enabled and no accept-list filter. The controller's public scanner address is used over the air, there
is no random/privacy-address configuration, and addresses stay out of captures.

The private endpoint is prepared on its BSP worker during enumeration and the same worker runs the bounded probe
afterward, so the scan does not consume the enumeration deadline. Preparation reuses the
[verified warm-firmware policy](../bluetooth-task4/README.md) (accept the observed operational build, confirm
its version unchanged, no upload). Each HCI command has its own five-second deadline; commands configure the base
event mask, LE Advertising Report mask, scan parameters, enable and disable (fields per the
[HCI reference](https://github.com/torvalds/linux/blob/v6.18/include/net/bluetooth/hci.h)), and advertising
reports and scan responses arriving during the enable/disable completion waits are consumed there too.

The consumer validates full HCI lengths, consecutive receive sequences, command credits and matching
status/opcode, and checks every variable-count advertising batch for complete record and AD field bounds before
exposing candidate fields. It parses 16-bit UUID lists and service data, shortened and complete names (complete
wins) and appearance. Advertisements and scan responses correlate by address and type only in RAM within 64
advertiser records; table exhaustion, malformed data or a stream discontinuity aborts identification. Candidate
output holds only the parsed name, name kind, HID/appearance evidence, source-type flags and RSSI under run-local
candidate numbers (not persistent identifiers); addresses are replaced before output, raw packets and the
address-keyed aggregate are never dumped, and names are escaped with address-shaped names (including dotted
formats) redacted.

After a possibly submitted enable, every exit attempts disable with a fresh deadline. A terminal stream failure can
prevent reply collection, and an unresolved enable can leave an ambiguous reply with disable's opcode; both stay
unconfirmed and the probe cannot claim cleanup success. Independent review found and corrected that ambiguity and
the dotted-name redaction before the run, and a targeted re-review found nothing remaining.

## Measured identification and cleanup

The serial capture recorded successful status for all eight commands: Reset `0x0c03`, Read Version `0xfc05`
twice, Set Event Mask `0x0c01`, LE Set Event Mask `0x2001`, LE Set Scan Parameters `0x200b`, and LE Set Scan
Enable `0x200c` once for enable and once for disable. Firmware stayed operational, variant `0x23`, build
193/week 33/year 24.

| Observation for anonymous candidate 4 | Advertisement | After scan response |
| --- | --- | --- |
| name | `MX Master` | `MX Master 3S` |
| AD name kind | shortened, `0x08` | complete, `0x09` |
| decoded HID service `0x1812` | present | retained |
| mouse appearance `0x03C2` | present | retained |
| source-type bits | `0x01` | `0x11` |
| RSSI | -37 dBm | -37 dBm |

Source bit 0 records the advertisement and bit 4 its scan response. The complete name combined with HID/mouse
evidence satisfied the accepted criterion; the shortened name alone was not an identification.

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

Post-scan GDB used only address-free scalar expressions and independently confirmed those counters and flags,
sequence 38, an empty copied queue, both receives `INTERRUPT_POSTED`, stream `USB_OK` and controller running.
No target address, raw advertising packet or address-keyed debugger state was captured; the unrelated VirtIO NIC
address in the generic boot log was redacted too, and review found no address-shaped identifier in the retained
output.

## Limits and delivery

This qualifies one prepared-host passthrough scan and confirmed disable. Controller duplicate filtering reduces
reports without proving freshness or losslessness, 19 address/type records do not establish a count of physical
devices, and a largest event of 45 bytes does not qualify multi-packet HCI transfers. Native periodic hardware,
extended scanning and cold upload are outside this result. Malformed input, table or FIFO overflow, terminal
failure and ambiguous cleanup were source-reviewed, not forced. The callback occupies its controller worker for
the bounded scan: this is an unmerged experiment, not a runtime Bluetooth service or a storage-concurrency
qualification, with no public ABI, report UI, connection, pairing or GATT operation. All scan code stays on the
published branch; the mergeable task branch holds only this report and the updated checklist and profile, and the
ordinary image has no scan-probe symbols. QEMU and GDB exited and `btusb` rebound on both interfaces; pins and
compiler/container inputs were unchanged.
