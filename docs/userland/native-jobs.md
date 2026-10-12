# Direct native jobs

Libc's [`pyxis/job.h`](../../userspace/libc/include/pyxis/job.h) launches one
known native executable through the caller's existing LAUNCH-authorized launcher.
The child stays in that launcher's space, CPU and execution group. The core
does not resolve images, expand bundles, interpret scripts or create groups.
Lua, shell, capture, deadlines and pipelines remain later work in the
[execution milestone](../wip/native-command-execution.md).

## Explicit inputs

`pyxis_job_options` selects the image, argv, final grant array, named resources,
roots, optional namespace, working snapshot, environment snapshot and three live
libc descriptors. Set an absent stream to `-1`; zero is descriptor zero. A NULL
working/environment snapshot selects no cwd/an empty environment. Working
snapshot roots and namespace are ignored: only the options' explicit selections
are used. Nothing replays immutable startup grants or adds ambient resources.

The synchronous call owns copies of metadata and attenuated handles until kernel
capture finishes. Source handles/descriptors survive. Cwd and environment
snapshots survive later parent mutation; callers may close them after launch
returns. Directory handles, namespace bindings and executable bytes remain
mutable objects, so copies do not establish an immutable content snapshot.

Every final grant is authenticated and checked, including unnamed grants and
the appended cwd, namespace and stream entries. The first profile denies all
launcher, space-factory, power, controller and administrative protocols, and
rejects management/termination rights on shared namespace, configuration and
observer objects. Ordinary selected content, input/output, network and local
runtime resources are allowed according to the explicit masks in
[`job_grants.c`](../../userspace/libc/job_grants.c). Unknown protocols refuse.
Image/roots/cwd/namespace require the native kinds supported by the launcher;
exported FILEs may supply streams.

The **trusted caller** audits namespace/provider semantics for the entire job.
LOOKUP cannot enumerate/filter/freeze a namespace, and providers or raw clients
can broker effects and transferred grants. Metadata authentication does not
prove those providers lack launch authority. Bundle expansion remains outside
the core; this contract is not a transitive sandbox or a capability encoded in
an environment string.

Streams get distinct READ (stdin) or WRITE (stdout/stderr) grants, without named
aliases or interrupt/input ownership added implicitly. FILE positions start at
zero in the child; the parent's cursor, read-ahead and pushback stay local. The
core flushes pending output in the FILE associated with each selected output
descriptor. It leaves other FILEs and input state alone; callers handle other
writer aliases before mixing I/O. Flush failure returns `CALL_IO` with `errno`
retained and no launch, but already delivered bytes are not rolled back.
Descriptor lookup reports `CALL_BAD_HANDLE` for EBADF and `CALL_IO` for other
descriptor errors. Native operation errors otherwise retain their statuses.

Arrays/strings obey the existing 64 KiB launch-capture bound and separately
page-padded 64 KiB startup bound, checked before output flushing. The initial
stack uses the existing zero/default or page-aligned 1–8 MiB contract. Kernel
image capture and mutable namespace/root collisions can still refuse later.
An uncertain launch returns `CALL_OUTCOME_UNKNOWN`; never replay it automatically.

## Observer ownership and retirement

`pyxis_job_launch` returns an owned opaque record. Its borrowed observer has
WAIT|TERMINATE; never close that borrowed handle. Copy it with WAIT alone to own
an independent observer or use it directly with `wait_many`/`WAIT_COMPLETE`.
`pyxis_job_terminate` requests a safe stop of that process, including its threads,
and preserves an already committed exit or fault. It does not wait or roll back
effects. `pyxis_job_wait` retrieves and caches the actual native
EXITED/FAULTED/TERMINATED result; repeated waits return the same result.

`pyxis_job_close` returns BUSY until a successful wait. Normal cancellation/error
paths must request stop and observe retirement before closing. After retirement,
close attempts release once and clear the record even if the release is
uncertain. Closing an independent observer never terminates the child.

The API is single-owner. WAIT observes execution-resource retirement, including
the process/task/VM/grant table, rather than all deferred object destruction.
There is no per-job group completion or caller-death stop guarantee. A surviving
session can retain a child after caller death; an ungrouped caller has no group
fallback. Published HOST work can delay retirement indefinitely. Descendant
launch authority is refused; the parked nested-group direction is separate.
