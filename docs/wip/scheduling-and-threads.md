# Threads and SMP follow-ups

Status: open directions after the runtime SMP milestone, completed on 2026-10-06.
The implemented behavior is in [SMP scheduling](../kernel/smp.md),
[private memory](../kernel/memory.md) and [init](../userland/init.md); the
measurements are listed in [SMP measurements](../kernel/smp.md#measurements).
Nothing here authorizes code or placeholder APIs; each item needs its own
decisions first.

## Serial services off the BSP

The BSP still runs every kernel worker: network, the native filesystem, HOST
transport, virtio-blk, USB and presentation. It also runs the request executor,
task reaping, object retirement and the general kernel VM. The
[task-8 mixed set](../development/experiments/smp-task8/README.md#4-cpus) shows the
cost: with a synced native write running, ttcp keeps about 42% of its solo rate,
the same as before the milestone.

The order is:

1. General kernel mapping invalidation and reclamation.
2. Off-BSP kernel-task scheduling, sleep and preemption rules.
3. Moving one substantial serial service to an explicit owner CPU.

Network and the native filesystem are the candidates. Choose between them with
mixed-load evidence; moving the filesystem also needs synchronized cross-CPU
block clients.

IRQ routing may stay on the BSP while notifications are correct. Keep each lwIP
and filesystem instance's single-owner contract; neither device ownership nor
exclusive instance access needs CPU 0. Do not move all workers or destructors
together.

## Multiple user threads

User threads are a separate milestone, needed by native consumers including
[Neovim/libuv](neovim-libuv.md) and [Go](go-runtime.md). The
[multiple-threads investigation](threads.md) audits current source and proposes
the native contract, safety prerequisites and first task. Its three defaults
are accepted in merged #567; task 1's process-lifetime split is delivered for
review, with [matched qualification](../development/experiments/threads-task1/README.md).
Later tasks remain unassigned.
It must specify:

- thread creation, join and exit;
- TLS and errno;
- synchronization;
- libc shared state;
- faults;
- last-thread and process cleanup.

Siblings break today's exclusive VM and table loans, even on one CPU. Parking one
caller does not quiesce the process, its outstanding kernel operations or its
retained user buffers. Two things are needed before threads:

- shared-table lifetime protection;
- process-wide VM activity and translation coordination.

Private memory operations now rely on the single-task model
([memory](../kernel/memory.md#execution)) and must be revisited with them. No
fake threading, and no separate-process substitute for shared-pointer worker
callbacks.

## Smaller candidates

- **Scratch-slot walks.** Walk the active address space through the recursive
  mapping. This is a later optimization: natively the false sharing costs little
  ([technical debt](../technical-debt.md#scratch-slot-false-sharing)).
- **BSP worker latency.** User tasks share the BSP with the kernel workers
  ([technical debt](../technical-debt.md#bsp-userspace-and-kernel-workers)).
  Revisit if presentation or input latency shows under load.
- **PMM batching.** `vm_back()` and heap growth take the PMM lock once per frame
  ([technical debt](../technical-debt.md#pmm-first-fit-search-under-its-lock)).

## Not prerequisites

Per-space budgets, advanced scheduler policy, NUMA, CPU hotplug and tickless
timers are later decisions, not placeholder APIs. Runtime space creation and
application-controlled titles are in [spaces](spaces.md#later-directions).
