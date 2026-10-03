# Populated-pool allocation review

PR #348's review found a task-3 blocker at `a663b65`: allocation reread an on-disk
bitmap page for each candidate block, starting at block 1 after every mount.
Mapping checks also reread it for existing data blocks. The initial measurements
used nearly empty pools and did not qualify this population-dependent cost.

The kernel now retains the full bitmap in VM after replay. Staged bitmap journal
images overlay that checkpointed base; only durable EMPTY folds them into it.
Healthy rollback drops staged images without changing the base. Allocation scans
cached 64-bit words, starts at the mount scan's first free block and wraps once.
Mapping checks and bitmap staging use memory. The single-descriptor-block journal
layout has a compile-time capacity assertion. The engine fix is `302d921`;
`ede0693` names the mount read bound and explains the assertion without changing
its values or runtime behavior.

## Configuration and reproduction

This ordinary interactive validation used the existing QEMU 10.2.2 AHCI-fixed
executable and nested KVM from the initial record: four CPUs, CPU 3 running the
remote workload, 2 GiB guest RAM, `-cpu max`, fresh OVMF variables, virtio-net/RNG
and writable 512-byte virtio-blk with flush and `cache=writeback`. Disposable pool
and disk files were in a task-owned `/dev/shm` directory. The host has swap, so
this is tmpfs-backed virtual-device work, not a no-swap RAM matrix or physical
storage result.

Both pools were 1 GiB with an explicit 16 MiB journal and a `bench` volume. Each
was embedded at sector 2049 in its own 1030 MiB GPT disk with disk GUID
`12345678-1234-4567-89ab-0123456789ab`. The control source had the existing 1 MiB
iobench fixture. The populated source added seven ordinary sparse files of
100,000,000 zero bytes each, imported by the formatter into allocated mappings.
Their source bytes total 700,000,000; with metadata, journal and the fixture,
about 67% of the pool's blocks were allocated. Source sparseness does not leave
their pool mappings unallocated.

Host formatting and initial fsck passed. The original and fixed populated boots
started from separate copies of the same formatted disk. The original image was
rebuilt from `a663b65` before the fix. Ordinary image/host-tool builds passed for
the corrected engine and the final named-constant clarification, using the
existing compiler and the build commands from the parent measurement record.
No compiler-container rebuild, tests or fault injection were added.

The original and fixed populated init mounted `bench` read/write as `data://`,
then created a namespace and started the existing remote services. The control
and populated reboot init opened it read-only as `ro://` first, then upgraded
the retained pool through writable `data://` before starting the session.

## Observations

Captured [original failure](bitmap-before.txt), [fixed populated workload](bitmap-populated.txt),
[control workload](bitmap-control.txt) and [read-only GDB inspection](bitmap-gdb.txt)
retain output and typed completion evidence.

On the original populated code, `date > data://t1.txt` succeeded, then
`sync data://t1.txt` failed with status 17 (`CALL_TIMED_OUT`). The serial record
also logged the background writeback timeout. A monitor snapshot after the
failed sync and background work showed 118,291 target reads and 484,676,608
read bytes, up from 80 reads and 484,352 bytes before the command. That window
includes background retries; it is not a per-sync I/O count or elapsed interval.
Host fsck passed after stopping the client and QEMU normally. This reproduces
the availability failure without establishing crash recovery.

On the fixed populated disk, the first `date`, its sync and the next create all
succeeded. GDB read-only inspection showed `bitmap_bytes = 32768`,
`bitmap_loaded = true`, `failed = false` and `writeback_error = CALL_OK`.
The pool's free-block count was 86,522 and its next-free hint was 175,621 at that
snapshot. No debugger engine calls or mutation were injected.

The existing benchmark then ran grow and prepared writes with a 4080-byte buffer,
one untimed warmup and three verified 1 MiB samples, synchronizing the retained
file separately. Each sample used 258 writes without short writes. Prepared
mode also writes and syncs untimed preparation before each measured overwrite.

| Fixed pool / interval | Median | Range |
| --- | ---: | ---: |
| Populated grow transfer | 200.252 ms | 198.769–204.674 ms |
| Populated grow sync | 197.503 ms | 197.267–198.215 ms |
| Populated prepared transfer | 136.630 ms | 135.729–137.197 ms |
| Populated prepared sync | 171.387 ms | 170.248–172.035 ms |
| Nearly empty grow transfer | 202.833 ms | 194.495–204.628 ms |
| Nearly empty grow sync | 197.892 ms | 195.487–198.852 ms |
| Nearly empty prepared transfer | 144.187 ms | 144.109–149.546 ms |
| Nearly empty prepared sync | 187.600 ms | 186.519–189.426 ms |

The populated files passed benchmark verification, subsequent directory sync,
extracted-pool fsck and byte-for-byte comparisons with the source fixture.
The control's first sync and subsequent create also succeeded after opening the
pool read-only and upgrading it to writable. It verified all benchmark samples.
After clean reboot of the populated disk through that same mount-upgrade order,
the existing read benchmark verified the persisted grow-write fixture. A write
through `ro://` was rejected before launch; a new writable file was created and
synchronized successfully. The [reboot output](bitmap-reboot.txt) records this
normal persistence and first-allocation check.
These small samples qualify this concrete population case, not arbitrary pool
sizes, fragmentation, owner-host latency or physical SSD wear. Earlier 128 MiB
measurements used different guest memory and backing storage; they are not a
matched latency comparison with this run.

The accepted error policy remains intact: sync acknowledges recoverable errors,
while an ongoing failure still fails retries and terminal uncertain I/O remains
latched. A timeout does not bypass ordered-data dependencies; namespace operations
that conservatively flush the current pool can still wait for unrelated dirty
data. This fix removes the measured allocation scan cost. Multi-block data/journal
coalescing and wider metadata caching remain outside this correction.

Committed-log replay, interrupted cleanup, ENOSPC and allocator pressure have
source-review coverage only. This review follow-up adds no runtime crash/error
injection or physical-media qualification.
All validation clients, QEMU and debugger processes were stopped, and the
task-owned disposable tmpfs directory was removed after extraction/checking.
