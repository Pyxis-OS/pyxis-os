# Native thread lifecycle qualification

Pre-code A: main `a617bcd74fcb875b3ddae8ad1ceaaeabae698f9e`, 2026-10-11,
including #704/#705. Exact-head successful CI #1836 supplied matching kernel,
SDK, userland and ports bundles; ordinary image assembly and host remote-client
build passed with LLVM 23.1.3 builder `49e2c1a`. Frozen ELF SHA-256
`036ee426bf60e2b8e53860c96b06e72e69a9da33bb6d1613a488d61cc1fde0c4`,
initrd `1db324cb59b374b9a4ce684a88500b3fd040520853ccfd67dde5b0a9f7ae2388`,
ISO `4d8326a9284b8cc5f0cc841476b3659a5de37be69c33ccb8dd7f3e5e56b19133`.

B timing source: `d7fc453ac45635cfefbbc213d066b5e01b627e86`; ordinary
`make -j16 image` built kernel, SDK, ports and userland with the same builder,
owner mirrors and cached firmware, without a compiler rebuild. Final kernel
rebuild used verified SDK/userland/ports bundles. Userland
`76d0508089d709fe2837d74a0edde148e391f700`
([#212](https://git.internal/PyxisOS/pyxis-userland/pulls/212)) supplies native
helpers and explicit session delegation; other pins are unchanged. No libc
runtime/startup, benchmark or klog format changes.

The matched B image deliberately uses A's identical initrd and kernel config:
ELF SHA-256 `31fcae50362e0a9291d18b5b6c5c8791e3cbbdd745ebe0d4981e5c4562699ccc`,
ISO `9fb0c83cd5ae9ae1eab175b5a3f8667aa10bbf6055d3617b72f12c26f72ff043`.
Old ordinary programs use their unchanged ABI subset. Sibling qualification
instead uses the new userland initrd and final kernel source `50a80d6b`:
ELF `d5ef673d3d08dcabacc5f58153cce7e154b6d65f2679c76af628ac9f511971f9`,
initrd `f037f77b8a9fd4b4c5804d5fae7d5d4f71ec6e6a0f2833d80612fd0ea2294fcb`,
ISO `6896a17378602f86e0be72657286c904818ed7084c230a9d8c690a30e499dc2e`.
Later gitlink/documentation commits change no qualified kernel source.

## Interleaved nested-QEMU comparison

QEMU 10.2.2, Q35/nested KVM, CPU host, 1/4 cores/one thread per core, 2 GiB,
UTC RTC, raw OVMF/fresh VARS, standard VGA/display none, modern VirtIO SCSI
CD/net/RNG; info logging/profiling off. UDP NAT forward 2324; reverse client
`--machine --no-shell-echo --listen t14 --beacon-address 127.0.0.1
0.0.0.0 2323`. No debugger or task-owned build during timing.
For each CPU count: fresh A1/B1/A2/B2 boots; A1 was captured before implementation.
Per boot, in order:

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 100
session bin://ipcbench.pxe call --size 4096 --messages 256 --rounds 100
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 100
session bin://ipcbench.pxe send --size 4096 --messages 256 --rounds 100
allocbench pages --size 65536 --live 8 --rounds 100
session bin://lua.pxe -e "for i=1,1024 do assert(pyxis.run{'echo','-n'}==0) end; print('launches=1024')"
```

Each session uses a fresh client, which waits for FINAL rather than a fixed
sleep. Pages run three times, each followed by shell `exit`; launches have a
16-child warmup then three 1024-child runs. All 3,200 timed IPC passes verified,
24 page runs reported 800 allocations/releases with zero failures, every launch
count matched and every invocation ended FINAL 0/complete. Clock calibration
32,993–37,932 ns/read, not subtracted. No samples removed. IPC values below are
median (minimum–maximum) milliseconds per 256-message pass; pages lists all
three elapsed milliseconds, launch lists host-client wall seconds including
discovery/session setup.

### 1 CPU

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 84.177 (83.300–85.584) | 85.330 (83.835–91.264) | 85.463 (84.414–91.208) | 85.895 (84.729–89.865) |
| CALL4096 completion | 84.391 (83.341–92.303) | 86.724 (84.831–96.428) | 85.893 (84.887–90.605) | 86.814 (85.058–104.597) |
| SEND64 completion | 13.011 (12.783–13.536) | 13.133 (12.842–14.168) | 13.164 (12.952–15.401) | 13.212 (12.873–17.567) |
| SEND64 admission | 1.202 (1.186–1.397) | 1.215 (1.186–1.700) | 1.222 (1.190–1.442) | 1.211 (1.190–1.881) |
| SEND4096 completion | 13.128 (12.874–14.109) | 13.396 (13.011–20.402) | 13.335 (13.059–14.370) | 13.308 (13.003–15.343) |
| SEND4096 admission | 1.260 (1.238–1.448) | 1.288 (1.264–1.798) | 1.302 (1.263–1.831) | 1.301 (1.262–1.526) |
| Pages, ms | 183.905 / 185.056 / 184.665 | 189.022 / 186.295 / 188.982 | 189.014 / 188.600 / 187.930 | 186.182 / 190.192 / 186.227 |
| 1024 launches, s | 4.15 / 4.14 / 4.13 | 3.21 / 3.20 / 4.24 | 4.21 / 3.19 / 3.19 | 3.20 / 3.18 / 3.13 |

### 4 CPUs

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 77.209 (73.371–104.668) | 95.553 (86.850–121.938) | 95.062 (73.716–115.986) | 95.178 (73.686–122.220) |
| CALL4096 completion | 78.142 (73.910–104.450) | 100.142 (73.792–114.350) | 97.978 (74.060–115.251) | 95.034 (76.017–125.634) |
| SEND64 completion | 11.995 (11.276–16.777) | 15.495 (11.277–22.265) | 14.551 (11.618–20.253) | 14.162 (11.641–21.804) |
| SEND64 admission | 1.210 (1.135–1.809) | 1.206 (1.144–1.707) | 1.112 (1.071–1.349) | 1.143 (1.071–2.220) |
| SEND4096 completion | 12.242 (10.440–17.850) | 14.759 (12.406–24.795) | 14.944 (11.442–21.075) | 16.443 (12.885–27.937) |
| SEND4096 admission | 1.262 (1.135–2.418) | 1.272 (1.205–1.944) | 1.274 (1.139–2.055) | 1.305 (1.259–2.262) |
| Pages, ms | 262.011 / 264.738 / 256.391 | 421.366 / 260.385 / 425.976 | 423.773 / 421.564 / 422.481 | 265.162 / 438.726 / 431.262 |
| 1024 launches, s | 3.14 / 3.13 / 3.19 | 4.74 / 3.11 / 4.81 | 4.77 / 3.77 / 3.85 | 3.71 / 3.18 / 3.19 |

One-CPU IPC/pages medians are close across boots. Four-CPU completion/pages vary
substantially within both A and B; baseline pages alone shift from 256–265 to
421–424 ms. Launch wall time includes discovery and is similarly noisy. This
comparison establishes workload completion, not a native speedup or regression.
Raw captures/frozen images remain in ignored `build/task5-{baseline,after}`.

After baseline timing, interactive four-CPU GDB with target calls disabled
observed the existing BSP network worker completing UDP receive and issuing a
reschedule IPI to CPU1; echo/exit completed FINAL 0/complete.

## Sibling qualification and debugger inspection

Manual [threadprobe](https://git.internal/PyxisOS/pyxis-userland/src/commit/76d0508089d709fe2837d74a0edde148e391f700/threadprobe/README.md)
through the same remote client, new initrd, 1/4 CPUs. Children use native calls,
atomics and raw FS, without shared libc allocation/stdio/errno. Inputs are eager
64 KiB usable stacks and one-page FS contexts, not libc's future sizing policy.

| Mode | Observed on both CPU counts |
| --- | --- |
| lifecycle / inspect | Two siblings increment shared state, use distinct RSP/FS and inherit MXCSR; repeated WAIT and completion readiness succeed. Closing a copied live observer does not kill its sibling. Backing releases after completion. |
| bind | Initial FS binds; rebinding and release of its live context return BUSY (9). Process retirement releases its pin. |
| refusal | Live stack/FS release and duplicate contexts return BUSY; stale identity and NX entry return BAD_REQUEST (4). Subsequent valid CREATE succeeds, checking provisional unwind. |
| close | Provider RECEIVE establishes sibling CALL admission; shared caller/receiver CLOSE yields ENDPOINT_CLOSED. Pinned release is BUSY; join then release succeeds. |
| stop / fault | Separate process with two live siblings: process termination or deliberate user UD2 yields two STOPPED/zero observers and final TERMINATED/FAULTED process completion after reclamation. Existing fault/release output remains. |
| exhaust | Real allocation and CREATE both return NO_MEMORY (10): 1 CPU holds 2,061 regions/1,962,663,936 bytes; 4 CPUs 2,103/1,961,791,488. Release held backing and CREATE succeeds with the same contexts. No kernel injection. |

All ordinary modes ended FINAL 0/complete. Initial qualification used `23e15d62`;
final `50a80d6b` reran lifecycle/fault on both counts and the four-CPU debugger
inspection after correcting retirement reporting to identify the actual faulting
task's CPU. No new log format was introduced.

Interactive GDB, target calls disabled and read-only inspection: CREATE holds
MUTATING with roots/borrowers zero, including the second CREATE with an existing
AP sibling held for drainage. The live process has three linked/storage members.
On 4 CPUs its two running siblings occupy CPU2/CPU3, share hardware CR3
`0x3e46000` with the process, and have RSP `0x12f20`/`0x25f20`, FS
`0x15000`/`0x28000`; borrowers are zero. Immediately before observer publication,
both exiting stack/FS pins are false, linked/storage counts are 2/3 and borrowers
zero; completion changes false to true afterward. One-CPU inspection separately
confirms shared root, distinct saved FS and raw FS markers. An earlier four-CPU
capture accidentally wrote an x87 GDB register via a convenience-name collision;
it was discarded and replaced by this clean final inspection.

Source review confirms stop-serialized publication, immutable fault-safe result
election, EXIT's admitted-reference cleanup before retirement, observer storage
without task/VM pointers, and BSP pin release before completion. The guarded
operation shares the future reservation family's registry/authority/identity
model; general sparse RESERVE/COMMIT/DECOMMIT remains unassigned.

## Owner-run native A/B and follow-up

2026-10-11, ThinkPad T14 Gen 1, 12 CPUs, AC, hands-free PXE A1/B1/A2/B2;
A `a617bcd7` (ELF `750cf669…`), B `a0c02469` (ELF `4a4a8564…`). All boots
use A's initrd and byte-identical kernel config. Raw captures:
`/shared/present/batch2/ab709/`. Values below are owner-supplied measurements;
fastest passes and completion medians are different statistics.

| Workload | A1 / A2 | B1 / B2 |
| --- | --- | --- |
| CALL64 fastest pass, ns/round trip | 3824 / 3802 | 3828 / 3732 |
| CALL4096 fastest pass, ns/round trip | 4573 / 4585 | 4574 / 4495 |
| SEND64 fastest pass, ns/message | 304 / 304 | 303 / 304 |
| SEND4096 sender batch-mean median, ns/message | 1020 / 1006 | 1048 / 1030 |
| SEND4096 completion min/p25/median, ms/256 messages | 0.526/0.826/1.005, 0.472/0.833/1.016 | 0.580/0.971/1.264, 0.611/0.966/1.278 |
| Pages, ns/operation, bimodal ranges | 25777–26683, 23762–26620 | 23745–26301, 25088–26553 |
| 1024 launches, wall seconds | 1.225–1.251, 1.216–1.281 | 1.269–1.459, 1.379–1.620 |

Fast paths/pages are unchanged within observed variation. SEND4096 sender
admission rises 2–3%, but completion rises about 26% in both B boots, approximately
1 µs/message. Launches may rise 5–15%, with noisy B2 variation. These results
establish a regression to investigate, not its cause.

**Inspected source:** SEND completion times 32 DRAIN control round trips, 256
RECEIVE/FINISH pairs and payload retention. STOP, verification and process
reclamation occur after its end clock. Endpoint receive/wake, scheduler
placement/enqueue and VM root/copy accounting algorithms are unchanged. No
membership scan, terminal-result election or new lock was added to normal IPC.
The new common-path work is one thread-exit pending load/branch at syscall return.
Task metadata grows 784→944 bytes, process 224→232, VM range 40→48; maximum
per-task BSP request storage remains 5,064 bytes. The `threads` service is created
once in boot init. A's userland supplies no new thread grant to benchmark or Lua
children, so an extra grant per launch cannot explain these matched results.

**Corrections:** the reaper previously stopped the already-retired member before
detaching it, adding queue/VM guards, result-election work and an AP reschedule
IPI to each ordinary child exit. Detach now precedes stop fanout, which still
reaches remaining siblings and preserves lifetime/pin/completion ordering. Final detachment also skips the failing default-result CAS when an immutable
process result already exists; the zero-result path still elects EXITED/0. These
are real accidental launch costs; they occur outside SEND's timed completion and
cannot directly explain that 26%.

New cold lifecycle fields also interrupted the original task layout, moving FP
and scheduler/copy/wait state. They now follow the old tail; the pending flag
uses old padding beside the existing hot flags. Offline DWARF checks verify
restored old hot-field offsets. This removes an avoidable layout disturbance,
but its cache/timing effect is unmeasured; it is a candidate, not a demonstrated
cause or speedup. Global queue/worker cache-line offsets remain unchanged in the
frozen A/B ELFs. No speculative allocator/scheduler redesign is included.

Follow-up sources `a00d1672` (stop targeting/layout) and `59230ac2` (redundant
election). Ordinary image builds passed. The first reran all seven sibling modes,
four 256-message/100-round IPC workloads and 1,024 launches on 1/4 CPUs: all
800 IPC passes verified, every invocation FINAL 0/complete. Interactive GDB on
both counts confirms the faulting retiree is detached (linked/storage 2/3), while
two still-live peers receive stop; 1,024 launches hit no task_request_stop entry
breakpoint. After the election correction, all seven modes and 1,024 launches
passed again on both counts. Final read-only GDB observes linked/storage 0/1
and an already committed EXITED/0 at the last detach; a 16-launch session hits
no final-member process_set_result entry breakpoint. Independent source review
finds no lifetime/pin/publication change. Final ELF SHA-256
`c8e43a7e243216e98b60bf6d5f16404fa5229cebe220f5ab0d3211d91089f2e6`,
initrd `583d31b7ec38536263a8f52a58c718eed7050aa84114d49e5dfe12c84c8f6d52`.
No native speedup is established. Captures remain in ignored
`build/task5-review-{qualification,final}`; task-owned VM/debugger/client jobs
are stopped. Exact submitted-head CI is reported in the PR.
Owner native rerun: A `a617bcd74fcb875b3ddae8ad1ceaaeabae698f9e`, B the submitted
follow-up head, same A initrd/config and commands above. Original B `a0c02469`
can be retained for an isolated before/after follow-up comparison. New sibling
qualification requires B's new SDK/initrd with userland #212. No improvement
is claimed before that rerun.

This gate supplies native lifecycle only: no parking, language TLS/errno,
thread-safe libc, pthread/C11 runtime or consumer adapter. The probes cover
bounded interleavings, not every device/endpoint/VM race or native hardware.
All completed qualification QEMU/GDB/client jobs were stopped after collection.
