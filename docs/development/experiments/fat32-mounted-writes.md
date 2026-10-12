# Mounted FAT32 write qualification

Task 4 scratch qualification, 2026-10-12. The
[pre-implementation baseline](fat32-mounted-write-baseline.md) remains the
preserved control at parent `36bcd7e96d3a`, fs `30f625a236dd`, userland
`3daa24611e1e`. Current parent `48ca23ac7dd7` is based on main `4366eb28`,
with fs `414be0ac0b06` and userland `9f2dd48582bb`. The detailed held-object,
lease and S5 cases below ran before that rebase at parent `5e890ebb13b0`,
with the same fs engine and userland `57fca54cbbe5`; they are not exact-head
re-executions. The rebased-head read-only-attribute case, matched npfs runs and
final backing correction at `197ebfad` are identified separately. Main movement
includes #756's managed libc/runtime.

Use the [native mount reference](../../devices/fat32-mounts.md) and
[shared mounted engine](../../../fs/docs/fat32-mounted.md) for contracts;
this document records measured/manual results and their limits. Local inputs,
screenshots, GDB observations and logs are retained under
`build/fat32-task4/qualification/` and `build/fat32-task4/logs/`.

## Configuration and commands

Ordinary host tools, SDK/kernel, userland and image builds use the existing
published compiler; no compiler rebuild or external source/cache addition.
Host tools build with:

```sh
make -C fs -j16 BUILD="$PWD/build/fs-tools"
make -j16 image BOOT_MENU_TIMEOUT=5
```

Image builds run in the existing `pyxis-llvm23.1.3-2d23443` builder. Manual
QEMU boots use q35/KVM, host CPU, four CPUs, 1 GiB RAM, OVMF and modern
VirtIO block (`disable-legacy=on`), with VirtIO SCSI CD and unused audio disabled.
Private scratch ISO configuration selects:

```text
init=boot://init-fat32-mount-write.pxe boot.install=1
```

