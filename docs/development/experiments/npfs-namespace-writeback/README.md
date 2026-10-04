# npfs namespace writeback separation

This addresses create, rename and shrink flushing unrelated dirty pool data,
identified in [review #348](https://git.internal/PyxisOS/pyxis-os/pulls/348).
The owner accepted keeping these operations immediately durable while flushing
only affected files when required for consistency. Whole-pool file/directory sync
and disk-scoped mount sync remain unchanged.

Create and rename without replacement commit their metadata without flushing
cached files. Replacement rename first flushes the moved file, then stages the
victim's last durable record with DETACHED/list fields. An open victim keeps its
live cached contents. Shrink flushes only its target before committing
the smaller size; this preserves pending growth, timestamps and retained partial
block writes, and can still fail on the target's own delayed-allocation error.

Before running, expected results were: a successful small create/rename would
leave unrelated cached data pending; shrink would affect only its target;
replacement would preserve an open victim's bytes through EOF; explicit sync
would subsequently make retained linked data durable. Rename without replacement
does not promise unsynchronized file contents after a crash. Replacement flushes
the new contents before removing the old name. Pressure/cache
exhaustion and background flushing may still flush the entire pool.

## Matched ordinary workload

Main `b352a1d` was compared with standalone implementation `5c403ea`.
Both exclude the other performance changes. The
[shared baseline/configuration record](../npfs-io-runs/README.md) gives exact
build, disk creation, QEMU and init commands, pins and
[baseline output](../npfs-io-runs/baseline.txt). The changed boot substituted
`namespace` for `io` in QEMU file names. It rebuilt its kernel/image against the
same verified SDK/userland/ports bundles with `make -j16 image
PREBUILT="sdk userspace ports"` and the same build overrides. Kernel compilation
was warning-free; no dependency or compiler-container change was needed.

The configuration was four CPUs, 256 MiB, nested KVM, QEMU 10.2.2 with AHCI fix
`d9f78431d8ebdc2d03ad74461138c1c9eb076aa5`, fresh OVMF variables, 512-byte VirtIO
sectors, 64 KiB device transfers and writeback caching. All target files were
regular tmpfs images under `/dev/shm/pyxis-npfs-performance`. The 128 MiB pool,
8 MiB journal and imported 1 MiB fixture were identical. The default 30-second
background interval was enabled in both measured boots; no other benchmark VM
ran concurrently.

Each command was entered individually through the existing machine-mode remote
client on port 23389, waiting for completion before the next:

```text
iobench write data://grow.bin --buffer 4080 --rounds 5 --sync
iobench write data://prepared.bin --prepared --buffer 4080 --rounds 5 --sync
```

Each verified one untimed warmup and five measured samples, with 258 payload
writes per sample, no short writes and exit status zero. Complete output is
[namespace.txt](namespace.txt).

| Interval | Main median (range), ms | Namespace-only median (range), ms |
| --- | ---: | ---: |
| Grow transfer | 197.462 (193.757–207.983) | 201.091 (195.647–206.147) |
| Grow sync | 197.152 (196.167–198.450) | 197.025 (195.772–198.390) |
| Prepared overwrite transfer | 153.359 (149.764–320.124) | 148.937 (143.986–156.684) |
| Prepared overwrite sync | 188.077 (62.798–191.799) | 186.407 (185.900–187.314) |

These warm single-file intervals show no material sync change; they do not time
create itself or establish owner-host latency. Background flushing can overlap
samples, so the baseline's outlying transfer/sync pair is retained.

## Small create behind an unrelated cached write

Both kernels ran these commands after the matched workload:

```text
cat app://share/iobench.bin > data://dirty2.bin
date > data://small2.txt
```

Immediately before the second command, read-only GDB inspection showed the
cached file's size 1048576, durable size 0 and `size_dirty=true` in each kernel.
QEMU `info blockstats` target counters bracketed the small create:

| Counter | Main before / after | Namespace-only before / after |
| --- | ---: | ---: |
| Write bytes | 22,384,640 / 23,535,616 | 21,139,456 / 21,168,128 |
| Write requests | 5,465 / 5,746 | 5,161 / 5,168 |
| Flushes | 300 / 312 | 276 / 280 |

The main create caused 1,150,976 bytes, 281 requests and 12 flushes; afterward the
unrelated file was clean with durable size 1048576. The changed create caused
28,672 bytes, seven requests and four flushes; afterward the unrelated file
remained dirty with durable size 0. Counters include the namespace operation and
asynchronous checkpoint work, not isolated syscall costs or physical wear.
The baseline ran additional reads/creates before this window, but both selected
files used existing inode slots and the same one-page root directory.

Renaming `dirty2.bin` to `moved.bin` succeeded. A periodic flush overlapped the
later debugger check, so that attempt does not prove retained dirty state.
A separate immediate pair did:

```text
cat app://share/iobench.bin > data://rename-dirty.bin
mv data://rename-dirty.bin data://renamed-dirty.bin
sync data://
```

After rename and before sync, the moved inode still had size 1048576, durable
size 0 and `size_dirty=true`. Later sync succeeded. Both moved and renamed files
were extracted from the stopped first disk and matched the fixture with `cmp`;
host `fsck.npfs` passed.

## Target-only shrink and retained-open dirty replacement

A second ordinary boot used the same implementation/disk, with the existing
menuconfig option `CONFIG_NPFS_FLUSH_SECONDS=300`. This separates manual debugger
observations from periodic flushing; its results are behavioral checks, not part
of the matched performance table. QEMU used `namespace-long-interval.iso`, fresh
`namespace-long-vars.fd` and the same `namespace-disk.raw`. Other options and
build overrides were unchanged. The interval was restored to 30 afterward.

```text
cat app://share/iobench.bin > data://other-long.bin
date > data://grow.bin
sync data://
```

The existing 1 MiB grow file was truncated to zero before the date application's
26-byte cached write. After that command its durable size was zero, while
`other-long.bin` remained size 1048576, durable size 0, `size_dirty=true`.
This confirms that shrinking a target did not flush the unrelated file. Sync
then succeeded.

Next, one `cat` command with 31 copies of the operand
`app://share/iobench.bin` wrote `data://victim.bin`, without sync. Its live size
was 32505856 (31 MiB), durable size 29360128 (28 MiB), `size_dirty=true` and no
handles remained. The cache's ordinary capacity-driven flushing made the prefix
durable while retaining the final 3 MiB. A second remote session started:

```text
cat < data://victim.bin > home://victim-copy.bin
```

While that reader was active, the first session ran:

```text
mv data://iobench.bin data://victim.bin
```

Rename succeeded. GDB then showed the old victim inode 11 still referenced once,
with its 31 MiB live size, 28 MiB durable size and dirty state unchanged.
`record.cleanup=DETACHED` and the volume cleanup head was 11. The replacement
name now selected the imported 1 MiB source. After the old reader completed
successfully, cleanup head became zero and free blocks were 29419.

The reader session then ran:

```text
cat home://victim-copy.bin > data://victim-copy.bin
sync data://
exit
```

The first session also ran `sync data://` and `exit`. All commands succeeded,
clients reported complete final draining, and QEMU was quit. The final stopped
partition passed host `fsck.npfs`; extracted `victim.bin` matched the 1 MiB
fixture, and `victim-copy.bin` matched 31 concatenated fixtures with `cmp`.
The latter's SHA-256 was
`f367e0c90c768764a50f7d5bb68fb74b9c2afe1c8fab42f3bdf0578cdc5dc5bd`.
Complete records are [namespace-long.txt](namespace-long.txt) and
[namespace-long-reader.txt](namespace-long-reader.txt).

Debugger observations used the matching archived ELF and port 12389, printing
`opened_pools->volumes->inodes` size/durable/dirty/reference/cleanup fields and
`opened_pools->volumes->record.cleanup_head`, then detaching. No engine functions
were called or kernel state modified. Stopped-pool extraction used `dd bs=512
skip=2049 count=262144`; verification used the existing `fsck.npfs`,
`npfs-inspect extract --volume bench --path ... --output ...` and `cmp`.

No new tests, self-tests, fault injection or automation were added. Disk-full,
allocator pressure, arbitrary crash points, uncertain writes and physical media
are not qualified by these runs; healthy/terminal rollback remains source-reviewed.
The protected task-3 measurements were not changed.

## Combined stack and current-main integration

The PRs are stacked in merge order #382 (I/O runs), #386 (metadata cache),
#387 (namespace writeback). The combined kernel at `d8eb88a`, still on main
`b352a1d`, passed the same grow/prepared/read commands with five samples each,
followed by `sync data://` and normal remote exit. It used a fresh copy of the
initial disk, the same default 30-second interval and the same QEMU options,
substituting `combined` in the file names. Stopped-pool fsck and extracted
grow/prepared comparisons passed. [combined.txt](combined.txt) retains the output;
this confirms integration, not attribution of one item's performance to another.

Main then advanced through `4b5e256` (USB block registration/GPT integration).
It was merged into #382 and carried through both dependent branches without
changing any submodule pins. A rebuilt warning-free kernel/image at `d5956f9`
booted the already-written combined disk, with fresh OVMF variables, default
30-second flushing and xHCI disabled. The QEMU command substituted
`current-main-combined.iso`, `current-main-combined-vars.fd` and
`current-main-combined-serial.txt`, retaining `combined-disk.raw`. It ran:

```text
iobench read data://grow.bin --buffer 4088 --rounds 1
cat app://share/iobench.bin > data://integration.bin
date > data://independent.txt
mv data://integration.bin data://integration-renamed.bin
sync data://
exit
```

The warmup/sample verified the persisted grow file. After creating
`independent.txt`, GDB showed the unrelated integration file still dirty with
live size 1048576 and durable size zero. Rename and sync succeeded, the client
reported complete draining, and QEMU was quit. Host fsck passed again; extracted
`integration-renamed.bin` matched the installed fixture with `cmp`.
[current-main-combined.txt](current-main-combined.txt) is the complete output.
This is a persistence/integration check, not a new matched performance comparison
against the changed main or a qualification of its USB backend. All task-owned
QEMU, debugger and remote-client processes were stopped.

## Replacement crash correction from review #387

Review of `365a1df` found that metadata-only replacement could discard a durable
old file while publishing a moved inode with durable size zero. The existing
retained-open-victim run used an already durable source, so it did not exercise
this case. Fix `319bc8f` flushes only the moved file when a victim exists, before
beginning the namespace transaction and taking its rollback snapshots. A failed
flush returns without changing either name. The victim's durable detachment and
retained-handle behavior are unchanged. Delayed allocation may now make a
replacement fail on the moved file's own disk-full error; that failure path was
source-reviewed, not forced in this run.

The reviewer reproduced the loss with the default 30-second interval. Our first
two manual attempts at that interval encountered background writeback and the
replacement survived; those attempts do not qualify cached replacement. To keep
the cached state observable during manual inspection, both comparison kernels
were rebuilt with the existing `CONFIG_NPFS_FLUSH_SECONDS=300` option. Baseline
was `365a1df`; fixed kernel was `319bc8f`. All other configuration, pinned bundles,
128 MiB initial pool, 8 MiB journal, four CPUs, 256 MiB, nested KVM, patched QEMU
and VirtIO writeback disk settings match the earlier record. Each boot used a
fresh `initial-disk.raw` copy and fresh OVMF variables; QEMU file names substituted
`replacement-before` or `replacement-fixed` and the archived `*-300.iso`.
This was a behavior check, not a new latency comparison.

Each command was submitted separately, waiting for exit status zero:

```text
cat app://share/hello.txt > data://target.txt
sync data://
cat app://share/iobench.bin > data://tmp.bin
mv data://tmp.bin data://target.txt
```

The fixed boot additionally wrote `unrelated.bin` from the same 1 MiB fixture
between sync and the temp-file write. Neither boot synced after replacement.
Read-only GDB inspection immediately after rename showed:

| Kernel / inode | Live size | Durable size | Dirty size |
| --- | ---: | ---: | --- |
| Baseline moved file, inode 15 | 1048576 | 0 | true |
| Fixed moved file, inode 14 | 1048576 | 1048576 | false |
| Fixed unrelated file, inode 15 | 1048576 | 0 | true |

After detaching GDB, each QEMU process was killed with SIGKILL while the cached
state remained pending. Partition extraction used the same `dd bs=512 skip=2049
count=262144` command; host `fsck.npfs --image ... --replay` passed for both.
`npfs-inspect list --volume bench` reported baseline `target.txt` size zero and
fixed `target.txt` size 1048576. The fixed file was extracted and `cmp` matched
the source fixture exactly. Fixed `unrelated.bin` recovered with size zero, as
expected for its unsynchronized contents: replacement did not flush it.
[replacement-crash.txt](replacement-crash.txt) retains every guest command
completion and both debugger snapshots. The expected remote disconnect occurred
because of abrupt VM termination; no complete final drain is claimed.

The 30-second configuration was restored and the ordinary default image rebuilt.
All QEMU, debugger and remote-client jobs from this correction were stopped.
No new test program, self-test, fault-injection mechanism or automation was added;
only the manual abrupt-shutdown case requested by the review was exercised.
Arbitrary crash points, uncertain disk failures and physical media remain outside
this qualification.
