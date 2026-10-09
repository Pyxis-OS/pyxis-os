# npfs disk-full and retained-open deletion qualification

Owner-assigned follow-ups from 2026-10-04. Native ThinkPad installation remains deferred
until writable USB storage exists. Target images were regular files on tmpfs, never host
block devices, and the protected task-3 experiment records are unchanged.

## Expectations recorded before running

The [adapter contract](../../../devices/filesystem-native-adapter.md#writeback-recovery-and-errors)
specifies delayed allocation at writeback: a cached write or close can succeed before
disk-full is reported by a later write, namespace mutation or sync. Recoverable NO_SPACE
must retain dirty data without terminally failing the pool, and the 30-second worker can
report a background failure while preserving it. Sync acknowledges a retained recoverable
error when reporting it; after space is freed and the remaining dirty data becomes durable,
a later sync must succeed. Stopped-image fsck and surviving-file comparisons must pass.

Remove persists DETACHED using the last durable inode record and does not discard contents
owned by an open reference: the name disappears while the retained handle reads the complete
original contents, and cleanup waits for the final close before reclaiming mappings. A
synchronized deletion followed by an unclean QEMU stop with the handle open must leave
durable cleanup membership that a later writable mount reclaims; host fsck checks consistency
but does not reclaim cleanup lists. No policy change, tests, fault injection or automation
was planned. Ordinary shell redirection supplies retained handles, and the guest Lua has no
io/os library. Allocation pressure and arbitrary storage failure stay listed as gaps unless
observed.

## Build and configuration

Main `9cbb0d3` (userland `d730e4f`, filesystem `d352c7e`, ports `bf7667c`, lwIP `a1aadb9`),
built with `make -j16 image fs-tools PREBUILT="sdk userspace ports"` and Pyxis GCC 16.2.0,
using the clean SDK from parent `4439ffb` (no ABI/export/runtime input differed). No
compiler-container rebuild. The image contained this temporary, uncommitted trusted init:

```text
#!app://shell.pxe
mount --partition 1 --volume bench --read-write data://
namespace create
service start text app://textfs.pxe
session app://session.pxe --configure-network --start-remote-services
```

`INIT_PRIMARY=app://init-idle INIT_CPUS=3=app://init` put the workload and server on CPU 3
and `MOUNT_DISK` was `12345678-1234-4567-89ab-0123456789ab`. QEMU 10.2.2 with upstream fix
`d9f78431d8eb` (cancel in-flight buffered reads on AHCI command-engine restart; see the
[AHCI crash reference](../../qemu.md#ahci-cd-rom-crash-before-kernel-entry)): q35, nested
KVM, `-cpu max`, four CPUs, 256 MiB, writable VirtIO block with `cache=writeback`, VirtIO net
and RNG, fresh OVMF variables per boot, xHCI disabled, 30-second flush interval. Each new
64 MiB pool had a 4 MiB journal and volume `bench`, in a 68 MiB GPT disk at sector 2049 on
512-byte media, prepared with `mkfs.npfs --size 64MiB --journal 4MiB --volume bench`,
`fsck.npfs`, `sgdisk --clear --set-alignment=1 --new=1:2049:+64M
--disk-guid=12345678-1234-4567-89ab-0123456789ab` and `dd … bs=512 seek=2049 conv=notrunc`.
The remote client submitted commands individually and recorded typed completions; B2 used
separate file names and two remote clients on one listener. Free blocks, references and
cleanup membership are internal read-only GDB observations
(`opened_pools->free_blocks`, `->failed`, `->writeback_error`,
`->volumes->record.cleanup_head` and its inode list), not FILESYSTEM_INFO fields; no
inferior calls or state changes were injected. After QEMU stopped, the partition was
extracted with `dd bs=512 skip=2049 count=131072 conv=sparse` and checked with `fsck.npfs`,
`npfs-inspect list` and `extract --volume bench --path … --output …`, compared with `cmp`.

## B1: full pool and recovery

Seven `cat` commands created `fill-0.bin` through `fill-6.bin` (eight copies of
`app://share/iobench.bin`, 8 MiB each); all and `sync data://` returned zero. Inspection found
976 free blocks, `failed=false`, no retained error and an EMPTY journal. The final copy,
`cat` of four fixtures to `data://fill-7.bin`, returned zero from cache, but `sync data://`
printed `sync: data://: No space left on device` (exit 1). The pool then had 77 free blocks,
`failed=false`, EMPTY journal and no retained error after that acknowledgment, since earlier
batches had committed before the last ran out of space. Another 32 seconds later the serial log
showed `npfs: background writeback/cleanup failed (status 24); pool retained`, and inspection
showed retained `CALL_NO_SPACE` with `failed=false`.

Then `rm data://fill-0.bin` succeeded; the first `sync data://` reported the retained
NO_SPACE once (exit 1) with 2,002 free blocks and `CALL_OK`; the second sync, a new 1 MiB copy
and its sync succeeded. Final inspection: 1,745 free blocks, no cleanup membership, EMPTY
journal, no failed latch; the remote shell exited normally with complete draining. The
extracted pool passed `fsck.npfs`, `recovered.bin` matched the 1 MiB source, `fill-7.bin`
matched four concatenated copies, the removed file was absent and the other six 8 MiB files and
the 4 MiB file remained. This observes delayed NO_SPACE, periodic failure retention, one-time
reporting and recovery without reboot; it does not exercise an allocator failure or uncertain
backing I/O.

## B2: retained-open deletion

A fresh pool received `big.bin` (one `cat` of 32 fixtures, 32 MiB) and `sync data://`. Two
sessions then ran: the reader `cat < data://big.bin > home://copy.bin` (its stdin grant stays
open through the copy), and while it ran the remover `rm data://big.bin`, `sync data://` and
`cat data://big.bin`. Remove and sync returned zero and the lookup printed `Not found`
(exit 1). Free blocks were exactly 7,138 before and after removal, and inode 2 still had one
reference, size 33,554,432, DETACHED flag 1 and cleanup head 2. The read completed with exit
zero despite the missing name; after its final close inspection showed 15,347 free blocks,
cleanup head zero, zero references and an EMPTY journal: no mappings were reclaimed while the
handle was retained and cleanup reclaimed them after close. `cat home://copy.bin >
data://copy.bin` and its sync succeeded; after normal exits fsck passed and the extracted
32 MiB `copy.bin` matched 32 fixture copies, verifying the complete retained-handle read.

The guest Lua has no io/os library, a nested shell lacked launcher resources and `>>` was
rejected as unsupported; those interfaces were not extended. One long `cat` plus a second
session gave the overlap without new programs or grants.

### Interrupted variant: pending detached cleanup across restart

A fresh pool imported the verified 32 MiB contents as `big.bin`. The reader ran again and the
second session ran `rm` and `sync data://` (both successful). Immediately before a manual QEMU
`quit`, inspection showed 7,138 free blocks, inode 2 referenced once and DETACHED, cleanup head
2 and an EMPTY journal, with the reader unfinished; both clients disconnected without final
acknowledgment, as expected. The stopped pool passed read-only fsck without replay, `volumes`
reported `inode_bytes 768 cleanup_head 2` and the root listing was empty, so persisted cleanup
membership existed before restart and the host checker did not reclaim it. The same disk then
booted with fresh variables and the same init: before any write there were 15,347 free blocks,
cleanup head zero, an EMPTY journal and no failed latch, and `big.bin` stayed NOT_FOUND. A
following `cat` of the fixture to `after-restart.bin` and sync succeeded; the extracted pool
passed fsck with cleanup head zero and only that 1 MiB file, matching the source. This
establishes cleanup after loss of retained references at an unclean restart; the stop occurred
before reclamation while the reader held the inode, and no arbitrary mid-batch or
uncertain-I/O interruption is qualified.

## Results and remaining gaps

Delayed-allocation ENOSPC, background error retention, sync acknowledgment and space recovery,
retained-open unlink with complete byte verification, final-close cleanup and pending detached
cleanup across a writable restart were exercised at runtime. No kernel or application behavior
changed, so no performance measurement is claimed. Allocator pressure or failure, uncertain
backing I/O, arbitrary interruption during a cleanup batch, read-only-device opening and
physical media remain unqualified.