The trusted selector opens explicit disk 1 / GPT entry 1 RW after releasing its
whole-disk claim. Only copied disposable GPT/FAT images are attached. The
[fixture geometry](fat32-mount-baseline.md#host-shared-reader) is a 64 MiB FAT32
partition at sector 2048, 512-byte sectors/clusters, two FATs and 129022 clusters.
The existing npfs workload uses the baseline's disjoint-partition scratch image.
No owner ESP, physical USB/NVMe/SD or native hardware is written.

Use existing browser commands `create`, `mkdir`, `hold SLOT ro|rw /PATH`,
`hwrite SLOT OFFSET = LITERAL`, `hread SLOT OFFSET LENGTH`, `resize SLOT SIZE`,
`replace /FROM | /TO`, `remove`, `drop`, `begin`, `end ID`, `sync`, `close`,
`retire`, `open` and `state`. Native calls are submitted manually; console/GDB
inspection and independent mtools reads establish the results below. S5 cases
use the ordinary local power overlay and a hardware GDB breakpoint at
`nvme_shutdown` to observe retained FAT state after sealing, then continue to
actual QEMU power-off. Partition copies are checked with `fsck.fat -n`; no
repair is performed. No new harness, fault hook, boot/output automation or klog
line is added.

## Executed native mutations and lifetime

Earlier parent `5e890ebb13b0` / fs `414be0ac0b06` / userland `57fca54cbbe5`:

- Replacement retained the held destination FILE's original `victim` content.
  The moved source and another opened reference retained native object `3335`;
  creating a new file under the old source name gave distinct object `3348`.
  Dropping the held victim reclaimed its chain.
- Shrink followed by regrowth returned `ABC` plus six zero bytes, independently
  read through mtools. After unlink, a write through the retained source FILE
  succeeded; its other held reference read `AZC` plus the zero tail. The old
  reusable namespace slot did not redirect the held FILE to a new object.
- Normal RETIRE after root closure still returned BUSY (`9`) with a held FILE.
  Dropping that FILE permitted RETIRE OK. Remount assigned a new domain (`3332`
  in that separate run); no retired-domain identity continuity was inferred.
- A repeated BEGIN returned BUSY (`9`) without changing the existing deadline.
  After lease expiry, a write returned TIMED_OUT (`17`). END naming interval
  `3334` acknowledged expiry and allowed a subsequent write. This executes the
  same-owner expiry/acknowledgement path; it does not measure another process's
  interval contention or process-stop drainage.
- Ordinary read-only child `cat` captured `RUN.PXE` from the dirty RW mount and
  executed content that printed `hello fat32`. The child received attenuated
  read-only roots rather than disk/control or mutation authority.

At rebased parent `48ca23ac7dd7` / fs `414be0ac0b06` / userland `9f2dd48582bb`,
the FAT read-only attribute on `HELLO.TXT` refused write, resize and removal with
READ_ONLY (`15`). GDB observed `dirty_owned=false`, mount workspace charge
2708496 bytes and two engine objects. This is an accounted-backing snapshot,
not total resident memory or a transient peak. A new empty file grew to 1 MiB;
a four-byte beyond-EOF write succeeded. Independent mtools extraction verified
exactly 1 MiB of zeros followed by `tail`. A 1 GiB resize returned LIMIT (`11`),
preserving size 1048580 and identity. Creating `bad:name` returned BAD_REQUEST
(`4`). The full image SHA-256 was unchanged across both refusals. Explicit sync,
data closure, RETIRE and quit left zero FAT instances/workspace and empty live/
retired lists; read-only fsck reported a clean volume. The original read-only
file still contained `hello fat32`.

The final code correction (`197ebfad`, unchanged dependency pins) charges old and
replacement backing together until successful replacement. Its fresh QEMU run
created a second file and repeated the 1 MiB growth, beyond-EOF write and LIMIT
refusal. At a hardware breakpoint at `kmalloc`, before allocating the 3789296-byte
replacement workspace, GDB observed a charge of 6496204 bytes including the old
2706896-byte workspace and retained chains. After growth, accounted backing was
3797504 bytes. Independent mtools extraction again verified the zero gap and
`tail`. Explicit sync/RETIRE/quit again left zero instances/workspace and empty
live/retired lists; `fsck.fat -n` reported a clean 7-file/4328-cluster volume.
These are scoped backing observations, not a total process/system resident peak.

## Actual S5 sealing with retained references

The following two cases executed at the earlier parent/userland revision above.
Both reached `nvme_shutdown` after FAT sealing and continued to successful S5;
neither terminated sessions or retired the FAT mount as a prerequisite.

**Named live file and active interval.** `LIVE.TXT` persisted nine bytes while
its FILE and a publication interval remained held. Post-seal GDB observed two
data nodes, active interval, `mount.retired=false`, `writer.dirty_owned=false`
and `volume.clean_shutdown=true`. Both FAT copies had the clean bit set.
The subsequent read-only host check was clean: five files, six allocated
clusters (`clean-power-fsck.log`). This demonstrates actual retained-view S5
sealing, not power-loss/crash-atomicity durability.

**Detached held chain.** Post-seal GDB observed three data nodes,
`dirty_owned=true`, `clean_shutdown=false`, `mutated=true`, `writer.stopped=false`
and `mount.retired=false`. Both FAT entry-1 words were `0x07ffffff`. S5 succeeded
with the detached chain still retained. The later `fsck.fat -n` reported one
unused cluster and the dirty bit; its proposed reclamation changed the calculated
free count from FSInfo 128781 to 128782. It explicitly left the image unchanged
(`orphan-fsck.log`). The dirty/orphan result is the accepted held-chain power
policy, not a clean fsck result or completed reclamation.

A failed power attempt followed by resumed mutation was **source-reviewed only**:
a confirmed seal clear updates the engine's flag/geometry snapshot; the next
mutation prepares mirrored dirty-mark images and flushes them before its first
data/FAT/directory effect. No failed-power resumption was executed. Uncertain
write/flush retention was not injected or runtime-qualified; source inspection
is not failure evidence.

## Matched host costs

Preserved control and current fs `414be0ac0b06` tools run in alternating pairs on
cached disposable media. Existing commands are unchanged:

```sh
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 check
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 list /
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 read /DIR/CHILD/HELLO.TXT
```

The raw command sequence and per-session copied image are the
[baseline workload](fat32-mounted-write-baseline.md#existing-shared-reader-and-raw-writer).
Python `time.perf_counter` measures full subprocess elapsed time, including
startup/captured output and raw-session fsync. Seven paired samples per operation,
Linux VM milliseconds; all samples and outliers remain in `host-interleaved.json`:

| Operation | Control median / min–max | Current median / min–max |
| --- | ---: | ---: |
| Whole-volume check | 1.481 / 1.389–8.666 | 1.475 / 1.335–1.626 |
| Root list | 0.581 / 0.503–1.061 | 0.574 / 0.489–0.963 |
| Small file read | 0.530 / 0.485–1.141 | 0.691 / 0.493–0.971 |
| Raw create/append/extend/replace/check | 51.884 / 48.132–59.792 | 53.196 / 49.211–54.549 |

A further 15 alternating read pairs gave control 0.505 / 0.463–0.899 and current
0.504 / 0.453–0.919 ms (`host-read-followup.json`). An image build briefly
coincided with the start of that follow-up; the entire batch is not claimed
quiet. The initial read median increase did not reproduce in this follow-up.
These are startup/VM/host-fsync costs, not mounted mutation throughput or
physical-media performance.

## Existing npfs path after main movement

Profiling is off. Reuse the existing 32 KiB input and command:

```text
iobench read system://iobench-small.bin --bytes 32768 --rounds 5
```

Run one warm-up and five verified passes per fresh boot, in order control A,
current A, control B, current B. GDB is detached and builds are off while timing.
All ten measured passes per revision verify the same hash, with no failures.
`build/fat32-task4/logs/npfs-interleaved.json` contains the full phase samples;
clock-call overhead is not subtracted. Combined milliseconds:

| Phase | Control median / min–max | Current median / min–max |
| --- | ---: | ---: |
| Open | 0.442 / 0.427–1.163 | 0.518 / 0.427–0.615 |
| Payload | 3.943 / 3.878–4.837 | 3.980 / 3.891–4.806 |
| Complete pass | 5.270 / 5.222–7.075 | 5.516 / 5.234–6.391 |

The aggregate current complete-pass median is 4.7% higher and is retained as a
measured difference. Pair A medians were control 5.276 / current 6.105 ms;
pair B was control 5.264 / current 5.252 ms. Pair A's slowdown did not reproduce
in pair B. Attribution is unresolved: the rebased comparison includes managed
libc/runtime and other main movement, rather than isolating FAT32 code alone.
Overlapping ranges or the second pair do not erase the aggregate increase.

## Qualification limits

These are manual nested-VM, host-tool and source-inspection results. They do not
qualify native ESP writes, physical USB/NVMe/SD, torn sectors, hardware power loss,
crash-atomic rename or failure recovery. No automatic retry, repair or additional
milestone work follows from these observations. The final kernel/SDK/default image
build passed with the existing compiler; submitted-head CI is reported in the PR.
Build CI does not prove the runtime contracts. All task-owned QEMU/GDB jobs are
stopped. Task 5 remains unassigned; adversarial review and any native run go
through the owner.
