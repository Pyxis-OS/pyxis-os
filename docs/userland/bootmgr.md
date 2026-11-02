# Boot manager

`bootmgr` is the Pyxis equivalent of `efibootmgr`: it lists and edits the
firmware's UEFI boot variables. It is a package in `base` and works on systems
that boot through UEFI with runtime variable services; elsewhere it reports that
the variables are unavailable.

## Authority

The kernel's typed `uefi` resource has two rights. `UEFI_RIGHT_BOOT_REGISTER`
is the installer's: it registers the one Pyxis OS boot option.
`UEFI_RIGHT_BOOT_MANAGE` covers the operations below. A space receives the
second only when its configuration sets
[`boot_manager`](init.md#boot-configuration); installed `pyxis` does, and the
session passes it to the local interactive shell, not to remote shells. A
program holds it only if its manifest declares the `uefi` resource with the
`manage` right. There are no other safeguards: the holder can reorder or delete
any entry, including the firmware's own and Pyxis's.

## Kernel operations

`include/abi/uefi.h` is the contract. `GET`, `SET` and `DELETE` act on one raw
variable, named by kind (`Boot####` with a number, `BootOrder`, `BootNext`,
`Timeout`, and the read-only `BootCurrent`) in the global GUID; values up to
4 KiB; written variables use the standard non-volatile, boot-services and
runtime attributes. `ENTRIES` reports which `Boot####` numbers exist in a
window of 256 as a bitmap, so one call never holds the firmware for long.
`bootmgr` builds and parses load options and `BootOrder` itself.

Writes follow the boot-register rule: success means the kernel read the variable
back and it matched. A write the firmware refused, or one that could not be
confirmed, returns `OUTCOME_UNKNOWN`; the kernel retries nothing, and
`bootmgr` says so and leaves the next step to the user. All firmware access
shares the existing single-entry gate, so a call can return `BUSY`.

## Parsing load options

`uefi_load_option_parse`, `uefi_load_option_label`, `uefi_path_next` and
`uefi_path_hard_drive` in `userspace/lib/uefi.c` (declared in `uefi.h`) are the
one parser for `Boot####` values and device paths: attributes, description,
path nodes, and the media hard-drive node (partition number, first LBA, size,
GPT partition GUID or MBR signature). `bootmgr` uses them; so should anything
that identifies the boot device from `BootCurrent`.

## Commands

```text
bootmgr                          BootCurrent, BootNext, Timeout, BootOrder and every entry
bootmgr next 0003                boot entry 0003 once, on the next restart
bootmgr next --clear
bootmgr order 0003,0001          set BootOrder (hexadecimal, like Boot####)
bootmgr timeout 5 | --clear
bootmgr create Fedora --like 0003 --file /EFI/fedora/shimx64.efi [--inactive] [--first]
bootmgr edit 0003 [--label NAME] [--file PATH] [--active | --inactive]
bootmgr delete 0003
```

After `bootmgr next N`, run `reboot` to start that entry once.

## Limits

- `create` copies the device path of the `--like` entry and replaces its file
  path. The program has no GPT or disk view, so it cannot describe a partition
  the firmware does not already name. `--file` takes `/` for `\`.
- Only load options with a plain device path and description are rebuilt by
  `edit` and `create`; optional data is kept by `edit` and not created.
- Listing scans all 65,536 `Boot####` numbers in 256 calls to find entries
  outside `BootOrder`.
- Driver, SysPrep and other non-boot variables are out of scope.
- Checked in QEMU with OVMF only; native firmware has not been checked.
