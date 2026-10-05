# SMP task 6: concurrent kernel heap growth

Matched before/after record for [SMP task 6](../../../wip/scheduling-and-threads.md),
recorded on 2026-10-05. These are nested-VM measurements: the development host is
itself a KVM guest (Fedora 44, Linux 6.19.10, 8 vCPUs, virtualized i9-12900K), and
its own KVM has `halt_poll_ns=200000`. They are not owner-host or ThinkPad results.

Task 6 changes no caller's CPU: every `kmalloc()` and `kfree()` still runs on the
BSP in a normal boot. The timings check that the heap and growth locks and the
new arena cost nothing measurable, and that the heap grows the same way. The
stress run checks the any-CPU paths, which no committed code uses yet.

## Configuration

| Item | Before | After |
| --- | --- | --- |
| Pyxis | main `fe1f5cd` (task 5) | this PR at `60114aa`; repeats at `2c41360` and `35fbc29` |
| Kernel ELF SHA-256 | `d83e7137…a850f234` | `ac150abc…4c72f6ad8d`; repeats `b2ef7f2c…1f97379b` and `a5477a08…00edd34a` |
| Pins | fs `d352c7e`, userspace `2f01df6`, ports `36d952e`, lwIP `a1aadb9` | same |

`2c41360` only stores the arena counter in the stats record as the cursor moves,
instead of computing it in each snapshot, so that GDB can read it. `35fbc29`
refuses growth that cannot fit the free frames (see
[after the review fix](#after-the-review-fix)). One 4-CPU set was repeated on
each.

The setup matches the [task-5 record](../smp-task5/README.md#configuration): a
default `make -j16 image` (Development, Read-only and Remote spaces), QEMU 10.2.2
with KVM, Q35, `-cpu max` and 256 MiB. Devices are virtio-net, virtio-rng and a
blank USB stick on `qemu-xhci`. Each command ran in a remote session, one after
another. After boot and after each group of rows, a GDB batch attach read
`'kernel/mm/heap.c'::stats`; no debugger was attached during timed work.

The RAM rows write to `home://`, the RAM file system:

- `iobench write home://growN.bin` (a new file per process)
- `iobench copy app://share/iobench.bin home://copyN.bin`
- `iobench read home://iobench.bin`, after one `cat` copied the 1 MiB fixture
  there

RAM-file backing is heap storage that doubles as a file grows. This was the only
heap growth in the [task-1 baseline](../smp-task1-baseline/README.md#kernel-heap-growth).

## Results

Every sample exited 0 and verified, with no allocation failures. Values are
medians with min–max ranges in ms, five fresh processes per row. For the RAM
rows, each process's own median transfer is one sample.

| Workload | 4 CPUs before | 4 CPUs after | 1 CPU before | 1 CPU after |
| --- | ---: | ---: | ---: | ---: |
| `allocbench heap` | 12.464 (12.371–12.699) | 12.548 (12.467–12.908) | 14.275 (13.247–14.683) | 14.369 (13.095–14.478) |
| `allocbench heap --mixed --size 4096 --live 256` | 48.672 (48.464–49.146) | 48.674 (48.384–49.506) | 56.858 (56.016–57.030) | 56.691 (56.145–56.964) |
| `allocbench growth` | 52.672 (52.338–56.070) | 52.324 (51.777–53.548) | 52.400 (51.930–55.029) | 52.666 (52.055–55.578) |
| `allocbench pages` | 37.329 (37.009–38.313) | 37.651 (37.020–38.932) | 37.891 (37.286–38.370) | 38.347 (37.865–39.633) |
| RAM write, transfer | 8.082 (6.661–8.866) | 8.579 (7.320–10.975) | 8.534 (6.255–8.789) | 10.479 (6.871–12.170) |
| RAM copy, transfer | 10.614 (7.340–11.089) | 8.969 (7.634–10.524) | 7.966 (6.943–9.688) | 9.402 (7.541–11.765) |
| RAM read, complete consumption | 0.335 (0.327–0.341) | 0.329 (0.326–0.334) | 0.328 (0.327–0.333) | 0.328 (0.326–0.332) |

The 1-CPU RAM write after the change looked slower, so both 1-CPU boots were
repeated. The 4-CPU set was repeated on `2c41360` and `35fbc29`:

| Repeat | 1 CPU before | 1 CPU after | 4 CPUs, `2c41360` | 4 CPUs, `35fbc29` |
| --- | ---: | ---: | ---: | ---: |
| `allocbench heap` | — | — | 12.491 (12.281–12.715) | 12.456 (12.301–12.759) |
| `allocbench heap --mixed …` | — | — | 48.743 (48.645–49.257) | 48.334 (48.258–48.642) |
| `allocbench growth` | — | — | 52.207 (51.560–52.516) | 52.657 (51.336–53.286) |
| `allocbench pages` | — | — | 37.421 (36.828–37.723) | 37.136 (37.026–38.015) |
| RAM write, transfer | 12.275 (6.805–12.731) | 9.778 (7.127–12.292) | 8.035 (6.896–8.653) | 10.792 (10.469–11.031) |
| RAM copy, transfer | 11.014 (8.555–13.025) | 9.249 (7.521–10.358) | 7.073 (7.033–10.752) | 9.769 (8.109–11.357) |
| RAM read, complete consumption | — | — | 0.326 (0.326–0.339) | 0.334 (0.331–0.341) |

Only the RAM rows of the 1-CPU repeats are summarized here; the full output has
every row.

RAM transfers on this VM vary between about 6 and 13 ms whichever kernel runs,
so the RAM differences are noise. Every other difference is within the spread
between runs too.

### Heap growth

Pools and pool bytes from the GDB reads:

| Boot | After boot | After allocation rows | After RAM rows |
| --- | --- | --- | --- |
| 4 CPUs before | 15 / 5,160,960 | 15 / 5,160,960 | 29 / 31,584,256 |
| 4 CPUs after | 15 / 5,160,960 | 15 / 5,160,960 | 29 / 31,584,256 |
| 1 CPU before | 15 / 5,160,960 | 15 / 5,160,960 | 29 / 31,584,256 |
| 1 CPU after | 16 / 5,423,104 | 16 / 5,423,104 | 30 / 31,846,400 |
| 1 CPU before, repeat | 19 / 9,342,976 | 19 / 9,342,976 | 30 / 33,808,384 |
| 1 CPU after, repeat | 16 / 5,423,104 | 16 / 5,423,104 | 30 / 31,846,400 |
| 4 CPUs after, `2c41360` | 16 / 5,423,104 | 16 / 5,423,104 | 30 / 31,846,400 |
| 4 CPUs after, `35fbc29` | 15 / 5,160,960 | 15 / 5,160,960 | 29 / 31,584,256 |

The pool count after boot varies between boots of the same kernel: main gave 15
and 19. The GDB read happens during late boot work, after the boot USB
enumeration, so the pools that already exist depend on timing. That has not been
traced further.

In every boot, the allocation rows added no pool. The RAM rows added 14 pools,
except in main's 1-CPU repeat, which started with 19 and added 11. On `2c41360`,
`arena_bytes` equalled `pool_bytes` at every read, with nothing retired. Heap
growth behaves as before, now in the arena. `35fbc29` gave the same, with nothing
retired. At the boot log line, the
general kernel VM held 80 range records and 1,680 backed pages before the change,
and 75 records and 1,060 pages after it. The difference is the five boot pools
(2,539,520 bytes, 620 pages) moving to the arena, which reported
`arena used=2539520 retired=0`.

Full output: `pre4-output.txt`, `post4-output.txt`, `pre1-output.txt`,
`post1-output.txt`, `pre1b-output.txt`, `post1b-output.txt`,
`post4b-output.txt` and `post4c-output.txt`. Each starts with the GDB reads. Before `2c41360`, the
`arena_bytes` field read 0 there; only the boot log and snapshots carried it.

## Stress run (not committed)

[stress.patch](stress.patch) is a throwaway patch; it applies to this PR's code
with `git apply` and is not part of the tree. The runs in this section used an
earlier version on `60114aa`. The file here is the version for `35fbc29`, which
adds the injected failure described [below](#after-the-review-fix). When the
scheduler starts, every AP spends six seconds on rounds over 64 slots. Meanwhile the BSP continues booting:
USB enumeration, display start and network setup all allocate. Each round:

1. Checks and frees the slot's previous block.
2. Allocates a new block of 16 B to 64 KiB. Every 512th block is 1–4 MiB, which
   forces growth.
3. Fills it with a per-CPU pattern.

At the end, each AP checks and frees its remaining blocks and runs TLSF's own
`tlsf_check()` under the heap lock.

Twenty seconds after start, when boot has finished, CPU 1 asks for 512 MiB. That
is more than the 256 MiB guest has, so growth maps frames until the PMM runs out,
then fails. CPU 1 then allocates 1 MiB. Afterwards, a remote session ran
`iobench write home://after.bin` and `allocbench growth`. Output is in
[stress-serial.txt](stress-serial.txt).

| CPUs | Rounds per AP | Pools after the six seconds | Failures | Pattern mismatches | `tlsf_check()` |
| ---: | --- | ---: | ---: | ---: | --- |
| 2 | 161,692 | 12 | 0 | 0 | 0 (consistent) |
| 4 | 158,232–159,731 | 24 | 0 | 0 | 0 on every AP |
| 4, with counters | 159,054–159,367 | 24 | 0 | 0 | 0 on every AP |
| 12 | 85,481–136,358 | 57 | 0 | 0 | 0 on every AP |

The 12-CPU boot ran 12 vCPUs on this 8-vCPU host, so its round counts vary.

- **Retry after waiting.** The second 4-CPU run counted what happened after
  taking the growth lock, from boot through the 512 MiB request. Twelve
  allocations succeeded on the retry, because another CPU's new pool already had
  room. Twenty-four went on to grow the heap: 23 added pools, and the 24th was
  the failed 512 MiB request. With `heap_init()`'s pool, that makes the 24 pools
  reported. A third 4-CPU run with a failure counter confirmed that the 512 MiB
  request was the only failed growth (16 retries satisfied, 24 growths).
- **Failed growth.** In each run, the 512 MiB request returned NULL. Retired
  bytes equalled exactly the arena advance: 112,185,344 bytes at 4 CPUs,
  137,142,272 at 2, and 86,462,464 at 12. That is the mapped prefix only, not
  the roughly 544 MiB pool the request needed. TLSF still checked consistent, and
  the following 1 MiB allocation succeeded from an existing pool.
- **Afterwards.** The remote `iobench` write and `allocbench growth` completed
  normally in every run, and the BSP logged no allocation failure while the PMM
  was briefly exhausted.

### After the review fix

The #425 review found that a growth larger than the free frames mapped page by
page until the PMM was empty, then retired everything it had mapped. Any program
able to write a RAM file can make such a request, for example by filling
`home://`, and repeating it would use up the arena. The runs above show the
cost: each 512 MiB request retired 86–137 MiB.

`35fbc29` compares the pool's pages, plus an allowance for page tables, with the
free frames before mapping. The stress patch's 512 MiB request is now refused.
To still exercise retirement, the patch also injects one failure after 64
mapped pages into the next growth, a 16 MiB request, and then repeats that
request.

| CPUs | Rounds per AP | Failures, mismatches | 512 MiB request | Injected failure | Repeat 16 MiB |
| ---: | --- | --- | --- | --- | --- |
| 4 | 159,247–159,731 | 0, 0 | NULL; arena and retired unchanged | NULL; retired 262,144 bytes, arena advanced by exactly that | Allocated at the next arena address, in a new pool |
| 12 | 87,240–135,050 | 0, 0 | NULL; arena and retired unchanged | Same | Same |

In both runs, `tlsf_check()` stayed 0 throughout. At 12 CPUs, 112 allocations
succeeded on the retry after waiting for another CPU's growth. Afterwards the
remote workloads ran normally.

**Control.** The same patch, with `kmalloc()`'s first attempt and `kfree()`
unlocked, panicked within seconds on 4 CPUs. Several CPUs failed the TLSF
assertion `second level bitmap is null` (`tlsf.c:562`) at once. So the stress
does detect a missing heap lock.

## Not exercised

- No caller in this PR allocates off the BSP; the evidence is the stress run.
- A real race, in which another CPU takes frames during a growth that passed the
  free-frame check, was not produced; the stress injected that failure instead.
- The memory-pressure notice sent by a refused growth was not observed
  separately.
- The retired-range path when `tlsf_add_pool()` rejects a pool did not run. Pool
  sizing makes that rejection unexpected.
- Running out of the 256 GiB arena itself was not exercised.
- No native ThinkPad run. Task 8 covers native validation.
