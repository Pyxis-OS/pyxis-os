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
is inherent in the accepted BSP ownership here; native evidence is pending, and
it must be assessed before concurrent entry. No allocator batching/API change.

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

## Native hands-free comparison (pending)

Use main `97336d9a` as A and the submitted integration head as B, one matching
current initrd for both, fresh PXE boots A/B/A/B, client on horse. This is a new
native comparison, distinct from frozen nested-QEMU A/B above. Four sessions,
redirect each JSONL to a distinct file:

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
identities and complete FINAL status. Native results will be added separately.
