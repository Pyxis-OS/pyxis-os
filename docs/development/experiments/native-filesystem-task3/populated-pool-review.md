# Populated-pool allocation review

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

PR #348's review found a task-3 blocker at `a663b65`: allocation reread an on-disk bitmap
page for each candidate block, starting at block 1 after every mount, and mapping checks
reread it for existing data blocks. The initial measurements used nearly empty pools and
did not qualify this population-dependent cost.

The kernel now retains the full bitmap in VM after replay. Staged bitmap journal images
overlay that checkpointed base; only durable EMPTY folds them in, and healthy rollback drops
them. Allocation scans cached 64-bit words from the mount scan's first free block and wraps
once; mapping checks and staging use memory. The single-descriptor-block journal layout has
a compile-time capacity assertion. The engine fix is `302d921`; `ede0693` names the mount
read bound and explains the assertion without changing values or behavior.

## Configuration

Ordinary interactive validation with the QEMU 10.2.2 AHCI-fixed executable and nested KVM
from the [initial record](README.md): four CPUs (remote workload on CPU 3), 2 GiB, `-cpu
max`, fresh OVMF variables, virtio-net/RNG and writable 512-byte virtio-blk with flush and
`cache=writeback`. Disposable pool and disk files lived in tmpfs on a host with swap, so
this is virtual-device work, not a no-swap RAM matrix or physical storage result.

Both pools were 1 GiB with a 16 MiB journal and a `bench` volume, each embedded at sector
2049 in a 1030 MiB GPT disk. The control held the 1 MiB iobench fixture; the populated one
added seven sparse files of 100,000,000 zero bytes (700,000,000 source bytes, imported into
allocated mappings), leaving about 67% of the pool's blocks allocated. The original and
fixed populated boots started from copies of one formatted disk, the original rebuilt from
`a663b65`. Host formatting and fsck passed; no compiler-container rebuild, tests or fault
injection were added. The populated init mounted `bench` read/write as `data://`; the
control and populated reboot init opened it read-only as `ro://` first, then upgraded the
retained pool through writable `data://` before starting the session.

## Observations

On the original populated code, `date > data://t1.txt` succeeded and `sync data://t1.txt`
then failed with status 17 (`CALL_TIMED_OUT`), with the background writeback timeout
logged. A monitor snapshot after the failure showed 118,291 target reads and 484,676,608
read bytes, up from 80 reads and 484,352 bytes (background retries included, so not a
per-sync count). Host fsck passed after a normal stop. This reproduces the availability
failure without establishing crash recovery.

On the fixed populated disk, the first `date`, its sync and the next create succeeded.
GDB (read-only) showed `bitmap_bytes = 32768`, `bitmap_loaded = true`, `failed = false`,
`writeback_error = CALL_OK`, 86,522 free blocks and a next-free hint of 175,621. The
benchmark ran grow and prepared writes with a 4080-byte buffer, one warmup and three
verified 1 MiB samples (258 writes each, no short writes), syncing separately:

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

The populated files passed benchmark verification, directory sync, extracted-pool fsck and
byte comparison with the source fixture. The control's first sync and next create also
succeeded after the read-only-then-upgrade mount order. After a clean reboot through that
order, the read benchmark verified the persisted fixture, a write through `ro://` was
rejected before launch, and a new writable file was created and synchronized. These small
samples qualify this population case, not arbitrary pool sizes, fragmentation, owner-host
latency or physical SSD wear; the earlier 128 MiB measurements used different memory and
backing storage and are not a matched comparison.

The accepted error policy is intact: sync acknowledges recoverable errors, ongoing failures
still fail retries and terminal uncertain I/O stays latched. A timeout does not bypass
ordered-data dependencies, and namespace operations that conservatively flush the pool can
still wait for unrelated dirty data. Multi-block data/journal coalescing and wider metadata
caching remain outside this correction. Committed-log replay, interrupted cleanup, ENOSPC
and allocator pressure have source-review coverage only.
