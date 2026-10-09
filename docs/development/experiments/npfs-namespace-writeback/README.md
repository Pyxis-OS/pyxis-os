# npfs namespace writeback separation

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

This addresses create, rename and shrink flushing unrelated dirty pool data, identified in
[review #348](https://git.internal/PyxisOS/pyxis-os/pulls/348). The owner accepted keeping
these operations immediately durable while flushing only affected files when required for
consistency. Whole-pool file/directory sync and disk-scoped mount sync are unchanged.

Create and rename without replacement commit their metadata without flushing cached files.
Replacement rename first flushes the moved file, then stages the victim's last durable
record with DETACHED/list fields; an open victim keeps its live cached contents. Shrink
flushes only its target before committing the smaller size, preserving pending growth,
timestamps and retained partial block writes, and can still fail on the target's own
delayed-allocation error. Rename without replacement does not promise unsynchronized file
contents after a crash; replacement flushes the new contents before removing the old name.
Pressure, cache exhaustion and background flushing may still flush the entire pool.

## Matched ordinary workload

Main `b352a1d` against standalone implementation `5c403ea`, both excluding the other
performance changes. Build, disk creation, QEMU and init commands, pins and baseline output
follow the [shared baseline record](../npfs-io-runs/README.md); the changed boot rebuilt
its kernel and image against the same verified bundles
(`make -j16 image PREBUILT="sdk userspace ports"`, warning-free, no dependency or
compiler-container change). Four CPUs, 256 MiB, nested KVM, QEMU 10.2.2 with AHCI fix
`d9f78431d8eb`, fresh OVMF variables, 512-byte VirtIO sectors, 64 KiB transfers and
writeback caching, all target files in tmpfs; the 128 MiB pool, 8 MiB journal and imported
1 MiB fixture were identical, the default 30-second background interval was enabled and no
other benchmark VM ran. Each command went through the machine-mode remote client (port
23389), waiting for completion:

```text
iobench write data://grow.bin --buffer 4080 --rounds 5 --sync
iobench write data://prepared.bin --prepared --buffer 4080 --rounds 5 --sync
```

Each verified one warmup and five samples (258 payload writes each, no short writes).

| Interval | Main median (range), ms | Namespace-only median (range), ms |
| --- | ---: | ---: |
| Grow transfer | 197.462 (193.757–207.983) | 201.091 (195.647–206.147) |
| Grow sync | 197.152 (196.167–198.450) | 197.025 (195.772–198.390) |
| Prepared overwrite transfer | 153.359 (149.764–320.124) | 148.937 (143.986–156.684) |
| Prepared overwrite sync | 188.077 (62.798–191.799) | 186.407 (185.900–187.314) |

These warm single-file intervals show no material sync change; they do not time create
itself or establish owner-host latency. Background flushing can overlap samples, so the
baseline's outlying transfer/sync pair is kept.

## Small create behind an unrelated cached write

Both kernels ran `cat app://share/iobench.bin > data://dirty2.bin` then
`date > data://small2.txt`. Just before the second command, read-only GDB showed the cached
file's size 1048576, durable size 0 and `size_dirty=true` in each kernel. QEMU
`info blockstats` target counters bracketed the small create:

| Counter | Main before / after | Namespace-only before / after |
| --- | ---: | ---: |
| Write bytes | 22,384,640 / 23,535,616 | 21,139,456 / 21,168,128 |
| Write requests | 5,465 / 5,746 | 5,161 / 5,168 |
| Flushes | 300 / 312 | 276 / 280 |

Main's create caused 1,150,976 bytes, 281 requests and 12 flushes, after which the unrelated
file was clean with durable size 1048576. The changed create caused 28,672 bytes, seven
requests and four flushes, and the unrelated file stayed dirty with durable size 0. The
counters include the operation and asynchronous checkpoint work, not isolated syscall cost or
physical wear.

Renaming `dirty2.bin` succeeded, but a periodic flush overlapped the later debugger check,
so that attempt proves nothing. A separate immediate sequence (`cat … > data://rename-dirty.bin`,
`mv` to `renamed-dirty.bin`, `sync data://`) showed the moved inode still at size 1048576,
durable size 0 and `size_dirty=true` after the rename and before sync; sync then
succeeded, both moved files extracted from the stopped disk matched the fixture with `cmp`
and host `fsck.npfs` passed.

## Target-only shrink and retained-open dirty replacement

A second boot used the same implementation and disk with `CONFIG_NPFS_FLUSH_SECONDS=300` to
separate manual debugger observations from periodic flushing (behavioral checks, not part
of the performance table; the interval was restored to 30). After
`cat app://share/iobench.bin > data://other-long.bin`, `date > data://grow.bin` truncated the
1 MiB grow file before its 26-byte write: its durable size was zero while `other-long.bin`
stayed at size 1048576, durable size 0, `size_dirty=true`, confirming shrink did not flush the
unrelated file.

One `cat` of 31 copies of the fixture wrote `data://victim.bin` without sync: live size
32505856 (31 MiB), durable size 29360128 (28 MiB), `size_dirty=true`, no handles (capacity-driven
flushing made the prefix durable). A second session started
`cat < data://victim.bin > home://victim-copy.bin`; while it ran, the first session ran
`mv data://iobench.bin data://victim.bin`. Rename succeeded, the old victim inode 11 stayed
referenced once with its 31 MiB live size, 28 MiB durable size and dirty state,
`record.cleanup=DETACHED` with cleanup head 11, and the name now selected the imported 1 MiB
fixture. After the reader finished the cleanup head was zero and free blocks were 29419. All
commands succeeded with complete final draining, the stopped partition passed `fsck.npfs`,
extracted `victim.bin` matched the 1 MiB fixture and `victim-copy.bin` matched 31
concatenated fixtures. GDB only printed inode size/durable/dirty/reference/cleanup fields and
the volume cleanup head; no engine function was called. Partitions were extracted with
`dd bs=512 skip=2049 count=262144` and checked with `fsck.npfs`,
`npfs-inspect extract --volume bench --path … --output …` and `cmp`.

Disk-full, allocator pressure, arbitrary crash points, uncertain writes and physical media
are not qualified, and healthy/terminal rollback remains source-reviewed. No tests,
self-tests, fault injection or automation were added, and the task-3 measurements are
unchanged.

## Combined stack and current-main integration

The PRs are stacked in merge order #382 (I/O runs), #386 (metadata cache), #387 (namespace
writeback). The combined kernel at `d8eb88a`, still on main `b352a1d`, passed the same
grow/prepared/read commands (five samples each), `sync data://` and a normal exit from a fresh
disk copy with the same 30-second interval; stopped-pool fsck and extracted comparisons
passed. This confirms integration, not attribution of any item's performance to another.

