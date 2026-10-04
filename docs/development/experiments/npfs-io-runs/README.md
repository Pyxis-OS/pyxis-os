# npfs contiguous write runs

This addresses the serial single-block write paths identified in
[review #348](https://git.internal/PyxisOS/pyxis-os/pulls/348).
The comparison is current main `b352a1d` against implementation `007be3e`;
metadata caching and namespace writeback changes are absent from both.
Expected behavior before the runs: identical synced bytes and journal ordering,
fewer backing requests for physically contiguous writes, and unchanged sync
completion at durable COMMITTED. Allocation remains delayed until writeback.

## Implementation

File flush first prepares the existing bounded set of at most 128 mappings,
then writes adjacent physical homes in runs of at most 32 blocks. A lazily
allocated 128 KiB VM buffer gathers the cache entries' separate payloads.
Allocation failure falls back to single-block writes; worker pressure maintenance
releases that buffer. Failed mapping preparation restores every selected dirty
entry without publishing an inode prefix. Data write uncertainty still stops
mutation, and metadata is committed only after all selected data writes succeed.

Journal image bytes are already contiguous in scratch and are submitted as one
extent after the descriptor. Checkpoint groups only adjacent home addresses
whose scratch indices are also adjacent; it does not reorder images. The existing
transport splits all extents at the device's transfer limit. Flush fences, CRC
input order, control publication, durable EMPTY and bitmap reuse remain unchanged.

## Configuration and commands

Linux nested KVM, QEMU 10.2.2 with upstream AHCI fix
`d9f78431d8ebdc2d03ad74461138c1c9eb076aa5`, four CPUs, 256 MiB,
`-cpu max`, OVMF CODE/VARS from `/usr/share/edk2/ovmf`. A fresh VARS copy and a
fresh copy of the same initial disk were used for each boot. This is emulated
VirtIO storage, not owner-host latency, USB, physical durability or SSD wear.

Both images were built with Pyxis GCC 16.2.0 and `make -j16 image fs-tools`.
The baseline rebuilt kernel, SDK, userland and ports from the selected pins:
fs `d352c7e`, userland `ad1d53a`, ports `bf7667c`, lwIP `a1aadb9`.
The changed kernel image reused those exact verified SDK/userland/ports bundles
with `PREBUILT="sdk userspace ports"`. No compiler-container rebuild was needed.
Existing vendor port warnings appeared during the baseline build; the changed
kernel compiled without warnings.

The build used the existing host CMake environment and these overrides:

```sh
PATH=/home/chronium/src/pyxis-native-writer-build/host-tools/bin:$PATH \
make -j16 image fs-tools \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis- \
  PYTHON=/usr/bin/python3 \
  INIT=/home/chronium/src/pyxis-npfs-performance-build/npfs-performance/init-native.sh \
  INIT_PRIMARY=app://init-idle INIT_CPUS=3=app://init \
  MOUNT_DISK=12345678-1234-4567-89ab-0123456789ab
```

Temporary uncommitted init:

```text
#!app://shell.pxe
mount --partition 1 --volume bench --read-write data://
namespace create
service start text app://textfs.pxe
session app://session.pxe --configure-network --start-remote-services
```

All target images were regular files under `/dev/shm/pyxis-npfs-performance`.
The source directory contained only the installed 1 MiB `iobench.bin` fixture.

```sh
build/fs-tools/mkfs.npfs --image /dev/shm/pyxis-npfs-performance/initial-pool.raw \
  --size 128MiB --journal 8MiB --volume bench \
  --source /dev/shm/pyxis-npfs-performance/source
build/fs-tools/fsck.npfs --image /dev/shm/pyxis-npfs-performance/initial-pool.raw
truncate -s 132M /dev/shm/pyxis-npfs-performance/initial-disk.raw
sgdisk --clear --set-alignment=1 --new=1:2049:+128M \
  --disk-guid=12345678-1234-4567-89ab-0123456789ab \
  /dev/shm/pyxis-npfs-performance/initial-disk.raw
dd if=/dev/shm/pyxis-npfs-performance/initial-pool.raw \
  of=/dev/shm/pyxis-npfs-performance/initial-disk.raw bs=512 seek=2049 \
  conv=notrunc status=none
```

QEMU invocation for the changed boot (baseline substituted `baseline` for `io`):

```sh
/tmp/pyxis-qemu-ahci-fix/build/qemu-system-x86_64 \
  -machine q35 -accel kvm -cpu max -smp 4 -m 256M \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=/dev/shm/pyxis-npfs-performance/io-vars.fd \
  -cdrom /dev/shm/pyxis-npfs-performance/io.iso \
  -drive if=none,id=target,format=raw,file=/dev/shm/pyxis-npfs-performance/io-disk.raw,cache=writeback \
  -device virtio-blk-pci,drive=target,disable-legacy=on \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:23389-:2323 \
  -device virtio-net-pci,netdev=net0,disable-legacy=on \
  -object rng-random,id=rng0,filename=/dev/urandom \
  -device virtio-rng-pci,rng=rng0,disable-legacy=on \
  -display none \
  -serial file:/home/chronium/src/pyxis-npfs-performance-build/npfs-performance/io-serial.txt \
  -monitor stdio -gdb tcp:127.0.0.1:12389
```

The target reported 512-byte sectors and a 64 KiB transfer limit. CPU 3's existing
remote session ran each guest command separately, waiting for completion:

```text
iobench write data://grow.bin --buffer 4080 --rounds 5 --sync
iobench write data://prepared.bin --prepared --buffer 4080 --rounds 5 --sync
sync data://
exit
```

The client was `build/tools/pyxis-remote --machine --no-shell-echo --columns 120
--rows 40 127.0.0.1 23389`. Full decoded outputs are [baseline.txt](baseline.txt)
and [io.txt](io.txt). The baseline additionally exercised reads and small creates
after the measured writes; those commands are outside these timing/counter windows.

## Results

Each workload verified one untimed warmup and five measured 1 MiB samples,
with 258 payload writes/sample, no short writes and exit status zero.
These are warm elapsed intervals, not confidence estimates or cold-disk timings.

| Interval | Main median (range), ms | Batched median (range), ms |
| --- | ---: | ---: |
| Grow transfer | 197.462 (193.757–207.983) | 201.570 (189.914–219.783) |
| Grow sync | 197.152 (196.167–198.450) | 35.682 (35.082–37.256) |
| Prepared overwrite transfer | 153.359 (149.764–320.124) | 144.699 (143.105–150.027) |
| Prepared overwrite sync | 188.077 (62.798–191.799) | 25.286 (24.902–25.886) |

The unchanged 30-second background flush may overlap an individual sample;
the main prepared sample with a 320 ms transfer and 63 ms sync illustrates why
transfer and sync must both be retained instead of quoting only the best sync.

Manual QEMU `info blockstats` snapshots bracketed each complete command, including
warmup/preparation, creation and asynchronous checkpoint activity:

| Window | Main write bytes / requests / flushes | Batched write bytes / requests / flushes |
| --- | ---: | ---: |
| Grow, 6 MiB logical payload | 7,794,688 / 1,903 / 172 | 7,794,688 / 393 / 172 |
| Prepared, 12 MiB including preparation | 13,348,864 / 3,259 / 104 | 13,316,096 / 356 / 100 |

Grow write bytes and flushes are identical; write requests fell about 79%.
Prepared totals differ by one small checkpoint/transaction window; these counters
are complete-command observations, not isolated syscall costs or wear estimates.

Both clients exited with complete final output draining; QEMU was then quit.
For each stopped disk, the partition was extracted with `dd bs=512 skip=2049
count=262144`. Host `fsck.npfs --image` passed, and `npfs-inspect extract --volume
bench --path grow.bin` and `--path prepared.bin` outputs both matched the installed
fixture with `cmp`. No new tests, self-tests, fault injection or automation were
added. Fragmented transfers, allocation-pressure fallback and backing failures
have source-review coverage only; physical media remains unqualified.
