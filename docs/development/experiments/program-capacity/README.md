# Program image and initial stack capacity

Matched before/after observations for [program capacity](../../../kernel/program-loading.md),
recorded on 2026-10-09. The original 8 MiB candidate below was superseded by the
owner's 1 MiB decision after cost review; [the recheck](#revised-1-mib-default)
records the implemented default. These are nested-KVM results. Raw timing, JSONL, serial,
debugger and image captures remain in ignored `build/capacity-baseline` and
`build/capacity-after`; no benchmark infrastructure was added.

## Original 8 MiB candidate: inputs and configuration

The baseline is main `a2591c444c04b4d5e0f69221b59e8f6f6010c68b`, using exact-head
[CI #1514](https://git.internal/PyxisOS/pyxis-os/actions/runs/1514) artifacts.
The after kernel is `058158bbd3b9418b0f853d636537d1ee6887182c`, built with the
existing LLVM 23.1.3 compiler at `49e2c1a`. Verified SDK, userspace and ports
artifacts were reused; no compiler-container rebuild or dependency update was
needed. Dependency revisions are userspace `72f303d6`, ports `8b918c46`, fs
`b427df29` and lwIP `a1aadb91`.

Effective kernel configuration and the staged archives' 874 regular-file
contents compare equal. Cpio timestamps differ; archive bytes are not identical.

Both images stage `multiplexer = true` and `launch = true` in Development and
`launch = true` in Remote; tracked configuration and dependency pins remain unchanged. QEMU
10.2.2 uses Q35, nested KVM, `-cpu max`, 512 MiB, UTC RTC, one socket with one
or four cores and one thread per core. Standard VGA presents 1280x800. VirtIO
SCSI CD boots the ISO to avoid the installed QEMU's AHCI issue; VirtIO RNG and
modern VirtIO networking use the user backend with host loopback TCP port 24567
forwarded to guest `10.0.2.15:2323`. Matching raw `/usr/share/OVMF/OVMF_CODE.fd`
and `OVMF_VARS.fd` use fresh writable variables per boot. Logging is `info`;
memory profiling is off. The debugger is detached during timing.

The [task 1 launch command](../threads-task1/README.md#inputs-and-configuration)
applies with `build/capacity-baseline` or `build/capacity-after` substituted for
its image, variables and serial paths.

## Launch and allocation cost

After a 16-child warmup, run three remote sessions per CPU count. Each executes
the existing Lua loop below, then `exit`; the host machine client reports
completion and final group drain:

```text
lua -e 'for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'
exit
```

Use the [task 1 host timing command](../threads-task1/README.md#workloads-and-results)
with `build/capacity-baseline/launch-4-1.seconds` and `.jsonl` output paths,
substituting the revision, CPU count and repetition as appropriate. Separately
run these existing commands three times each in fresh processes:

```text
allocbench growth --size 65536 --live 128
allocbench pages --size 65536 --live 16 --rounds 64
```

Every launch sample completed 1,024 children. Growth reported 128 allocation
attempts and releases; pages reported 1,024 each, with zero failures. All samples
exited normally with status zero and final `drain=complete`. Values are medians
with minimum–maximum ranges over three samples:

| Workload | 1 CPU before | 1 CPU after | 4 CPUs before | 4 CPUs after |
| --- | ---: | ---: | ---: | ---: |
| 1,024 launches, whole remote session (s) | 3.06 (3.03–3.07) | 10.53 (10.52–10.56) | 3.50 (2.91–3.54) | 11.30 (10.80–11.34) |
| Allocation growth (ms) | 10.747 (8.590–10.920) | 10.580 (9.748–10.813) | 8.452 (8.184–9.524) | 8.753 (8.232–8.896) |
| Allocation pages (ms) | 85.180 (84.811–85.625) | 85.137 (85.003–85.233) | 77.214 (76.809–78.038) | 78.498 (76.648–79.637) |

Host timing includes root/session startup, Lua interpretation, transport, exit
and final group drain, at hundredth-second resolution. It does not isolate
loader or teardown latency. Allocation intervals follow
[allocbench's contract](../../allocation-profiling.md), excluding final process
teardown; clock calibration is not subtracted. Allocation ranges overlap.
Launch ranges do not: median complete-session time rises about 3.4x at one CPU
and 3.2x at four CPUs. Eager allocation, zeroing and reclamation of the larger
stack is an inspected source of additional work; these samples do not isolate
its share or establish owner-host or physical-hardware performance.

## Resident session memory

In each four-CPU boot, select Development and split twice with Ctrl+B then `%`.
Sample three idle shell panes, then run `head -c 1 | cat` in the focused pane
and sample while both children await input. Type `a` without Enter, wait for the
pane prompt and sample again. Geometry and split order match; mux retains
allocated history widths when panes shrink.

Read-only GDB inspection used the matching ELF and no function calls. PMM,
kernel heap and heap-growth locks were clear at every snapshot. Walking mux's
execution-group capabilities and member lists counted mux plus three shells,
or those four processes plus two pipeline children. Summing their `RANGE_BACKED`
VM pages measures process-owned backing, excluding borrowed display mappings,
page tables and kernel storage. The reserved guard contributes no backing.

| Stage | Session processes | Owned VM backing before/after (MiB) | Whole-guest PMM allocations before/after (MiB) |
| --- | ---: | ---: | ---: |
| Three idle panes | 4 | 6.234375 / 34.234375 | 47.8984 / 160.5234 |
| Two live pipeline children | 6 | 8.421875 / 50.421875 | 50.1680 / 176.8398 |
| Prompt restored | 4 | 6.234375 / 34.234375 | 47.9023 / 160.5273 |

Session backing increases exactly 7 MiB per live process: 28 MiB at idle and
42 MiB with the pipeline alive. Whole-guest allocated physical frames increase
by 112.625 MiB at idle and 126.671875 MiB during the pipeline. That PMM measure
includes all processes, private page tables, kernel storage and retained kernel
heap pools; it is neither host QEMU RSS nor a count of permanent boot
reservations. Allocatable capacity is identical at 108,244 frames. Kernel heap
pools retain 3 MiB before and 3.25 MiB after, explaining 256 KiB of the global
increase. The session walk is a bounded census, not a universal process registry.

The originally accepted fixed eager 8 MiB default therefore has a material memory
and short-command cost even for ordinary shells and tiny commands. On 2026-10-09
the owner revised that default to 1 MiB after reviewing these results. Larger
stacks will come from program-bundle manifests through a bounded load parameter,
without changing P1F or rebuilding the compiler container. That path is proposed,
not implemented by the capacity change.

## Original 8 MiB candidate: native capacity and cleanup

An ordinary `make -j16 image PREBUILT="sdk userspace ports"` passed with the
existing builder. Separate interactive validation used the same after kernel,
four CPUs and devices above, with **1 GiB RAM** to permit the maximum-span
fixture alongside the service stacks. Temporary native fixtures were added only
to that validation archive; those images and PMM counts are not the matched
512 MiB session measurements. Sources, executables and debugger captures remain
under ignored `build/capacity-fixtures` and `build/capacity-validation`.

The existing SDK compiler linked a native P1F with 2 MiB initialized data and
84 MiB BSS. Its serialized size is 2,155,163 bytes; three segment memory extents
produce a page-rounded span of **90,263,552 bytes**, well above the old window.
It checked initialized sentinels and every BSS page, touched a volatile 2 MiB
local stack frame, allocated and released 64 KiB of native private memory, and
exited normally with status zero. This is a capacity fixture, not native Clang.

At ordinary task preparation, GDB observed entry `0x400000`, returned stack top
`0x7ffffffff000`, startup at `0x1000`, and the private allocation later at
`0x3000`. Walking VM ranges showed disjoint image backing, one reserved guard
page at `0x7fffff7fe000` and 2,048 backed stack pages at `0x7fffff7ff000`.
Read-only physical page-table inspection through QEMU's GDB memory mode found
the guard PTE zero, user RX code, user RW/NX data and stack, and user read-only/NX
startup metadata. No inferior function was called. The program's stack frame
began at `0x7fffffdfef90`, inside the new range.

Normal BSP retirement reached `process_task_reclaimed` with the task link null
and its reclamation flag still set. Process destruction reduced allocated frames
from 62,957 to 38,815 before `process_control_complete` reported `PROCESS_EXITED`,
status zero. A separate fresh boot stopped at the boot-init `user_task_create`
call: that path also supplied `0x7ffffffff000`, startup at `0x1000`, and later
retired boot init normally with status zero.

Temporary P1F variants exercised admission boundaries:

| Input | Runtime outcome |
| --- | --- |
| Last segment enlarged to end at exactly first base + 256 MiB | Loaded; fixture checks and normal exit passed |
| Unrounded memory end one byte beyond that ceiling | Rejected after page rounding |
| Extra sparse page at first base + 256 MiB | Rejected over-span layout |
| Segment in the guard or stack, or trailing partial page rounding into the guard | Rejected collision |
| Tiny native images at `0x1000` and `0x7ffffffff000` | Both exited normally, preserving custom valid placements |

All invalid single-image attempts produced launch failure and left allocated
frames at 38,815, without a prepared child. The pipeline
`echo -n | boot://guard-collision.pxe` failed stage two with `CALL_BAD_REQUEST`
(status 4). Its first child was `PROCESS_PREPARING`, with prepared task storage;
batch rollback discarded it before publication. Allocated frames returned from
40,901 to 38,815 and batch count from one to zero.

Real memory pressure, without fault injection, came from a four-stage pipeline
of the exact-limit fixture. The third image exhausted physical frames after
mapping 62,788 of a 65,520-page segment. `vm_back` returned `MM_NO_MEMORY` and
freed exactly that mapped prefix; destroying the failed VM released its earlier
segments and page tables. Batch rollback then discarded two earlier prepared
children: count returned to zero and allocated frames returned exactly to
38,815. The shell reported stage-three `CALL_NO_MEMORY` (status 10), a subsequent
`echo` succeeded, and root exit completed with status zero and `drain=complete`.

Source inspection confirms the unchanged 16 MiB installed capture budget in
`include/abi/launcher.h`, HOST capture in `kernel/object/launcher.c` and the NPFS
size check in `kernel/fs/npfs.c`. This validation did not qualify installed large
payload capture, native Clang, compiler stack probing or physical-hardware cost.
All task-owned QEMU, GDB and remote clients were stopped after inspection.

## Revised 1 MiB default

Kernel `fb10f206a15202de807c0d2bb4a17c78f0ba4df5` changes only the stack size from
the candidate, retaining the high top, guard reservation, span/collision checks
and returned top. An ordinary image build passed. The earlier exact-main baseline
`a2591c44` was booted again for the brief comparison, holding the above compiler,
dependency bundles, devices, 512 MiB RAM, logging and workload constant. Kernel
configuration matches, and the recheck uses the exact same baseline initrd bytes.
These controlled captures precede integration of newer main/dependency changes;
they isolate the capacity revision rather than combine it with unrelated ports.
Captures stay in ignored `build/capacity-recheck` and `build/capacity-1m`.

Two repetitions per CPU count after the same 16-child warmup produced:

| 1,024-launch complete session | Fresh low-stack baseline (s) | Revised high 1 MiB stack (s) |
| --- | --- | --- |
| 1 CPU | 3.03, 3.04 | 3.22, 3.21 |
| 4 CPUs | 3.00, 3.04 | 3.54, 3.21 |

All samples completed 1,024 children, status zero and final group drain. Costs
return to the low-three-second scale, compared with the 8 MiB medians of 10.53
and 11.30 seconds. They do not become identical: these short rechecks are about
6% higher at one CPU and 6–18% at four CPUs, with too few samples to isolate
causes or establish confidence. High placement needs additional page tables;
its share of elapsed cost was not isolated. Allocation workloads were unchanged
and were not repeated in this brief check.

Fresh four-CPU boots repeated the same three-pane/pipeline sequence:

| Stage | Processes | Owned backing baseline/revised (MiB) | Whole-guest PMM baseline/revised (MiB) |
| --- | ---: | ---: | ---: |
| Three idle panes | 4 | 6.234375 / 6.234375 | 47.8984375 / 48.0234375 |
| Two live pipeline children | 6 | 8.421875 / 8.421875 | 50.16796875 / 50.30859375 |
| Prompt restored | 4 | 6.234375 / 6.234375 | 47.90234375 / 48.02734375 |

Owned backing returns exactly to baseline at every stage. Both retain 3 MiB of
kernel heap pools. Whole-guest allocation is 32 frames (128 KiB) higher at idle
and 36 frames (144 KiB) with children; source inspection of the high placement
accounts for two extra page-table frames per process. The census and PMM
definitions remain those above; these values are not host RSS.

Revised interactive four-CPU/1 GiB validation used the same 86 MiB-span native
fixture with its local frame reduced to 128 KiB (serialized size 2,155,323 bytes).
Initialized/BSS/private-memory checks and normal exit passed. GDB observed the
returned high top, 256 backed stack pages starting at `0x7fffffeff000`, and one
reserved guard at `0x7fffffefe000`; physical page-table inspection found its PTE
zero and the stack user RW/NX. BSP destruction released 22,346 frames before
normal process-control completion.

The exact 256 MiB fixture and custom low/top-page images passed again. Rounded
over-span, sparse over-span and collisions at the revised stack/guard addresses
rejected. Invalid-stage-two rollback returned 12,168 allocated frames to 11,878.
A four-stage maximum-span pipeline exhausted memory on stage four after 25,877
pages of its segment; partial unwind and three-child rollback returned exactly
to 11,878, followed by successful `echo`, root exit and final drain. These
validation captures stay in ignored `build/capacity-1m-validation`; their archive
and memory configuration differ from the matched resident comparison. All
task-owned QEMU, debugger and client jobs were stopped.
