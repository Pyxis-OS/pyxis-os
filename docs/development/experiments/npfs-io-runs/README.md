# npfs contiguous write runs

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

This addresses the serial single-block write paths identified in
[review #348](https://git.internal/PyxisOS/pyxis-os/pulls/348). The comparison is main `b352a1d` against
implementation `007be3e`, with metadata caching and namespace writeback absent from both. Expected before the runs:
identical synced bytes and journal ordering, fewer backing requests for physically contiguous writes and unchanged
sync completion at durable COMMITTED, with allocation still delayed until writeback. This record is also the shared
setup reference for the [metadata cache](../npfs-metadata-cache/README.md) and
[namespace writeback](../npfs-namespace-writeback/README.md) comparisons.

## Implementation

File flush first prepares the existing bounded set of at most 128 mappings, then writes adjacent physical homes in
runs of at most 32 blocks, gathering the cache entries' separate payloads in a lazily allocated 128 KiB VM buffer.
Allocation failure falls back to single-block writes and worker pressure maintenance releases the buffer. Failed
mapping preparation restores every selected dirty entry without publishing an inode prefix, data write uncertainty
still stops mutation, and metadata is committed only after all selected data writes succeed. Journal image bytes,
already contiguous in scratch, are submitted as one extent after the descriptor; checkpoint groups only adjacent home
addresses whose scratch indices are also adjacent and never reorders images. The transport splits all extents at the
device's transfer limit, and flush fences, CRC input order, control publication, durable EMPTY and bitmap reuse are
unchanged.

## Configuration and commands

Linux nested KVM, QEMU 10.2.2 with the upstream AHCI fix `d9f78431d8eb`, four CPUs, 256 MiB, `-cpu max`, OVMF with
a fresh variables copy and a fresh copy of the same initial disk per boot: emulated VirtIO storage, not owner-host
latency, USB, physical durability or SSD wear. Both images were built with Pyxis GCC 16.2.0 and
`make -j16 image fs-tools` (pins fs `d352c7e`, userland `ad1d53a`, ports `bf7667c`, lwIP `a1aadb9`); the baseline
rebuilt kernel, SDK, userland and ports, and the changed kernel reused those verified bundles with
`PREBUILT="sdk userspace ports"`. No compiler-container rebuild was needed; the baseline build showed existing vendor
port warnings and the changed kernel none. Overrides: `INIT=<temporary init>`, `INIT_PRIMARY=app://init-idle`,
`INIT_CPUS=3=app://init`, `MOUNT_DISK=12345678-1234-4567-89ab-0123456789ab`. The temporary uncommitted init:

```text
#!app://shell.pxe
mount --partition 1 --volume bench --read-write data://
namespace create
service start text app://textfs.pxe
session app://session.pxe --configure-network --start-remote-services
```

Target images were regular tmpfs files; the source directory held only the installed 1 MiB `iobench.bin`:

```sh
mkfs.npfs --image initial-pool.raw --size 128MiB --journal 8MiB --volume bench --source source
fsck.npfs --image initial-pool.raw
truncate -s 132M initial-disk.raw
sgdisk --clear --set-alignment=1 --new=1:2049:+128M --disk-guid=12345678-1234-4567-89ab-0123456789ab initial-disk.raw
dd if=initial-pool.raw of=initial-disk.raw bs=512 seek=2049 conv=notrunc status=none
```

QEMU: `-machine q35 -accel kvm -cpu max -smp 4 -m 256M`, OVMF pflash pair, the ISO as `-cdrom`, the target as
`virtio-blk-pci` (`cache=writeback`), VirtIO net with a loopback forward to the remote port and VirtIO RNG,
`-display none`, serial to a file, `-monitor stdio` and a GDB port. The target reported 512-byte sectors and a 64 KiB
transfer limit. The remote shell on CPU 3 ran each command separately, waiting for completion, through
`pyxis-remote --machine --no-shell-echo --columns 120 --rows 40`:

```text
iobench write data://grow.bin --buffer 4080 --rounds 5 --sync
iobench write data://prepared.bin --prepared --buffer 4080 --rounds 5 --sync
sync data://
exit
```

The baseline also exercised reads and small creates afterwards, outside these timing and counter windows.

## Results

Each workload verified one untimed warmup and five measured 1 MiB samples (258 payload writes each, no short writes,
exit status zero): warm elapsed intervals, not confidence estimates or cold-disk timings.

| Interval | Main median (range), ms | Batched median (range), ms |
| --- | ---: | ---: |
| Grow transfer | 197.462 (193.757–207.983) | 201.570 (189.914–219.783) |
| Grow sync | 197.152 (196.167–198.450) | 35.682 (35.082–37.256) |
| Prepared overwrite transfer | 153.359 (149.764–320.124) | 144.699 (143.105–150.027) |
| Prepared overwrite sync | 188.077 (62.798–191.799) | 25.286 (24.902–25.886) |

The unchanged 30-second background flush may overlap a sample; main's prepared sample with a 320 ms transfer and
63 ms sync shows why transfer and sync must both be kept rather than quoting only the best sync. QEMU
`info blockstats` snapshots bracketed each complete command, including warmup, preparation, creation and
asynchronous checkpoint activity:

| Window | Main write bytes / requests / flushes | Batched write bytes / requests / flushes |
| --- | ---: | ---: |
| Grow, 6 MiB logical payload | 7,794,688 / 1,903 / 172 | 7,794,688 / 393 / 172 |
| Prepared, 12 MiB including preparation | 13,348,864 / 3,259 / 104 | 13,316,096 / 356 / 100 |

Grow write bytes and flushes are identical and write requests fell about 79%; prepared totals differ by one small
checkpoint window. These are complete-command observations, not isolated syscall costs or wear estimates. Both
clients exited with complete draining; each stopped disk's partition was extracted (`dd bs=512 skip=2049
count=262144`), host `fsck.npfs --image` passed, and `npfs-inspect extract --volume bench` outputs of `grow.bin` and
`prepared.bin` matched the installed fixture with `cmp`. No tests, fault injection or automation were added;
fragmented transfers, allocation-pressure fallback and backing failures have source review only, and physical media is
unqualified.
