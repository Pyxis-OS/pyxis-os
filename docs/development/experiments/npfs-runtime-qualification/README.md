# npfs disk-full and retained-open deletion qualification

Owner-assigned follow-ups from `/shared/pyxis-fs-next-tasks.md`, 2026-10-04.
Native ThinkPad installation remains deferred until writable USB storage exists.
Target images are regular files on tmpfs, never host block devices. The protected
task-3 experiment records are unchanged.

## Expectations recorded before running

The [adapter contract](../../../devices/filesystem-native-adapter.md#writeback-recovery-and-errors)
specifies delayed allocation at writeback. A cached write or close can succeed
before disk-full is reported by a later write, namespace mutation or sync.
Recoverable NO_SPACE must retain dirty data without terminally failing the pool.
The 30-second worker can report background failure while preserving that data.
Sync acknowledges a retained recoverable error when reporting it; after space
is freed and all remaining dirty data becomes durable, a later sync must succeed.
Stopped-image structural checking and surviving-file comparisons must pass.

Remove persists DETACHED using the last durable inode record and does not discard
contents still owned by an open reference. Removing an open file must hide its
name while the retained handle can still read the complete original contents.
Detached cleanup must wait for the final reference to close before reclaiming
its mappings. A synchronized deletion followed by an unclean QEMU stop while the
handle remains open must leave durable cleanup membership; a later writable
mount must reclaim it after those references have disappeared. Host fsck checks
consistency but does not reclaim cleanup lists itself.

No changed policy, new tests, self-tests, fault injection, native validation program or
boot/output automation is planned. Ordinary shell redirection supplies retained
file handles; the existing remote client and debugger permit manual interaction
and inspection. The guest Lua has no io/os library, so the suggested Lua file
example is unavailable without an unrelated port change. Allocation pressure and
arbitrary storage failures must remain listed as gaps unless actually observed.

## Build and configuration

Main `9cbb0d3` pins userland `d730e4f`, filesystem `d352c7e`, ports `bf7667c`
and lwIP `a1aadb9`. The kernel and host tools were built with `make -j16 image
fs-tools PREBUILT="sdk userspace ports"`, Pyxis GCC 16.2.0 and the existing Lua
host tools. Verified application/ports products use the clean SDK from parent
`4439ffb` and the same pinned userland/filesystem. No ABI/export/runtime inputs
changed between that SDK build and this main. No compiler-container rebuild was
needed. The ordinary image contains this temporary, uncommitted trusted init:

```text
#!app://shell.pxe
mount --partition 1 --volume bench --read-write data://
namespace create
service start text app://textfs.pxe
session app://session.pxe --configure-network --start-remote-services
```

`INIT_PRIMARY=app://init-idle INIT_CPUS=3=app://init` places the workload/server
on CPU 3 and leaves CPUs 1 and 2 idle. `MOUNT_DISK` is
`12345678-1234-4567-89ab-0123456789ab`. QEMU 10.2.2 with the existing AHCI fix
uses q35, nested KVM, `-cpu max`, four CPUs, 256 MiB RAM, modern writable VirtIO
block with `cache=writeback`, VirtIO net and RNG, and OVMF code/variables from
`/usr/share/edk2/ovmf`. Fresh variables are used per boot. Native xHCI is disabled
and the flush interval remains 30 seconds. The live ISO, variables, standalone
pools and GPT target disks are all regular files under `/dev/shm/pyxis-npfs-next`.
Each new 64 MiB pool has a 4 MiB journal and volume `bench`, placed in a 68 MiB
GPT disk at sector 2049 on 512-byte media. Host tools operate only after QEMU
stops. The existing remote client submits commands individually and records
typed completion events; no new driver, harness or validation program is added.

Image preparation for B1 (B2 uses separate filenames; its interrupted variant
imports the verified 32 MiB file with `--source`):

```sh
build/fs-tools/mkfs.npfs --image /dev/shm/pyxis-npfs-next/pool-b1.raw --size 64MiB --journal 4MiB --volume bench
build/fs-tools/fsck.npfs --image /dev/shm/pyxis-npfs-next/pool-b1.raw
truncate -s 68M /dev/shm/pyxis-npfs-next/disk-b1.raw
sgdisk --clear --set-alignment=1 --new=1:2049:+64M --disk-guid=12345678-1234-4567-89ab-0123456789ab /dev/shm/pyxis-npfs-next/disk-b1.raw
dd if=/dev/shm/pyxis-npfs-next/pool-b1.raw of=/dev/shm/pyxis-npfs-next/disk-b1.raw bs=512 seek=2049 conv=notrunc status=none
cp build/pyxis.iso /dev/shm/pyxis-npfs-next/live.iso
cp /usr/share/edk2/ovmf/OVMF_VARS.fd /dev/shm/pyxis-npfs-next/vars-b1.fd
```

The manual B1 launch was:

```sh
/tmp/pyxis-qemu-ahci-fix/build/qemu-system-x86_64 \
  -enable-kvm -machine q35 -cpu max -smp 4,sockets=1,cores=4,threads=1 -m 256M \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=/dev/shm/pyxis-npfs-next/vars-b1.fd \
  -drive if=none,id=live,media=cdrom,format=raw,readonly=on,file=/dev/shm/pyxis-npfs-next/live.iso \
  -device ide-cd,drive=live,bootindex=1 \
  -drive if=none,id=target,format=raw,cache=writeback,file=/dev/shm/pyxis-npfs-next/disk-b1.raw \
  -device virtio-blk-pci,drive=target,disable-legacy=on \
  -device virtio-net-pci,netdev=net0,disable-legacy=on \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2333-:2323 \
  -device virtio-rng-pci,disable-legacy=on -display none \
  -serial file:build/npfs-next/b1-serial.txt -monitor stdio -no-reboot \
  -gdb tcp:127.0.0.1:1236
build/tools/pyxis-remote --machine --no-shell-echo --columns 120 --rows 40 127.0.0.1 2333
```

B2 changes only target, variables and serial filenames; two remote clients
connect to the same listener from separate host terminals. Free blocks, references and cleanup membership
are internal read-only GDB observations, not public FILESYSTEM_INFO fields.
For example, attach to `build/caelum.elf`, read `opened_pools->free_blocks`,
`opened_pools->writeback_error`, `opened_pools->failed`,
`opened_pools->volumes->record.cleanup_head` and its `inodes` list, then detach.
No inferior calls or state changes were injected.

```sh
gdb -q -batch build/caelum.elf \
  -ex 'target remote 127.0.0.1:1236' \
  -ex 'p opened_pools->free_blocks' \
  -ex 'p opened_pools->failed' \
  -ex 'p opened_pools->writeback_error' \
  -ex 'p opened_pools->volumes->record.cleanup_head' -ex detach
```

## B1: full pool and recovery

Seven ordinary `cat` commands created `fill-0.bin` through `fill-6.bin`, each
concatenating eight copies of `app://share/iobench.bin` (8 MiB). All returned
zero, as did `sync data://`. Debugger inspection then found 976 free blocks,
`failed=false`, no retained error and EMPTY journal. The final copy was:

```text
cat app://share/iobench.bin app://share/iobench.bin app://share/iobench.bin app://share/iobench.bin > data://fill-7.bin
sync data://
```

The 4 MiB cached copy returned zero. Sync printed `sync: data://: No space left
on device`, then `shell: sync: Exited with status 1`; command completion reported
`exit_status: 1`. The pool had 77 free blocks, `failed=false`, EMPTY journal and
no retained error after that acknowledgment. Successful batches had committed
before the last batch ran out of space. Waiting another 32 seconds produced
`npfs: background writeback/cleanup failed (status 24); pool retained` in serial
output; inspection now showed retained `CALL_NO_SPACE` and `failed=false`.

```text
rm data://fill-0.bin
sync data://
sync data://
cat app://share/iobench.bin > data://recovered.bin
sync data://
```

Remove succeeded. The first sync reported the retained NO_SPACE once (exit 1),
then inspection showed 2,002 free blocks and `CALL_OK`. The second sync succeeded,
as did the new 1 MiB copy and its sync. Final inspection showed 1,745 free blocks,
no cleanup membership, EMPTY journal and no failed-pool latch. The remote shell
exited normally with complete output draining before QEMU was stopped.

The stopped extracted pool passed `fsck.npfs`. Inspector extraction and `cmp`
confirmed `recovered.bin` matches the 1 MiB source and the complete `fill-7.bin`
matches four concatenated source copies. The removed file was absent; the other
six 8 MiB files and the 4 MiB final file remained listed. This observes delayed
NO_SPACE, periodic failure retention, one-time reporting and recovery without a
reboot. It does not exercise an allocator failure or uncertain backing I/O.

Stopped-image checking and the first byte comparison used:

```sh
dd if=/dev/shm/pyxis-npfs-next/disk-b1.raw of=/dev/shm/pyxis-npfs-next/pool-b1-after.raw bs=512 skip=2049 count=131072 conv=sparse status=none
build/fs-tools/fsck.npfs --image /dev/shm/pyxis-npfs-next/pool-b1-after.raw
build/fs-tools/npfs-inspect --image /dev/shm/pyxis-npfs-next/pool-b1-after.raw list --volume bench
build/fs-tools/npfs-inspect --image /dev/shm/pyxis-npfs-next/pool-b1-after.raw extract --volume bench --path recovered.bin --output /dev/shm/pyxis-npfs-next/recovered.bin
cmp /dev/shm/pyxis-npfs-next/recovered.bin build/userspace-root/share/iobench.bin
```

The same inspector command extracted `fill-7.bin`; its comparison source was
ordinary host `cat` concatenating four fixture copies. B2 uses the same stopped
partition extraction and tools with its own filenames and 32 fixture copies.

## B2: retained-open deletion

A fresh empty pool received `big.bin`, built with one `cat` command concatenating
32 copies of the 1 MiB archive fixture, followed by `sync data://`. Two independent
remote sessions then ran:

```text
# Reader session; its stdin file grant remains open through the copy.
cat < data://big.bin > home://copy.bin
# Remover session while the reader is still running.
rm data://big.bin
sync data://
cat data://big.bin
```

Remove and sync returned zero. Lookup by name printed `Not found` and exited 1.
Inspection before and after removal showed exactly 7,138 free blocks; after
removal inode 2 still had one reference, size 33,554,432, DETACHED flag 1 and
cleanup head 2. The read completed with exit zero despite its name being gone.
After its final close, inspection showed 15,347 free blocks, cleanup head zero,
zero references and EMPTY journal. Thus no mappings were reclaimed while the
handle was retained, and cleanup reclaimed them after close.

```text
cat home://copy.bin > data://copy.bin
sync data://
```

Both succeeded. After both remote sessions exited normally and QEMU stopped,
host fsck passed. Extracted `copy.bin` was exactly 32 MiB and matched 32 source
fixture copies with `cmp` (SHA-256
`b8efa7fd42d4ffe882012b04a6b102666c7a3a0fe301218b9f771bcefa117908`).
This verifies the complete retained-handle read, not only successful calls.

The guest Lua has no io/os library. Preliminary attempts to run an ordinary
nested shell lacked launcher resources, and `>>` was rejected as an unsupported
operator. Those existing interfaces were not extended; one long `cat` copy and
a second session provide the overlap without new programs or grants.

## B2 interrupted variant: pending detached cleanup across restart

The fresh pool imported the verified 32 MiB contents as `big.bin`. The reader
again ran `cat < data://big.bin > home://copy.bin`; the second session ran
`rm data://big.bin` and `sync data://`, both successful. Immediately before the
manual QEMU `quit`, inspection showed 7,138 free blocks, inode 2 referenced once
and DETACHED, cleanup head 2 and EMPTY journal. The reader had not completed.
No process/group shutdown or final handle close preceded the stop. Both clients
then reported disconnection without final acknowledgment, as expected for an
unclean stop; these sessions did not exit normally.

The stopped extracted pool passed read-only fsck without replay. Inspector
`volumes` reported `inode_bytes 768 cleanup_head 2`, and the root listing was
empty. Thus persisted cleanup membership was present before restarting; the
host checker did not reclaim it. The same disk then booted with fresh OVMF
variables and the same writable init. Before any new write, inspection showed
15,347 free blocks, cleanup head zero, EMPTY journal and no failed-pool latch.
Lookup of `big.bin` remained NOT_FOUND.

```text
cat app://share/iobench.bin > data://after-restart.bin
sync data://
```

Both succeeded. After normal remote-shell exit and QEMU stop, the extracted pool
again passed fsck; inspector reported cleanup head zero and only the 1 MiB
`after-restart.bin`. Its extracted bytes matched the archive source with `cmp`.
These observations establish cleanup after loss of retained references at an
unclean restart. The stop occurred before reclamation, while the reader held the
inode; no arbitrary mid-batch or uncertain-I/O interruption is qualified.

## Results and remaining gaps

Delayed allocation ENOSPC, background error retention, sync acknowledgment and
space recovery, retained-open unlink with complete byte verification, final-close
cleanup and pending detached cleanup across a writable restart are runtime
exercised. No kernel or application behavior changed, so no matched performance
measurement is required or claimed. Allocator pressure/failure, uncertain backing
I/O, arbitrary interruption during a cleanup batch, read-only-device opening and
physical media remain unqualified by these runs.

Serial logs and existing machine-client records are retained in
`build/npfs-next/`; disposable images and comparison files are in
`/dev/shm/pyxis-npfs-next/`. Ordinary image assembly was restored after removing
the init/mount overrides. All QEMU and debugger jobs started for this task are
stopped; unrelated jobs and the original checkout's local filesystem pin were
left alone. The protected task-3 experiment directory is unchanged.
