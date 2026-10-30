# Libc capacity

Limits audit work package 6 (rows 21–22). WP6.1–WP6.6 were accepted by chronium
on 2026-10-30. This work implements the [limits rules](../interfaces/limits.md)
without adding authority or quotas. Kernel admission changes are coordinated
with the launcher work; this checklist distinguishes libc from that dependency.

## Accepted contract

- Thread identities retain their generation and slot widths. Stable growable
  metadata replaces the libc count policy. A live child's borrowed record and
  wait word never move. One collector owns cleanup; records become reusable only
  after confirmed retirement. Uncertain storage remains retained.
- Keys have a growable generation registry and lazy per-context values backed by
  held native MEMORY, independent of malloc bootstrap. Allocation happens outside
  the registry guard. Failed growth preserves old values; unset reads and NULL
  stores need no allocation. Four finite destructor passes defer newly added
  slots to a later pass. Cleanup includes confirmed stopped native contexts.
- Worker stacks keep the 1 MiB eager default, with explicit page-multiple sizes
  from the native page minimum through actual layout/backing admission. SDL
  rounds explicit minimum requests without a second MiB policy. There is no
  demand-fault stack growth.
- One full-width TLSF allocator handles representable requests and alignment,
  with checked class, search, pool and page-rounding arithmetic. Normal malloc
  alignment stays 16 bytes; zero-size behavior and failed realloc ownership stay
  unchanged. The kernel allocator is separate.
- Standard APIs retain their error surfaces. Thread/key capacity gives EAGAIN;
  metadata exhaustion gives ENOMEM; invalid stack shape gives EINVAL. Native
  authority, busy and uncertain failures retain their meaning. There is no libc
  automatic diagnostic output or last-error slot. Optional native limit reasons
  use the launcher work's common shape, never a second ABI.

## Delivery

- [x] Stable libc thread records and native-sized worker stacks (userland #454).
- [x] Growable key registry and lazy per-context values (userland #455).
- [x] Full-width checked allocator and over-page alignment (userland #456).
- [x] SDL worker-stack migration and reached consumer updates (ports PR linked
  from the integration PR).
- [ ] Common limit-reason wiring and coordinated native count/stack admission.
- [x] Per-item one/four-CPU QEMU qualification and SDK/C++/SDL builds.
- [x] Quiet matched existing-tool measurements after the concurrent build ended;
  earlier load-affected samples retained without a performance conclusion.

Kernel files and function ownership are sent to the orchestrator before edits;
no independent launcher/process changes are implied by the libc delivery.
The SDL detached-thread no-op is a separate existing lifetime issue, not fixed
or qualified by removing registry limits.

The userland/ports work is pushed as draft PRs pending common limit reasons and
remaining kernel/ABI coordination. The current kernel still enforces 256 native task-storage
records; tests above that count await the coordinated kernel slice. Stopped raw
context value-release ownership has source review; no allocator fault injection
or native qualification is claimed. See the
[qualification record](../development/experiments/libc-capacity.md).
