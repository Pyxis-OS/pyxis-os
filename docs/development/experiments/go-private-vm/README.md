# Sparse private-VM foundation

Measured baseline: Pyxis `2480d10f`. The reviewed implementation source is
`c2058ec2` and `0dc102e8`, based on `28ff8214`; the older measured baseline is
retained here as the revision actually run, not relabelled as the branch base.
The builder was `pyxis-llvm23.1.3-3d69e83`. Both normal images built with
`make -j16 image`. No dependency pins changed.

## Configuration and checks

Nested QEMU/KVM, `-cpu max`, 2 GiB RAM, OVMF, four CPUs; repeat the functional
probe with one CPU. The active clock was HPET, without invariant TSC. A temporary
native bundle requested memory/manage, threads/manage and clock/read+sleep;
its source and captures remain outside the repository. Normal image assembly
was also checked without that qualification overlay.

Both CPU configurations passed sparse aligned reserve/release, mixed commit,
zeroing, interior decommit/recommit, a distant committed island, whole release,
stale identity and range refusals. Guarded native contexts remained eager:
unpinned guarded identities refused updates; pinned contexts refused updates
and release. A sparse identity refused native stack registration. Decommit woke
a sibling's address wait with BAD_BUFFER, and request bytes stored inside the
decommitted page were safely captured before mutation. Committing 4 GiB on a
2 GiB guest returned NO_MEMORY, preserved existing sentinel backing, and allowed
a subsequent small commit and whole release. The bundle then exited successfully.

Debugger inspection on four CPUs compared a 1,097.125 MiB, 64 MiB-aligned empty
reservation before/after:

| Counter | Before | After |
| --- | ---: | ---: |
| VM reserved pages | 319 | 281183 |
| VM backed pages | 317 | 317 |
| PMM allocated frames | 5099 | 5099 |
| PMM free frames | 486328 | 486328 |
| Wait-token storage bytes | 2536 | 2536 |
| VM metadata bytes | 3288 | 3416 |
| VM extent records | 0 | 0 |

The two additional range records cost 128 bytes of VM metadata. This excludes
the private-allocation ownership record, heap headers and page tables. No data
frames, page tables or wait tokens were allocated for the empty span. A separate
decommit stop observed one registered waiter becoming zero before return.
Debugger stops were not timing samples. The debugger-only boot used a
VirtIO-SCSI firmware CD-ROM after stock host QEMU crashed in AHCI before Caelum.

## Eager-path comparison and limits

Existing workload, three samples per boot where completed:

```sh
allocbench pages --size 1048576 --live 4 --rounds 16
```

Each completed sample made 64 allocations and 64 releases, peaked at 4 MiB
and reported no allocation failures. Interleaved boots gave:

| Boot | Elapsed samples (ms) | Clock-read samples (µs) |
| --- | --- | --- |
| A1, baseline | 115.016, 98.235, 111.991 | 35.583, 37.175, 46.709 |
| B1, candidate | 132.440, 132.467, 128.734 | 36.193, 32.436, 38.737 |
| A2, baseline | 150.147, 150.034, 149.387 | 43.961, 42.105, 60.781 |
| B2, candidate | 408.987, 421.538 | 154.526, 161.290 |

B2's remote session disconnected before its third sample. A stale debugger
spinning on a crashed QEMU was also found after B1 and stopped. HPET-read cost
and host contention varied substantially; these samples establish successful
ordinary allocation, **not a performance improvement or regression**. They do
not measure Go startup. That measurement still requires the Go target and a
recorded clock source.

Sparse operations yield between steps of at most 256 backing pages or extent
records. The existing global address-wait withdrawal scan remains an
interrupts-off tail. Empty page tables remain until process destruction;
trimmed extents can retain bounded token capacity, and extent lookup is linear.
Review follow-up (2026-10-22): the normal image rebuilt and the existing native
probe passed again on one and four vCPUs. GDB on four vCPUs confirmed sparse
identity release on the VM worker, and eager guarded release (pinned refusal and
unpinned success) inline in the request executor. Each observed a drained
mutation with interrupts off. These were correctness checks, not new timing
samples; the table above remains the original HPET experiment.

There is no native hardware qualification in this record. See the
[memory reference](../../../kernel/memory.md) for the implemented contract.
