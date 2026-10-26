# VM activity and user-buffer lease qualification

## Pre-code baseline

2026-10-10, clean main A `476b2a332c05e8cbe577b3d36bf08e24cd3e0a27`, after
merged #684. All four bundles from exact-head successful
[#1802](https://git.internal/PyxisOS/pyxis-os/actions/runs/1802) verified; ordinary
`make -j16 image PREBUILT="kernel sdk userspace ports" REMOTE_BEACON=t14` passed
with LLVM 23.1.3 builder `49e2c1a`. Cached owner-mirror AX200 firmware reused;
initial offline assembly lacked that cache and failed before boot. No upstream
fetch or compiler rebuild. Userland `796087b11fab0a89257b55ec047479c23a3e40cb`,
ports `43eec1937c0db8341b0ad28bcda1352b29f55e28`, fs
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

Frozen SHA-256: ELF `e9fb1fced62e12657b96ffc157545dca7a07099612b0fda0795a33b1bc3ca8a6`,
ISO `97bd7379b790ecde175ecbc4df93cd4c13c01cdd17a3322fe14ad692551cac9f`,
initrd `fe140ac9c0ca86076bf3fc91a943abf3099260b63b88df7bf55e971f5040ec6b`.

QEMU 10.2.2, Q35/nested KVM, CPU host, 1 or 4 cores/one thread per core,
2 GiB, UTC RTC, raw OVMF/fresh VARS, modern VirtIO SCSI CD/RNG/net, standard VGA,
display none, info logs/profiling off. No debugger/build during measurements.
Reverse client `--machine --no-shell-echo --listen t14 --beacon-address 127.0.0.1
0.0.0.0 2323`, UDP NAT forward 2324. Loopback beacon is QEMU's NAT adaptation;
owner native commands omit it.

Each boot: call/send 64/4096 bytes, 256 messages/100 rounds; 16-child launch
warmup then three 1024-child Lua launch sessions; three `allocbench pages --size
65536 --live 8 --rounds 100` processes. All 800 timed IPC passes verified with
zero failed CALLs/rejected SENDs; all FINAL records 0/complete. Each page run had
800 allocations/releases, zero failures. All launches reported expected counts.
Launch wall time includes discovery/transport; clock calibration is not subtracted.

Median (range), milliseconds except launch seconds:

| Workload | 1 CPU A1 | 4 CPU A1 |
| --- | --- | --- |
| CALL64 complete | 84.023 (83.505–87.398) | 75.047 (67.678–106.626) |
| CALL4096 complete | 84.257 (83.613–85.429) | 72.759 (67.156–98.132) |
| SEND64 admit / complete | 1.181 (1.168–1.384) / 12.957 (12.734–13.467) | 1.193 (1.071–1.905) / 12.186 (10.656–21.729) |
| SEND4096 admit / complete | 1.244 (1.221–1.714) / 13.122 (12.845–18.684) | 1.257 (1.180–2.140) / 11.944 (11.307–17.483) |
| Pages | 64.909 (64.668–65.446) | 59.639 (58.519–60.369) |
| 1024 launches, seconds | 3.15 (3.13–4.29) | 3.19 (3.13–4.10) |

Clock calibration: 32,185–37,541 ns/read across the IPC invocations. Nested-VM
costs are not native performance. Raw captures in ignored
`build/task4-baseline/{1,4}-A1`; VMs/clients stopped before implementation.
Later comparison and native evidence will be recorded separately.

## Implemented comparison

Frozen B `9c556ae6` built ordinarily from source with the same compiler, config,
SDK/userland/ports bundles and byte-identical initrd. ELF SHA-256
`7076583e14b80194e1c51bf174e6cb6075c4722961ff32bc3dc160e6ae098771`, ISO
`5f8e942f889b554a59d0f9342f8cd5c0cce57f232ed4c5b9c52ff9eb6ccefeb5`.
Fresh boots, the same commands/configuration above, no debugger or build during
timings: 1 CPU A2/B1/A3/B2; 4 CPU A1/B1/A2/B2. All 3,200 timed IPC passes
verified, all 24 page runs had 800 allocations/releases and zero failures, all
24 launch sessions reported 1,024 children, and all FINAL records were 0/complete.
Eight boot workload sets also included an untimed pass per IPC workload; eight
launch warmups reported 16 children. Clock calibration 32,185–36,961 ns/read.

Per-boot medians, milliseconds except launch seconds; the chronological columns
are A/B/A/B. IPC has 100 samples per cell, pages and launch three:

| 1 CPU | A2 | B1 | A3 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 complete | 84.182 | 83.967 | 83.862 | 84.090 |
| CALL4096 complete | 84.262 | 84.233 | 83.794 | 84.370 |
| SEND64 admit / complete | 1.184 / 13.009 | 1.199 / 12.997 | 1.176 / 12.911 | 1.207 / 13.071 |
| SEND4096 admit / complete | 1.237 / 13.084 | 1.246 / 13.096 | 1.228 / 13.060 | 1.253 / 13.146 |
| Pages | 66.361 | 184.172 | 64.920 | 184.128 |
| 1024 launches, seconds | 4.39 | 3.13 | 4.12 | 3.15 |

| 4 CPU | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 complete | 75.047 | 95.291 | 101.352 | 99.641 |
| CALL4096 complete | 72.759 | 95.344 | 95.941 | 95.411 |
| SEND64 admit / complete | 1.193 / 12.186 | 1.209 / 15.391 | 1.189 / 16.346 | 1.215 / 14.348 |
| SEND4096 admit / complete | 1.257 / 11.944 | 1.272 / 15.553 | 1.252 / 16.002 | 1.276 / 14.604 |
| Pages | 59.639 | 421.901 | 59.670 | 260.007 |
| 1024 launches, seconds | 3.19 | 4.10 | 3.17 | 3.13 |

Across the comparison, 1-CPU CALL ranged 83.244–91.475 ms and SEND completion
12.726–18.574 ms; 4-CPU CALL 67.156–126.894 ms and SEND completion
10.656–25.018 ms. SEND admission ranges overlap (1 CPU 1.164–2.231 ms,
4 CPU 1.071–2.370 ms); B medians are slightly higher. Launch ranged 3.11–4.76 s.
These shared-host results do not establish an IPC/launch regression or absence
of native cost. Raw JSONL and decoded median/range data remain in ignored
`build/task4-{baseline,after}`; no samples were filtered.

Pages show a clear cost: A 64.876–70.956 / 58.519–59.723 ms versus B
183.188–184.513 / 258.901–423.150 ms on 1/4 CPUs. MEMORY formerly mutated locally;
each allocation/release now suspends its caller, departs the private root and
makes a BSP request/reply round trip. A preliminary worker-only B `c06bc146`
added a second handoff even when already drained (1 CPU 618.023–1344.957 ms,
median 838.523). `4937b053` removes that unnecessary handoff: ready transactions
execute directly in the BSP executor, preserving the same gate and final-copy
ownership; unready ones remain asynchronously drainable. Preliminary artifacts
and samples are retained in `build/task4-preliminary`. Remaining round-trip cost
comes with the accepted BSP ownership here; the native comparison below
measures its practical cost. No allocator batching/API change.

## Interactive inspection and limits

QEMU's GDB, with matching ELF and injected calls disabled, inspected 1/4-CPU
MEMORY: MUTATING on BSP/kernel CR3 with zero roots/borrowers; caller resumed in
REPLY on its private CR3, exactly one root and no borrowers; final reply returned
OPEN with no pending request or mutator. `9c556ae6` reconfirmed the ready path's
`request_worker -> finish_mutation` stack on both CPU counts; the 4-CPU caller
resumed on CPU1. Earlier `0e3305db` inspected the asynchronous worker path,
request-reference cleanup before final copy and nested FILE leases (2 -> 1, IF=0).
Normal allocbench and pipebench completed after debugger detach.

On `0e3305db`, remote provider reads, pipe transfer, RAM create/read/rename/remove
and capture/remove completed; missing-file status 1 reached the shell. With four
CPUs, manual local `mousetest` in Development exercised DISPLAY ACQUIRE/RELEASE:
BSP MUTATING with 0/0 counts, AP REPLY on its private root, normal Escape released
the display and restored the prompt. Remote mousetest correctly lacked display
authority. No fault injection, sibling tasks or new test infrastructure.

Source audit covers late BAD_BUFFER, exact returned-generation unwind, scoped
operation claims, queued-device cleanup and the bounded precommit BUSY path.
Those races/timeouts are not measured with the retained single user task.
Dynamic DISPLAY REPLACE, HOST/NPFS, disk, HCI and audio hardware remain unqualified
in this run. Task 5 must qualify sibling entry, held-task/retirement races and
these concurrent refusals. No public ABI or new kernel log lines.

## Main integration

Merged main `97336d9a` into integration `c1e24cc8` after freezing the comparison.
The clipboard conflict keeps both fallible captured input and main's UTF-8 Copy
normalization; independent source review found no blocker. Ordinary source image
and host client builds passed with matching SDK/userland/ports from successful
main [#1811](https://git.internal/PyxisOS/pyxis-os/actions/runs/1811).
Userland `d2674ca576e39ff0614f06611743e77d0616d639`, ports
`df3210b04142295206d0b60850cbe5959dd2cffa`; fs/lwIP/compiler unchanged.
Integration ELF SHA-256
`9fd8a413906e2ab4e1ef66d576209349eaf57a802770e8eab390f23c12c4e1e3`, ISO
`15e467553eea3af9c4d819f3fc75f322ace1686cec19aa78a2ce6b0e596bbe3d`, initrd
`e1f6959ad2ec41069bf9bdb8210cb40a3703204bf78c6ea174412b35843a25c0`.

Fresh 1/4-CPU boots and matching-ELF GDB reconfirmed BSP MUTATING 0/0 on kernel
CR3, direct executor completion, private-root REPLY 1/0 and OPEN cleanup; the
four-CPU caller resumed on CPU1. Normal page allocate/release completed after
detach. Provider/pipe and RAM create/read/rename/remove passed; screenshot data
passed an eight-byte PNG signature check through Lua. Denied boot:// creation
left no file, and missing-file status reached the client. Five 4-KiB SEND rounds
of 256 messages verified with zero rejects and FINAL 0/complete. An attempted
`hexdump --help` returned command-not-found (not used as a capture check); the
replacement Lua check passed. One early GDB breakpoint before firmware mapped
the kernel was refused; after ordinary boot, the same matching-ELF inspection
passed. These runs are functional qualification, not additional timing samples.
Integration captures remain in ignored `build/task4-integration`; owned VMs,
debuggers and clients were stopped.

## Native ThinkPad comparison

Owner-run ThinkPad, 12 CPUs, AC, PXE, fresh A1/B1/A2/B2 boots. A main
`97336d9a` (owner-reported ELF SHA-256 prefix `67154e97`), B `b7a00f7d`
(`75964d51`); main `97336d9a`'s initrd for all runs. Same commands below;
launch wall time is horse-client time including discovery. Raw JSONL in
`/shared/present/batch2/ab700/700{A1,B1,A2,B2}/`, wall times in each
`launch-times.txt`. Local inspection verified all 1,600 timed IPC passes, all
12 page runs (800 allocations/releases, no failures), all 12 1,024-child launches
and all FINAL 0/complete records. No profiled measurements or samples filtered.

Milliseconds except launch seconds; IPC cells are medians of 100 samples:

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| Pages, three runs | 35.369 / 35.892 / 35.335 | 41.600 / 42.326 / 42.093 | 35.952 / 35.392 / 35.369 | 38.037 / 37.970 / 37.978 |
| 1024 launches, three runs, s | 1.342 / 1.525 / 1.449 | 1.572 / 1.781 / 1.720 | 1.465 / 1.597 / 1.353 | 1.699 / 1.895 / 1.453 |
| CALL64 complete | 1.253 | 1.644 | 1.233 | 1.316 |
| CALL4096 complete | 1.874 | 2.108 | 2.080 | 2.231 |
| SEND64 admit / complete | 0.0785 / 0.490 | 0.099 / 0.553 | 0.063 / 0.351 | 0.090 / 0.482 |
| SEND4096 admit / complete | 0.2185 / 1.143 | 0.2655 / 1.038 | 0.2305 / 1.224 | 0.277 / 1.020 |

Paired per-boot medians: pages +19.0% / +7.3%, launch +18.7% / +16.0%,
CALL64 +31.2% / +6.7%, CALL4096 +12.5% / +7.3%; SEND4096 completion
-9.1% / -16.6%, despite higher admission. Individual IPC ranges overlap:
CALL64 A 0.854–3.702 / B 0.989–3.970 ms, CALL4096 A 1.033–4.055 /
B 1.193–4.728 ms, SEND64 A 0.293–1.376 / B 0.377–2.046 ms,
SEND4096 A 0.406–1.928 / B 0.544–1.877 ms. Clock-read calibration A
136–148 ns, B 190–194 ns except B2 CALL64 435 ns; not subtracted. The
consistent pages/launch cost is real and much smaller than nested KVM.

**Attribution by code inspection, not a timed phase decomposition.** Pages makes
800 MEMORY ALLOCATE and 800 RELEASE calls. Each now saves/parks its caller,
reloads kernel CR3 before acknowledging departure and publishing the deferred
request, executes on BSP, then resumes on the private root for final reply.
`memory_call` / `bsp_request_submit_and_wait` / `process_vm_mutation_forward`
implement that new round trip. All page/frame work remains in `vm_alloc/free`;
new admission guards, borrower leases and ALLOCATE output revalidation add work.
The paired page deltas are 2.6–6.7 ms, or about 1.6–4.2 microseconds per mutation
averaged across the full workload, not isolated scheduler/CR3 timings. With the
sole task parked and no retained borrowers, the existing ready path observes
zero roots/borrowers and executes directly in the BSP executor: no peer drain,
NMI/IPI acknowledgement round or VM-worker timeout wait is added here.
AP-to-BSP notification and caller wakeup still belong to the round trip; page
work also moves CPU/cache locality. The captures do not isolate those components.

Launch does not newly hand off each child mapping. `user/spawn.c`, `load.c`
and `startup.c` still prepare unpublished children exclusively on BSP. Lua's
pinned `lua/pyxis.c` and libpyxis `program.c` perform lookup, authority queries,
prefix reads, launch, wait and cleanup; these now pay syscall/root accounting
and copy leases repeatedly. Parent mallocs reuse retained TLSF pools; `echo -n`
uses static stdio and copies allocator authority at startup, without inherently
allocating a heap pool. Therefore MEMORY round trips cannot explain every child.
The original `launcher.c:capture_string` called `copy_from_user` per character
for argv, resource/root names, environment and cwd, acquiring and releasing a
guarded borrower for every byte. The bounded-capture change below removes that
amplification. No native
trace apportions the observed roughly 229–265 microseconds extra per launch
among string capture, other accounting, occasional heap growth and client time.

**Owner accepted:** retain the generic zero-activity fast path and accept the
remaining BSP MEMORY round trip (about 1.6–4.2 microseconds per mutation) as a
[latency follow-up](https://git.internal/PyxisOS/pyxis-os/issues/1310),
without a task-count bypass. Bounded string capture and the native rerun are
complete. The IPC fixed cost is accepted for #700 merge, with a bounded
investigation before task 5; this does not authorize its implementation.

### Bounded capture qualification

Implementation `48b8b98d`: one short lease per string, page-aware validation and
byte copying through NUL; unchanged capture budget, overflow checks and
BAD_BUFFER/BAD_REQUEST mapping. Each page is validated only when reached, and
failure unwinds the lease before returning. RAM/INITRD prefix reads reuse their
existing outer lease for full-output validation and header copy, removing
redundant nested borrows on the launch path. Global borrower accounting, other
nested paths, BSP mutation and the public ABI are unchanged. Source review found
no blockers; invalid addresses and final-byte/page-edge rules are source-inspected.

Ordinary source image build passed with the same SDK/userland/ports, compiler,
config and byte-identical integration initrd. ELF SHA-256
`a698895584743f13546a506b2c1299fef097c05e0229866f0e559c07a54f650b`, ISO
`0a320c42639c929706df6b38b3e43be52ecb9f9971039a5ca5778b635fceccd9`.
Same QEMU configuration, fresh 1/4-CPU boots, matching-ELF GDB with injected calls
disabled. Ordinary Lua launches passed with empty and 8,192-byte arguments. GDB
observed the long copy crossing a page boundary with one process/task borrower,
returning OK with 8,193 copied bytes and zero borrowers; empty copy returned one.
A 65,500-byte argument exhausted the remaining capture budget: GDB saw TOO_LONG,
zero copied result/borrowers, then CALL_BAD_REQUEST (4) reached Lua and FINAL
1/complete. No child/output was published for that refused launch.

On four CPUs GDB observed INITRD prefix data/header copy on CPU1 with one borrower,
then OK/1,033 bytes and zero borrowers. Sixty-four ordinary child launches and all
four IPC workloads (256 messages, five rounds each) completed with all 20 passes
verified, zero failures/rejects and FINAL 0/complete. Normal provider/pipe and RAM
create/read/rename/remove passed. Pages had 800 allocations/releases and zero
failures on each CPU count. These are functional checks; debugger runs and single
page samples establish no timing improvement. Invalid-pointer injection and new
test infrastructure were not added. Captures are in ignored `build/task4-string`;
owned VMs/debuggers/clients stopped. Native results follow below.

### Native rerun on the final kernel head, 2026-10-11

[Owner-reported results](https://git.internal/PyxisOS/pyxis-os/pulls/700#issuecomment-8315):
ThinkPad T14 Gen 1, 12 CPUs, AC, PXE, hands-free A/B/A/B fresh boots. A main
`97336d9a` (ELF prefix `67154e97`), B `a573f473` (`7f86f475`), one A initrd and
byte-identical kernel.config. Same workloads and parameters; new runs A3/A4/B3/B4,
raw `/shared/present/batch2/ab700-rerun/`. Local inspection verified all 1,600
IPC passes, page runs and launches with FINAL 0/complete. The comparison also
uses yesterday's runs; the 2.71 s launch outlier is retained. Multimodal IPC
medians are noisy, so the fixed per-message comparison uses fastest passes.

| Workload | A (main) | B (`a573f473`) | Change | Before string fix (`b7a00f7d`) |
| --- | --- | --- | --- | --- |
| Pages, ns/op, three runs each | 22,071–22,398 | 23,739–23,800; two higher runs 25,390–25,605 | +7.5% typical | +7–19% |
| 1024 launches, wall seconds | 1.21–1.39; median 1.25 | 1.24–1.49; median 1.30; one 2.71 outlier | about +4%, noisy | +16–19% |
| CALL64 fastest pass, ns/round trip | 3,313–3,348 | 3,823–3,874 | +0.53 microseconds (+16%) | 3,865–3,881 |
| CALL4096 fastest pass, ns/round trip | 3,994–4,054 | 4,631–5,027 | about +0.6 microseconds (+15%) | 4,659–4,665 |
| SEND64 fastest pass, ns/message | 222–223 | 318–320 | +97 ns (+44%); 222 -> 319 ns | 318–319 |
| SEND4096 completion median | 1.117–1.214 ms | 1.162–1.204 ms | unchanged | — |

Bounded capture removes most of the launch penalty; it does not change the IPC
fixed cost. The owner accepts merge with the per-message IPC cost recorded as
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1310).
Its relative impact is largest for short messages. A bounded investigation of
copy/activity leases follows before task 5 and does not block #700's merge.
Causal phase attribution and any optimization remain unmeasured/unimplemented.

### Hands-free commands

For another comparison, keep a matching initrd and fresh A/B/A/B boots. Four
sessions, redirect each JSONL to a distinct file:

```sh
for mode in call send; do
  for size in 64 4096; do
    printf 'session bin://ipcbench.pxe %s --size %s --messages 256 --rounds 100\n' "$mode" "$size" |
      pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323 >"$mode-$size.jsonl"
  done
done
printf 'allocbench pages --size 65536 --live 8 --rounds 100\nexit\n' |
  pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323 >pages.jsonl
printf 'session bin://lua.pxe -e "for i=1,1024 do assert(pyxis.run{\047echo\047,\047-n\047}==0) end; print(\047launches=1024\047)"\n' |
  pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323 >launch.jsonl
```

EOF drains a session successor; do not send `exit` to a session or sleep waiting
for its output. Repeat pages/launch three times with separate output files;
precede launches with the same command using 16 children. Time launch client wall
time externally, as above, including discovery. Record exact kernel/ELF/initrd
identities and complete FINAL status. Preserve the original native captures.
