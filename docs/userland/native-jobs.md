# Direct native jobs

Libc's [`pyxis/job.h`](../../userspace/libc/include/pyxis/job.h) launches one
known native executable through the caller's existing LAUNCH-authorized launcher.
The child stays in that launcher's space, CPU and execution group. The core
does not resolve images, expand bundles, interpret scripts or create groups.
Lua, shell and pipelines remain later work in the
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

## Private capture and deadlines

[`pyxis/capture.h`](../../userspace/libc/include/pyxis/capture.h) wraps one direct
job with separate native RAM stdout/stderr FILEs. The trusted caller explicitly
supplies an exclusively provisioned work root and READ clock. Create/unlink
happens before either WRITE-only stream is delegated. No stdin is installed;
the command options must leave all three streams at `-1`. There is no implicit
cwd, environment, namespace, image lookup, bundle expansion or adapter.

The capture profile validates the final inventory, including unnamed entries
and appended cwd. It excludes terminal, keyboard, pointer, display, capture,
clipboard, audio, space, log, mount and raw-disk bypass, alongside the direct
core's authority exclusions. Exact private-root handles and valid domain/object
identity aliases refuse. The caller audits the entire private subtree,
credentials and namespace/provider reachability throughout the job. Directory
metadata cannot prove RAM backing or exclusive access; shared `tmp://` and
unpredictable names are insufficient.

`pyxis_capture_start` retains the record and FILEs; `step` polls policy/readiness,
`wait` uses bounded `wait_many` intervals, and `cancel` requests stop. One required
absolute monotonic deadline covers the job; it never refreshes. Synchronous
preparation/admission can exceed it, and expiry is checked when launch returns.
The default poll interval is 10 ms. Zero limits select 1 MiB per stream: these
are polled stop triggers and returned-byte caps, **not hard backing quotas**.
Output can overshoot before the caller runs again.

The first local stop observation (deadline, either stream limit, cancellation
or operation error) is separate from the actual native result. Stop racing exit
or fault can still produce EXITED or FAULTED. After successful retirement, the
result exposes each final size, capped returned size and truncation flag.
`read` accepts explicit stream/offset and returns native short reads up to the
cap, without allocating backing-sized output or exposing its FILE capability.
There is no chronology between stdout and stderr. `close` returns BUSY until
retirement; source grants survive and owned releases are attempted once.

`fresh_group` requires CREATE_GROUP on an unbound launcher. The wrapper creates
one group internally, retains sole CONTROL, seals it after launch and observes
both process retirement and group completion. It does not retrofit nested groups
or descendants. Direct capture retains the existing caller/session fallback;
there is no new caller-death mechanism or hard cleanup wall-time guarantee.

A failed start can return a non-NULL owned record. Never replay the launch.
`CALL_OUTCOME_UNKNOWN` without a process observer quarantines the captures:
normal close remains BUSY until caller/session teardown. With its retained fresh
group, the wrapper stops and waits group cleanup, allowing release but leaving
`result_valid` false rather than inventing a process result. Other errors also
retain pending ownership for cleanup retries. `step` can observe retirement
after a clock-policy failure; operation failures return a result snapshot.
