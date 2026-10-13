# FAT32 bound mount evidence qualification

## Inputs and scope

Owner-assigned addition, 2026-10-13: expose retained GPT identity and whole-device
geometry through the existing bound `MOUNT_OBSERVE` authority. No new operation,
mount selector, right or mutable state was added. The implementation is in
`8145fa29`; the userland helper is `e3c6ff9`. This record covers disposable QEMU
media, not the owner's ESP or authenticated hardware identity.

The full default image was built from Pyxis `a2619a62` plus that implementation
with userland `e3c6ff9`, fs `414be0ac`, ports `4ed452df` and lwIP `a1aadb91`.
Builder: `pyxis-llvm23.1.3-4c23a20`; command:

```sh
make -j8 image BOOT_MENU_TIMEOUT=5
```

The qualified kernel SHA-256 was
`699d05bfc8cbbdf2b517bf2dc809508293be6e83f8ccf79a3a7ff8f2663c63b2`.
The branch was subsequently rebased onto `e8b2c02d`; the FAT implementation and
helper remained unchanged. No compiler-container rebuild or ports change.

## Interactive QEMU and debugger results

QEMU 10.2.2, q35/KVM, host CPU, four vCPUs, 1 GiB, OVMF, modern VirtIO block,
framebuffer input through the monitor. The scratch disk had 274,432 512-byte
blocks. Entry 1 was a 131,072-block FAT32 partition at block 2,048; a disjoint
npfs partition remained present. GPT attribute bit 2 was set on the FAT entry
using `sgdisk --attributes=1:set:2`, exercising a nonzero retained attribute.
`sgdisk -v` accepted both GPT copies.

The existing `init-fat32-mount.pxe boot.install=1` browser selected the displayed
disk and entry explicitly, with the disk attached read-only. At `fat32-mount>`:
`state`, `close`, `retire`, `state`. The helper accepted both 680-byte replies.
Active flags were `0x106`; retired flags were `0x107`. Domain, device, entry,
extent and all 72 added evidence bytes were identical across retirement.

The existing scratch writer (`init-fat32-mount-write.pxe boot.install=1`) selected
the same disposable disk attached writable. `state`, `create /EVIDENCE.TXT`,
`state`, `close`, `retire`, `state` all succeeded. Flags were `0x10e`, `0x11c`,
then `0x10f`. Identity and geometry remained byte-for-byte identical across the
mutation and retirement. A host read-only `fsck.fat -n` on the extracted FAT
partition passed (six files, six allocated clusters).

A separate GPT/FAT32 boot image held the newly built kernel/archive and Limine,
and booted directly through VirtIO with no CD. Its single ESP occupied 262,144
blocks at block 2,048 on a 274,432-block device. Boot command:
`init=boot://init-fat32-mount.pxe`, without install mode. The actual boot-origin
issuer admitted only the RO view; no injected provenance or raw inventory was
used. `state`, `cat /boot/limine/limine.conf`, `close`, `retire`, `state` succeeded.
Active/retired flags were `0x106`/`0x107`; all evidence was retained. The whole
image's SHA-256 was unchanged after the read-only run.

For each observed state, read-only GDB inspection stopped at `copy_to_user`
with `bytes == 680` and captured its source record before the normal copy:

```gdb
set may-call-functions off
hbreak copy_to_user if bytes == 680
continue
dump binary memory /tmp/mount-state.bin source (char *)source+680
detach
```

An independent host byte comparison checked the disk GUID against GPT header
bytes 56–71, the partition GUID/type/attributes against the selected GPT entry,
and the extent against its first/last LBAs. Device block count matched raw-image
length divided by 512, including space outside the selected partition. Every
comparison passed, and each reply set `MOUNT_STATE_IDENTITY_VALID`. Private media
GUID values and local selection configuration are deliberately not recorded.

## Inspected limits

Admission already validates both GPT copies and compares its fresh engine facts
with the selected inventory entry before publishing any control. OBSERVE uses
the immutable admitted volume; it neither re-probes media nor authenticates it.
Failure and retirement retain that volume. Failure-state evidence lifetime was
verified from the stopped-mount observation path; no mutation-failure injection
was performed in these runs.

The helper rejects malformed flagged evidence or reply framing with
`CALL_OUTCOME_UNKNOWN`, preserving the caller's output. Checks are subtraction
safe and permit nonzero attributes and unknown state flags. This validation was
reviewed against the existing GPT/FAT admission profile; no forged-provider
runtime test was added. The reply and embedded job record grow by 72 bytes;
existing queue/lifetime bounds remain unchanged, with no extra allocation or
media I/O operation introduced by OBSERVE. No timing claim is made.

All qualification QEMU/debugger jobs were stopped. Native owner-present ESP
qualification remains separately pending in the
[trusted workflow run sheet](../../devices/fat32-volumes.md#owner-present-native-esp-run-sheet).
