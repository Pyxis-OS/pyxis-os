# FAT32 raw-write qualification

## Inputs and baseline

Before implementation, build parent `9605041e` (only the accepted-contract doc
above main `5dc08c5b`), fs `9b4479e`, userland `2ccb58e`, ports `28b5a3bc` and
lwIP `a1aadb91`. Runtime inputs match that main revision. Native qualification
uses fs `28386cc79ed3` and userland `a5d36c1b4036`, rebased onto userland main
`20fb34f`; parent integration `543c3f75` contains main `1536d997` (#745).
The intervening native-command core is outside this task. Installer ESP sources,
read-only bootstrap and native inspector sources retain their baseline hashes.
Before submission, rebase onto parent main `ae0bc1df` and userland main
`bdef26b`, pinning userland `5fa4f42291a9`. Their intervening changes affect HTTP
and its documentation. A focused rebuild produces byte-identical scratch-writer
and bootstrap programs; the FAT32 engine, runtime and kernel inputs are unchanged.

Build with `make -j16 sdk`, then `make -j16 image BOOT_MENU_TIMEOUT=5` in the
existing `pyxis-llvm23.1.3-49e2c1a` builder. Explicit SDK regeneration precedes
final image qualification; no stale library is substituted. Host builds use
`make -C fs -j16`. No compiler container, CI workflow, test framework or
fault-injection interface is added.

The disposable disk is 70 MiB GPT, entry 1 at sector 2048, with a 64 MiB FAT32
partition: 512-byte sectors/clusters, two mirrored FATs, 129022 clusters.
Prepare it with `sgdisk`, `mkfs.fat -F32 -S512 -s1 -f2 -h2048`, `mmd` and
`mcopy`. Its original check reports two files, three directories and five
allocated clusters. Existing adversarial images are copied before mutation.

## Matched reader costs and host mutations

[Shared-engine qualification](../../../fs/docs/fat32-write-qualification.md)
records seven cached, unprofiled before/after invocations of `fat32-inspect`
`check`, `list /` and `read /DIR/CHILD/HELLO.TXT`, including process startup.
Median milliseconds: check 1.661 → 1.401, list 0.879 → 0.541, read 0.952 → 0.955.
Ranges overlap substantially; no reader regression or writer speedup is
established. These Linux VM samples do not measure physical storage. There was
no existing incremental raw-writer operation to compare.

Host create/append/zero-extension/replacement, empty files, directory growth,
255-byte names and one/two-FAT layouts cross-read with mtools and pass
`fsck.fat -n`. Restarted receipts, collisions, allocation-full and malformed
volumes refuse before effects. Permission-denied opening leaves the image intact.
A private 256 KiB tmpfs produces real ENOSPC during extension: the writer stops
in the data phase, retains allocation/prepared-sector recovery information,
skips queued commands and preserves the committed old file. No shared mount is
exhausted. Short writes and flush failures are not separately induced.

## Interactive native qualification

QEMU 10.2.2, q35/KVM, host CPU, four CPUs, 1 GiB RAM, OVMF, VirtIO SCSI CD,
modern VirtIO block (`disable-legacy=on`), disabled unused audio, native console
and GDB. Both baseline and final read-only profiles pass the original volume
check. Scratch writing uses a separate writable copy and boot entry.

Select disk 1 / GPT 1, then run:

```text
mkdir /native
create /native/hello.txt = hello
append 2 =  world
extend 2 1030
replace 2 = replacement
check
read /native/hello.txt
receipts
quit
```

All mutations report durable success. A quiescent snapshot after extension
contains exactly `hello world` plus 1019 zero bytes. Replacement reads exactly
`replacement`; final check reports three files, four directories and seven
allocated clusters. Extract with `dd bs=512 skip=2048 count=131072`, then
`fsck.fat -n`, `mdir` and `mtype` independently agree. Bytes outside the partition
and both FAT header/clean-error fields are unchanged.

GDB observes the native claim at partition 1 / first 2048 / 131072 blocks,
child disk rights 23 (INFO|READ|WRITE|RELEASE), two startup resources and no
roots/namespace/cwd. Before effects, the directory-create plan has completed
whole-volume preflight; caller workspace is 2694224 bytes. Returning from the
effect runner yields FAT32_OK. After quit, both claim and retired-disk lists
are empty. Observed PMM allocation falls from 5542 to 3891 pages; these are
global snapshots, not a measured peak or private process residency.

Rebooting loses receipts: `append 2 = denied` refuses with an unchanged full-image
hash. A separate writable hidden-entry-after-end-marker image refuses creation
as corrupt, keeps its full-image hash, and releases its claim on quit. Its idle
PMM snapshot also has 3891 allocated pages. No stuck claim or retained private
backing is observed. QEMU/debugger jobs are stopped after qualification.

This is nested-VM evidence. No native ESP/USB/NVMe writes, power-cut recovery,
torn sectors, uncertain mirrored-FAT/directory publication or external repair
are qualified. Flush success, failure-latch semantics and retained recovery do
not promise rollback or crash atomicity.
