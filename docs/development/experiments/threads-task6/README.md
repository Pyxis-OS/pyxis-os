# Native address parking qualification

Pre-code A: main `d124a05387d22e6aa43b52cb210f46ea8a8861ec`, including #710,
#714/#718 and the accepted userland closure. Exact-head CI #1915 supplied matching
kernel/SDK/userland/ports bundles; ordinary bundled image and host-client builds
passed before gate-6 code existed. The owner accepted the binding, page-incarnation
and literal detach-before-notify defaults on 2026-10-11.

B timing source: `780c6ae2599bea1cc1fe050d63075a5e231b845b`; ordinary kernel,
SDK and full image source builds passed with builder LLVM 23.1.3 `49e2c1a`,
owner mirrors/caches and no compiler rebuild. The timing image deliberately uses
A's identical initrd/config and their unchanged ordinary-program ABI subset.
Final qualification uses the new SDK, native helpers and existing manual threadprobe
parking modes from [userland #221](https://git.internal/PyxisOS/pyxis-userland/pulls/221).
Later main integration changed docs/host remote support and the userland pin;
qualified kernel `.text`/`.data` and `.bss` layout match the timed ELF; `.rodata`
differs only in the existing 12-byte build revision, plus debug/provenance changes.

## Interleaved nested QEMU

QEMU 10.2.2, Q35/nested KVM, CPU host, 1/4 cores, one thread/core, 2 GiB,
UTC RTC, raw OVMF/fresh VARS, standard VGA/display none, modern VirtIO
SCSI CD/net/RNG. Info logging; no profiler/debugger or task-owned build during
retained timings. UDP NAT forward 2324; existing reverse machine client waits
for FINAL 0/complete. Per CPU count: fresh A1/B1/A2/B2 boots, A1 before code.

Four CALL/SEND × 64/4096 workloads at 256 messages/100 rounds, then a 16-launch
warmup and three 1024-launch Lua runs; [exact commands](../threads-task5/README.md#interleaved-nested-qemu-comparison).
All 3,200 numbered IPC passes independently checked OK/status 0/verified;
all launch counts matched, every invocation FINAL 0/complete. Clock-read cost
35,456–79,836 ns/read was not subtracted. Values are median (min–max)
milliseconds per 256-message pass; launch rows list every host-client wall time,
including discovery/session setup. No samples removed from these runs.

### 1 CPU

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 84.920 (83.678–108.405) | 84.197 (83.099–89.122) | 84.337 (82.603–305.458) | 84.180 (83.526–85.370) |
| CALL4096 completion | 84.641 (83.911–270.759) | 84.537 (83.576–89.138) | 84.853 (83.992–118.662) | 84.630 (83.853–320.556) |
| SEND64 completion | 13.098 (12.874–13.774) | 13.071 (12.783–13.835) | 13.134 (12.867–13.688) | 13.150 (12.804–22.928) |
| SEND64 admission | 1.204 (1.183–1.681) | 1.202 (1.188–1.433) | 1.208 (1.192–1.522) | 1.216 (1.191–2.042) |
| SEND4096 completion | 13.231 (12.965–14.537) | 13.189 (12.922–43.960) | 13.329 (12.874–29.144) | 14.410 (13.309–21.454) |
| SEND4096 admission | 1.289 (1.258–1.518) | 1.271 (1.247–5.143) | 1.292 (1.248–2.806) | 1.423 (1.291–2.197) |
| 1024 launches, s | 3.201 / 3.187 / 3.167 | 4.166 / 4.173 / 3.369 | 3.188 / 3.183 / 3.187 | 3.389 / 4.951 / 4.168 |

### 4 CPUs

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 105.623 (75.731–761.895) | 98.823 (76.934–141.481) | 94.519 (83.119–126.808) | 93.794 (73.641–121.774) |
| CALL4096 completion | 103.113 (76.533–537.711) | 101.652 (76.741–128.337) | 101.545 (73.882–108.296) | 94.827 (83.081–122.845) |
| SEND64 completion | 15.746 (11.758–36.950) | 15.329 (11.708–22.138) | 14.761 (12.324–19.892) | 15.529 (11.712–19.264) |
| SEND64 admission | 1.304 (1.174–4.356) | 1.214 (1.167–2.160) | 1.208 (1.147–1.414) | 1.206 (1.149–1.520) |
| SEND4096 completion | 13.316 (11.396–20.806) | 15.501 (11.827–23.016) | 15.201 (12.893–21.692) | 13.356 (12.423–20.637) |
| SEND4096 admission | 1.293 (1.219–2.135) | 1.286 (1.225–1.812) | 1.275 (1.215–1.867) | 1.260 (1.184–1.465) |
| 1024 launches, s | 3.307 / 3.144 / 3.157 | 3.250 / 3.937 / 3.859 | 3.163 / 3.204 / 3.168 | 3.222 / 3.173 / 3.847 |

IPC medians mostly overlap. One-CPU B2 SEND4096 is higher; A and B both have
outliers and four-CPU completion varies between boots. B launch wall times have
more high values, including roughly one-second shifts; some overlap A. These
nested results establish completion, not a native speedup or an attributed
regression. The code adds token allocation/initialization per private backed
range and 8 bytes/page metadata; native matched launch/IPC results are pending.
An unconditional scheduler-queue acquisition for empty expiry was removed before
the timed B build; its ordinary path now reads a local empty-list pointer.

Two exploratory one-CPU B captures are retained separately: before that fast-path
change, and a run overlapping the isolated probe compile. They and an extra A
capture are excluded from the matched table by configuration/window, not by result.
Raw/frozen captures remain in ignored `build/task6-{baseline,after,qualification}`.

Frozen baseline SHA-256: ELF `ea37cfaed391bc15b3e30bfb5fe6b30f9d1cc675f02e01c35c7c22b4d038933b`;
initrd `d0ca6f8c938d113f7b8ba32c20653416af0b7f2dff50288a2331cf34b7f4e053`;
ISO `e217c7398d334b982342d3e7a84d0442b44fc419723fa7f1c458f54c9da4feed`.
Keep A's initrd/config unchanged for timings; qualification may use the new SDK/helpers.

B timing ELF SHA-256 `430e2199a264e3df697854d925257d8af09af5194986883076e8b3efe85f0f53`;
ISO `13e107a406f9f93a280e96b2279c7415675cf34e70428f8c10a10754e47a31a2`.

## Manual qualification

Ordinary full image rebuild at source `baff3226`, with then-published userland
`0fd8ba55`, passed. ELF `e77cf71714fdad3a836bb54c3845cfa34dc57adb1f8f1e1afa4caa5067b7815b`.
The later userland main rebase drops the already-restored rint replay; source trees
match. Final full image rebuild at source `24b4ee5a`, pin
`ee9ccae7be0f33b4c82f4ac00f285a0571ce37d7` after rebase onto userland main
`dca986b9` (#218/#220), passed; current-main ancestry was checked before pinning.
ELF `37af384d3bc348028c12a5e3cb35b7af7897a972cccb06560df3390aafd3a203`,
initrd `38e2dc0d0454964066434b987f21cc7f9d53fa9b5e9130c435765dac99fba512`,
ISO `fe96fbc13114ef925c28e2e9e5039e1b7cd8c1f2fe8f62f31d1d40f1d82a3b83`.
Its kernel `.text` matches the timing ELF byte-for-byte. Updated contention,
refusal, process-isolation/stop, lifecycle and CLOSE modes passed again on 1/4
CPUs. Userland's focused diff has no libc/runtime/startup changes.

On both CPU counts, manual `park32`, `park64`, `park-refusal`, `park-stop`,
`park-inspect`, `park-unmap`, existing `lifecycle`/`close` and real `exhaust`
modes passed. Contention runs complete 64 rounds with actual wake counts equal to
observed returns and an empty final wake; wake with the predicate unchanged requires
rechecking. Refusals cover BSS/stack/heap, mismatch, poll/future expiry, zero maximum,
high 32-bit expectation, unaligned/unmapped/read-only addresses.
Exhaustion reports real allocation/CREATE NO_MEMORY and successful recovery;
no injected failure. Identity-counter exhaustion and RW protection upgrades are
inspected, not artificially exercised.

Interactive QEMU GDB observes:

- Two linked registrations on one key, physically parked with roots/borrowers zero;
  on four CPUs the inspected siblings are on CPUs 2 and 1.
- Wake, timer expiry, process stop and BSP withdrawal commit the reason/unlink and
  decrement the count before `task_address_wait_wake`: notification still false.
- Timer cancellation runs on the waiter's permanent scheduler stack, with current
  task NULL, no borrower and its pending expiry entry already removed; four-CPU
  inspection observes this on AP CPU 1. Other ordinary clock timers remain valid.
- Stop retains process lifetime exclusion, sets permanent stop and drains both
  linked records. After modes finish all buckets and per-CPU pending expiry lists
  are empty; no sleep retains a mapping lease.
- Same-VA heap reuse gets a fresh incarnation (1 CPU 14529→14530; 4 CPUs
  493851→493852), old WAIT returns BAD_BUFFER, new mapping WAKE returns zero.

A selective-CPU debugger experiment for early notification hit the probe's existing
five-second progress bound (EXIT 1) while required CPUs were held. It was abandoned;
ordinary runs passed and final inspection found empty buckets/pending lists.
Final four-CPU GDB caught an actual pre-park wake: WOKEN/unlinked with
`parked=false`, `notified=false`, zero per-task borrower, then `notified=true`
and scheduler association cleared. The resumed 64-round probe completed
FINAL 0/complete. The updated parent supervisor also verifies same-VA WAKE
returns OK/count zero while another process's child waiters remain isolated;
process termination/join then passed on both CPU counts.
No kernel state was injected, no new klog lines, test framework or POSIX surface.
Native hardware/performance remains owner-run evidence; gates 7/8 need separate go.

Task-owned QEMU/GDB/client jobs are stopped. Exact submitted-head CI is reported
in the PR; userland has no Actions tasks. Native A/B is ready for the owner:
A = main `94d0320d` (includes #710), B = submitted PR head, same A initrd/config,
four commands above plus 1024 launches. No native speedup is claimed.
