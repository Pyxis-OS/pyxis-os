# Libc capacity

Limits audit work package 6 (rows 21–22). WP6.1–WP6.7 were accepted by chronium
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
- [x] SDL worker-stack migration and reached consumer updates (ports #241).
- [x] Common limit-reason wiring and coordinated native count/stack admission.
- [x] Per-item one/four-CPU QEMU qualification and SDK/C++/SDL builds.
- [x] Quiet matched existing-tool measurements after the concurrent build ended;
  earlier load-affected samples retained without a performance conclusion.

The kernel slice is stacked on the actual WP3 launcher/capacity branch, reusing
its common optional reason ABI. No launcher or process-lifetime redesign is added.
The SDL detached-thread no-op is a separate existing lifetime issue, not fixed
or qualified by removing registry limits.

The stack remains draft until WP3 merges and the native LLVM prefix/container
is rebuilt for the changed development SDK signatures. Native count admission
now checks representability, and ordinary one/four-CPU QEMU holds and joins 288
workers, then checks process completion with 288 workers still held. Named
rounding/address refusals and success-path detail clearing also pass. Stopped
raw-context value-release ownership has source review; no allocator fault
injection, identity/counter-exhaustion injection or native qualification is claimed. See the
[qualification record](../development/experiments/libc-capacity.md).

## Key metadata retirement

WP6.7 accepted (chronium, 2026-10-30): failure to retire obsolete key metadata
retains surviving backing and its actual charge under process ownership until
teardown, without terminating the process or undoing a valid publication.
Uncertain addresses/handles are forgotten and never accessed or retried later;
this prevents a collector from acting on a reused address. Pre-publication
allocation failure keeps the old key/value and its original failure status.
Orderly destructor drainage and stopped-context collection clear their ownership
descriptors before retirement. Native CREATE uncertainty and live stack/context
retirement rules are unchanged. No injected cleanup failure is claimed.
