# Kernel heap pool reclamation

C.10 qualification, 2026-11-02. Before: main `93bdcf0330`, userland
`36c0a49`, ports `02aec1f2`. After: that same composition with the kernel
pool-reclamation change. No userland/ports or public ABI changes.

Both full RAM-live images used the published `b33ad74-r1` compiler image.
QEMU q35/KVM, `-cpu max`, OVMF, 512 MiB, VirtIO block/net/RNG; one guest and
no own build at a time, nested in the shared Fedora VM. No native claim.

## Exhaustion and recovery

An outside-Git qualification program creates ordinary native pipes until actual
`CALL_NO_MEMORY`, retaining both ends in a dynamically grown vector, then closes
every end and exits. Remote waits for process-group cleanup before launching
`echo hello | cat | wc -c` and a checksum of the stock 1 MiB fixture. There is
no injected allocation failure or fixed product quota. GDB takes passive heap
and PMM snapshots before and after; no inferior calls or state edits.

| Image / CPUs | Pipes before refusal | Pool bytes after cleanup | PMM free frames after cleanup | Next pipeline |
| --- | ---: | ---: | ---: | --- |
| Before / 4 | 3,778 | 335,859,712 | 833 | launch fails, `CALL_NO_MEMORY` |
| After / 4 | 3,770 | 5,505,024 | 81,477 | `6`, all stages exit 0 |
| After / 1 | 3,777 | 6,029,312 | 81,516 | `6`, all stages exit 0 |

After four-CPU setup, PMM had 81,315 free frames; after one-CPU setup, 81,351.
Both recoveries reclaimed 330,469,376 additional heap-pool bytes. Pending and
quarantined bytes were zero, and the cleanup queues were empty. Both subsequent
checksums were `1625934143 1048576`. Every post-change command and final output
drain completed successfully. Refusal counts depend on incidental live storage;
they are observations, not admission limits. The TLSF pool identity costs one
pointer per block, included in actual allocation overhead.

## Ordinary workload cost

Each cohort runs one warmup and ten timed `pyxis.run({"bin://shell.pxb", "-c",
workload})` calls using Lua's existing monotonic clock. Timing includes job
cleanup and normal output. Three cohorts per boot; saved before/after images
alternated twice. `allocbench pages --size 65536 --live 16 --rounds 64` separately
timed 1,024 native backing allocations and 1,024 releases with zero failures.

| Workload, elapsed ms | Before repeat: cohort medians | After repeat: cohort medians |
| --- | --- | --- |
| Small shell (`exit 0`) | 3.207 / 3.431 / 3.164 | 2.994 / 2.976 / 3.451 |
| `echo hello \| cat \| wc -c` | 7.760 / 9.007 / 8.773 | 8.157 / 8.141 / 8.348 |
| `cat boot://share/iobench.bin \| cksum` | 9.125 / 9.410 / 9.277 | 8.839 / 9.084 / 9.131 |
| Native backing allocation/release run | 29.353 / 29.190 / 29.502 | 30.148 / 29.349 / 29.926 |

The initial before boot was substantially faster: shell medians 1.502–1.705 ms,
pipeline 4.366–4.544 ms, checksum 5.617–6.168 ms, backing run 23.134–23.477 ms.
Initial after was 2.915–3.460 / 7.814–8.300 / 9.113–9.235 / 29.338–29.658 ms.
The saved unchanged before image then reproduced the slower period. Its clock
call loop shifted from 208–223 ns to 412–473 ns; after repeat was 373–416 ns.
These controls demonstrate substantial VM timing variation across the first
pair. The alternating repeat does not show a distinct large ordinary-workload
regression; its backing-run median rose about 2%, within this limited nested
sample. No native CPU-cost or statistical-confidence claim is made.

The separate universal-pipe queue exhaustion sequence is qualified in the pipe
stack, where that interface exists. Timeout/quarantine correctness is source
reviewed; no TLB failure was injected. Physical pool reclamation leaves the
existing never-reused virtual arena and its separate follow-up unchanged.
