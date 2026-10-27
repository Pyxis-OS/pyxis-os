# Caller-CPU JIT qualification

2026-10-27, shared-host nested KVM correctness checks; no native-hardware result.
Baseline kernel `297e48a4f8603d5c1c6cca38617f6bc9bc1e8051` (merged BSP task3),
implementation `1c90a94f7b5a6fb9dbc0e7d298b394f0488b5d22`, userland
`79035bb9c976dc95c05ebaa4daf624f6182077ff`, ports
`f209c917e4db8051283df3b30e1dd33258ee44cb`. Later foundation edits document the
interface, correct an owner comment and clear the deliberate profiling ABI break.
The [interface](../../../interfaces/jit.md) defines the contract.

## Configuration and commands

Q35, KVM `host,+invtsc`, 4 GiB preallocated RAM, OVMF, VirtIO VGA/network/RNG,
one and four vCPUs. Diskless disposable RAM archive; `nvme.write=0`,
`display.timing=off`, `log.level=info`. Only the qualification Remote space sets
`jit=true`. Baseline/control runs fix it to CPU1; the four-CPU JIT suite permits
CPUs 1–3, and the one-CPU suite uses CPU0. Existing PROFILE clients were rebuilt
for the development snapshot extension. No shipped configuration enables JIT.

Build kernel/SDK and the changed userland/port objects with the pinned builder.
The explicit `jit-min` target builds a manual example bundle, outside normal
`all`/install targets. Through Remote, run `boot://jit-min.pxb`, then its raw
inner executable (expected missing authority), and the external `jit-check.pxb`
with `contracts`, `siblings`, `limits`, `physical` and `children`. Sources,
commands, input hashes and captures remain under
`/shared/present/jit-grant/qualification/`; no test framework or kernel delay
hook is shipped. Lua/libuv fixtures are in the adjacent `adapter-check/`.

## Results

Both full one/four-CPU suites passed:

- Zeroed rounded allocation, attenuated MANAGE refusal, malformed state/range
  refusal, separate ordinary/JIT ownership, mixed RW/NX–R/NX–R/X gauges,
  contents preserved, 32 rewrite/publication cycles, stale and foreign identities.
- A noncalling pthread executes each published value, with a logical-use
  acknowledgement before the next writable transition. Address-wait withdrawal
  returns BAD_BUFFER; restoring writable memory admits a fresh parked waiter
  and one confirmed wake. Debugger inspection saw one enrolled waiter before
  withdrawal, not merely a later failed enrollment.
- 256 MiB and 4096-region boundaries return LIMIT without a new identity. Real
  physical exhaustion using ordinary allocations returns NO_MEMORY for JIT,
  preserves an existing region and its gauges, then recovers after freeing.
- User faults enforce RX write refusal, R/NX write refusal, NX execution refusal
  and post-release execution refusal. An explicit transferred service cannot
  operate on the parent's identity. Stop and repeated joined observation pass.
- Read-only hardware breakpoints saw a 256 MiB allocation admitted on CPU1 with
  MUTATING, zero roots/borrowers and kernel CR3 distinct from the private root;
  before request retirement, status was OK with `stopping=1`, all 256 MiB still
  charged and exclusion retained. The parent's joined result was TERMINATED.
- Existing sparse/eager MEMORY, context pins, parked I/O, Go parallelism and M
  retirement, YMM preservation, stop, and VirtIO display resize controls pass.
- Lua and libuv declaring bundle launches execute the example (42 then 43).
  Raw children receive no implicit JIT. Libuv's explicit zero-right override
  refuses required JIT; explicit MANAGE succeeds without changing the source.
  A space without provisioning refuses required JIT before admission.

One profile-enabled control run per revision used the same four-CPU VM,
Remote CPU1 and existing `allocbench pages --profile` workloads:

| Workload | Before | After |
| --- | ---: | ---: |
| 4 KiB, 16 live, 16 rounds | 2.705 ms | 2.638 ms |
| 1 MiB, 4 live, 4 rounds | 12.974 ms | 4.398 ms |
| 1 MiB, 4 live, 64 rounds | 61.180 ms | 59.574 ms |

These are single elapsed samples with profiling enabled, not CPU time or a
controlled speedup claim. The short large-allocation case varied substantially
across preparation runs (4.398–15.058 ms); host/cache/scheduling effects were not
isolated. No ordinary-memory performance optimization is claimed. JIT's 70
PROTECT requests accumulated 0.216 ms service (16.9 us maximum) on four CPUs and
0.106 ms (4.65 us maximum) on one CPU; the configurations differ, so these are
functional instrumentation observations, not a scalability comparison.

## Instruction visibility and limits

`process_vm_private_call` forces departure to the kernel root before drainage.
The owner advances the generation before effects and publishes REPLY only after
execution completes. The mutator re-enters its private root before copying the
reply/opening the gate; siblings enter after that gate opens. Scheduler private
activation unconditionally writes CR3. An outstanding old-root reservation
prevents mutation, including a return racing closure. Thus every post-commit
execution crosses serialization; generation increment or SYSRET alone is not
used as proof.

[Intel SDM Vol 3A §§10.1.3/10.3](https://cdrdv2-public.intel.com/835754/253668-sdm-vol-3a.pdf)
and [AMD APM Vol 2 §§7.6.1/7.6.4](https://docs.amd.com/v/u/en-US/24593_3.44_APM_Vol 2)
describe synchronous instruction modification and serializing instructions,
including MOV CR3. Publication synchronization and instruction serialization
are separate from TLB withdrawal. The manual example calls no user CPUID to
mask a missing kernel boundary. Source inspection and nested execution do not
replace Intel/AMD native qualification.

Physical exhaustion was exercised, but metadata and wait-token counter exhaustion
were not individually forced. No strict-near placement, sparse JIT, code-entry
leases, LLVM MCJIT or llvmpipe/native performance claim. Global waiter withdrawal
and current BSP final destruction remain existing limits; code users remain the
caller's responsibility. No new default-boot output or activation is added.
