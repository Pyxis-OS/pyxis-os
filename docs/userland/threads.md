# Libc threads and synchronization

The selected `<threads.h>`, `<pthread.h>` and `<semaphore.h>` profile uses native
THREADS CREATE/EXIT/completion and address WAIT/WAKE. The kernel exposes native
objects, not pthread calls. Libc keeps its caller-only FS bootstrap authority
separate from the explicit `threads` MANAGE resource required for siblings and
parking; creation also needs MEMORY MANAGE. Timed operations require CLOCK READ,
and sleeping requires CLOCK SLEEP. No missing grant is supplied implicitly.
Bundles request the existing resource as `threads = { "manage" }`.

## Creation and collection

Libc-created siblings are joinable or detached, with process-local generational identities,
distinct eager guarded stacks and static local-exec TLS/errno. The initial
thread has a usable identity but cannot be joined; raw native tasks have no
managed identity and cannot be joined through libc. Initially 64 unreclaimed
records include the initial thread. The default stack is 1 MiB; explicit sizes
are page-aligned within 1–8 MiB. These limits are tuning, not a public ABI.

CREATE reserves its record and backing before native publication. The child
waits for the completion observer to be published before entering user code.
An ambiguous native CREATE result stops the process; it cannot safely be retried
or have its possibly live backing released. One joiner claims a record, waits
for native retirement and pin release, then releases TLS, stack and observer.
Repeated/stale joins fail. Cleanup failure quarantines the record until process
teardown rather than reusing possibly live storage. Definite pre-effect native
VM BUSY refusals get at most eight attempts outside guards; persistent BUSY and
all other errors remain failures. Unknown outcomes are never retried. The retry
budget is tuning, not an API guarantee. Heap growth rechecks already published
pools between BUSY attempts.

Ordinary sibling return/exit drains language TLS destructors in reverse order,
then up to four key passes, and reports its return value. Keys are generational;
deletion invokes no callbacks. A key value is cleared before its callback.
After the key passes, language destructors first registered by keys also run.
`libc_thread_cleanup_register` installs one per-thread runtime cleanup callback;
it runs last, after both language drains. Repeated registration of the same
callback is accepted before cleanup starts; a different callback is refused.
This final runtime callback must not register new destructor-bearing TLS or
rearm keys: no further drainage follows. After a registered runtime cleanup,
required language-destructor registration terminates rather than silently
losing the callback. Programs without that hook keep their existing exit order. Forced stop,
abort and `_Exit` still bypass user cleanup. The hook supplies no new authority
and does not replace the language's process cleanup.

Initializer thread exit resets active once objects for another attempt. Recursive
thread exit during destructor drainage stops the process rather than claiming
normal completion after abandoning remaining callbacks.

`pthread_detach`, `thrd_detach` and `PTHREAD_CREATE_DETACHED` mark a sibling as
having no joiner; a thread may detach itself. The record stays valid, and refused
as a join target (`EINVAL`), until collected. Nothing runs on the thread's behalf:
the exiting thread never frees its own stack, so the next `pthread_create`,
`thrd_create`, join or orderly exit collects every detached sibling whose native
completion is ready, releasing its stack, TLS and observer exactly as a join does.
The 64-record bound is unchanged and a finished detached thread no longer counts
once collected; a burst of live threads still fails with `EAGAIN`. Detaching an
unknown or stale identity returns `ESRCH`; the initial thread, a thread already
detached or being joined, and the wrong entry kind return `EINVAL`.

Join joinable workers before returning from main or invoking orderly process
`exit`; finished detached workers are collected for you.
Libc runs process exit handlers, `.fini_array` and stdio shutdown only from the
initial thread after collection (a detached thread still running counts as live);
otherwise exit forces process teardown without
user callbacks or stdio flushing; a native exit-handler primitive is recorded as
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1302). FS remains valid throughout orderly finalizers. Native forced
stop/fault never runs user destructors. Last native THREAD_EXIT without an earlier
process result remains PROCESS_EXITED/status 0, as specified by the native ABI.

## Synchronization profile

Process-private normal and recursive mutexes, conditions, unnamed semaphores,
once and runtime keys use real parking. Conditions enroll before unlocking;
every admitted wait reacquires its mutex. Concurrent waits on a condition use
one mutex; recursive condition waits require depth one. Default timed waits use
absolute UTC translated to the monotonic native clock, with UTC rechecked after
expiry. Condition attributes can select CLOCK_MONOTONIC. Invalid times,
overflow and unavailable authority fail honestly.

Reader/writer locks support the audited C++ unwinder; recursive reads are allowed,
without fairness or upgrade guarantees. Keep synchronization storage alive until
all admitted operations return and exclude new users during destruction.
Cancellation, robust/process-shared objects, scheduling controls and C11
yield are outside the profile. Declared unsupported options return ENOTSUP;
undeclared facilities are not stubs.

## Shared libc and C++ state

Allocator metadata is serialized; native pool growth runs outside its guard.
Cross-thread free/realloc is supported. Descriptor admissions retain storage,
not extra native grants: final alias close attempts native CLOSE immediately,
and a reused number cannot retarget an admitted operation. File cursor/read-ahead
and complete FILE operations are serialized; pipes have independent read/write
exclusion. Callers exclude new use during FILE destruction and serialize same-DIR
operations. [Borrowed handle and close rules](libc-portability.md#ownership-and-stdio-integration)
remain in force.

Internal cwd/environment users hold owned snapshots across mutation. Public
borrowed getters require caller exclusion through use. Implicit time and UTF-8
conversion state belongs to each TCB. Timezone conversions retain their selected
snapshot; escaped designation strings remain valid until process exit.

The SDK's C++ adapter uses this profile for `std::thread`, synchronization,
thread-local exception state, static guards, once and unwinder locks. C++ once
exceptions reset the initializer state before rethrow. `std::thread::detach` and
`std::async` workers use detached siblings and are collected as above. Wider non-lock-free compiler atomic helpers are not
supplied; the thread probes use lock-free integral atomics. SDL's adapters are delivered; libuv, Hax and Go adapters remain separate
[assignments](../interfaces/threads.md#delivery).

[Gate 8 qualification](../development/experiments/threads-task8/README.md)
records builds, interactive QEMU/GDB and measured limits separately.