Main then advanced through `4b5e256` (USB block registration/GPT integration), merged into
#382 and carried through both dependent branches without changing submodule pins. A
rebuilt warning-free kernel at `d5956f9` (xHCI disabled, fresh OVMF variables, 30-second
flushing) booted the already-written combined disk and ran
`iobench read data://grow.bin --buffer 4088 --rounds 1`, a cat of the fixture to
`integration.bin`, `date > data://independent.txt`, `mv` to `integration-renamed.bin`,
`sync data://` and `exit`. The persisted grow file verified; after `independent.txt` was
created GDB showed the unrelated integration file still dirty (live size 1048576, durable
size zero); rename and sync succeeded, draining completed, fsck passed and the extracted
renamed file matched the fixture. This is a persistence check, not a matched performance
comparison or a qualification of the USB backend.

## Replacement crash correction from review #387

Review of `365a1df` found that metadata-only replacement could discard a durable old file
while publishing a moved inode with durable size zero; the earlier retained-open-victim run
used an already durable source and missed it. Fix `319bc8f` flushes only the moved file when
a victim exists, before the namespace transaction begins and takes its rollback snapshots; a
failed flush returns with both names unchanged, the victim's durable detachment and
retained-handle behavior are unchanged, and replacement may now fail on the moved file's own
disk-full error (source-reviewed, not forced).

The reviewer reproduced the loss at the default 30-second interval. Two manual attempts at that
interval hit background writeback and the replacement survived, which does not qualify cached
replacement. Both comparison kernels were therefore rebuilt with
`CONFIG_NPFS_FLUSH_SECONDS=300` (baseline `365a1df`, fixed `319bc8f`), with all else as before
and a fresh initial disk and OVMF variables per boot. Commands, each waited to exit status 0:

```text
cat app://share/hello.txt > data://target.txt
sync data://
cat app://share/iobench.bin > data://tmp.bin
mv data://tmp.bin data://target.txt
```

The fixed boot also wrote `unrelated.bin` from the fixture between the sync and the temp-file
write; neither boot synced after replacement. Read-only GDB right after the rename showed:

| Kernel / inode | Live size | Durable size | Dirty size |
| --- | ---: | ---: | --- |
| Baseline moved file, inode 15 | 1048576 | 0 | true |
| Fixed moved file, inode 14 | 1048576 | 1048576 | false |
| Fixed unrelated file, inode 15 | 1048576 | 0 | true |

After detaching GDB each QEMU process was killed with SIGKILL while the cached state was
pending. Host `fsck.npfs --replay` passed for both; `npfs-inspect list` showed baseline
`target.txt` at size zero and fixed `target.txt` at 1048576, the fixed file extracted and
matched the fixture exactly, and fixed `unrelated.bin` recovered with size zero as expected
for its unsynchronized contents. Every guest command completed and both snapshots were taken;
the remote disconnect was expected from the abrupt termination and no complete final drain is
claimed. The 30-second configuration was restored. Only this manual abrupt-shutdown case was
exercised; arbitrary crash points, uncertain disk failures and physical media remain outside
the qualification.
