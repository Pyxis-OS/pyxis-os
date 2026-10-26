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

The 2026-10-13 BSP inventory (main `50e5411f`; full tables in Git history) ranked
the candidates by benefit and cost. Its first slice, interrupt-on bounded process
reclamation, is implemented ([record](../development/experiments/bounded-teardown/README.md)).
Its accepted decisions also stand: kernel workers keep fixed owner CPUs and
never migrate or work-steal ([SMP](../kernel/smp.md#remaining-serial-services-and-limits));
CPU 0 worker precedence is parked with a design
([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1318)). The
remaining ranking, highest first:

1. Bound the other interrupts-off owner operations: launch and rollback, VM
   allocation, capture, capability-table growth, input and clipboard scans.
2. A fixed-AP pilot with a small owner (entropy, or the one-shot TCP/debugger
   identity helpers), after proving its backend and IRQ ownership.
3. Audio on its own CPU with its IRQ, request and exit-cleanup ownership, after
   that foundation.
4. Device-private block queues, then rendering leased frames on another CPU.
5. Network, xHCI services and the native or HOST filesystem.

General VM reuse, GPE methods, early boot, firmware power actions and fatal
reporting stay on the BSP or faulting CPU deliberately. Each move needs its own
accepted ownership, baseline and native qualification.

## Multiple user threads

User threads are a separate milestone, needed by native consumers including
[Neovim/libuv](../userland/neovim.md#milestone-and-delivery) and [Go](go-runtime.md). The
[multiple-threads plan](../interfaces/threads.md) holds the native contract and ordered
safety/runtime gates; tasks 1–5 (process lifetime through native sibling
lifecycle) are merged and tasks 6–9 are open.

Shared-table lifetime protection and process-wide VM activity coordination
(thread tasks 2–4) are merged. No fake threading, and no separate-process
substitute for shared-pointer worker callbacks.

## Smaller candidates

- **Scratch-slot walks.** Walk the active address space through the recursive
  mapping. This is a later optimization: natively the false sharing costs little
  ([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1314)).
- **BSP worker latency.** User tasks share the BSP with the kernel workers
  ([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1318)).
  Revisit if presentation or input latency shows under load.
- **PMM batching.** `vm_back()` and heap growth take the PMM lock once per frame
  ([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1311)).

## Not prerequisites

Per-space budgets, advanced scheduler policy, NUMA, CPU hotplug and tickless
timers are later decisions, not placeholder APIs. Runtime space creation and
application-controlled titles are in [spaces](spaces.md#later-directions).
