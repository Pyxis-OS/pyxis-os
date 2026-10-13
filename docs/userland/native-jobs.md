# Direct native jobs

Libc's [`pyxis/job.h`](../../userspace/libc/include/pyxis/job.h) launches one
known native executable through the caller's existing LAUNCH-authorized launcher.
The child stays in that launcher's space, CPU and execution group. The core
does not resolve images, expand bundles, interpret scripts or create groups.
Lua exposes owned capture/pipeline records, and libuv has an exact-inventory
opt-in alongside its ordinary spawn path. The shell retains its existing syntax.

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

`pyxis_job_launch_observer` shares the same preparation, final-grant checks and
selected-output flushing, but returns an owned WAIT|TERMINATE observer directly.
Event loops own its dispatch/close policy. It adds no capture record or hidden
stop on close; [`uv_pyxis_spawn_job`](../../ports/libuv/README.md) uses this path.
A launch whose observer publication fails reports OUTCOME_UNKNOWN: a framed
BAD_BUFFER can occur after the kernel has started the child, so it is not proof
of refusal. Missing batch observers have the same conservative rule. Never replay.

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

`fresh_group` requires LAUNCH. The wrapper creates a root through an unbound
launcher or a subgroup through a bound launcher,
one group internally, retains sole CONTROL, seals it after launch and observes
both process retirement and subtree completion. Its direct children still
receive no descendant launcher. Direct capture retains the existing caller/session fallback;
there is no new caller-death mechanism or hard cleanup wall-time guarantee.

A failed start can return a non-NULL owned record. Never replay the launch.
`CALL_OUTCOME_UNKNOWN` without a process observer quarantines the captures:
normal close remains BUSY until caller/session teardown. With its retained fresh
group, the wrapper stops and waits group cleanup, allowing release but leaving
`result_valid` false rather than inventing a process result. Other errors also
retain pending ownership for cleanup retries. `step` can observe retirement
after a clock-policy failure; operation failures return a result snapshot.

## Native pipelines and the event pump

[`pyxis/pipeline.h`](../../userspace/libc/include/pyxis/pipeline.h) accepts one
to eight explicit `pyxis_job_options` stages and a native PIPE service. Stage
streams must all be `-1`: the library installs directional pipes for adjacent
stages, the last stdout and a shared stderr. It uses the capture profile's
private-root audit, clock, deadline, limits and optional sole-CONTROL fresh group.
Every stage's metadata and final grant inventory are checked before setup; one
atomic batch publishes all stages. Child-only parent endpoints close promptly.

`has_input` distinguishes absent stdin from finite supplied input. The library
copies supplied bytes before effectful setup, so later caller mutation has no
effect; allocation failure refuses before launch. Supplied zero bytes delivers
EOF, while absent stdin remains absent. There is no live FILE, descriptor or
callback producer. Intermediate stdout goes only to the next stage; final stdout
and combined stderr drain into two separate private unlinked RAM FILEs. Stderr
has no stage attribution, writer atomicity or cross-stream chronology guarantee.

`start` returns an owned record; `step` performs one bounded concurrent round,
and `wait` advances rounds until cleanup, using `wait_many` when immediate
progress ends. Each round observes all stages, drains both outputs with bounded
try reads and appends, and try-writes input. A full input or output pipe never
causes a serial blocking drain. Native short writes retain their unwritten suffix;
unknown mutation outcomes are never replayed. Waits share one absolute deadline
and are bounded by the existing native wait limit, without refreshing on progress.

Every stage has an independent `stage_valid` and actual native result. A nonzero
exit is a result, not an automatic pump error. Input EPIPE closes only the feeder:
`input_peer_closed`, confirmed `input_accepted` and `input_unsent` describe delivery
while output drain and observation continue. Acceptance means pipe acceptance,
not child consumption; `input_uncertain` marks an unconfirmed input write.
Deadline, limit, cancellation or other operation error requests stop of every
unfinished stage, then observes them and drains buffered output through EOF.

`complete` requires every known stage's retirement, both output drains and the
optional group's cleanup. Sizes, capped reads and truncation use the capture
contract. A transfer failure marks the affected output incomplete; a retained
prefix can still be read after cleanup without inventing EOF or a complete
capture. Error calls return a result snapshot, and the owner may continue cleanup
with `step`/`wait`. `failed_stage` records the native batch refusal index when
available; missing observers never receive invented stage results.

`close` returns BUSY before completion. A failed start may retain an owned record;
never retry launch. Unknown admission without observers quarantines the record
until caller/session teardown. A retained fresh group stops and waits cleanup
without fabricating process results. No descendants, per-job caller-death
guarantee, backing quota or hard admission/retirement wall-time bound is added.

## Lua and declared clients

[Lua's owned jobs](lua.md#owned-native-jobs) use this core for one-stage capture
and pipelines, with exact role/root selection, owned cwd/environment snapshots
and finite binary input. Strong recovery ownership survives Lua errors and GC;
explicit cancellation, observation and once-only close retain the native result
and cleanup distinctions. `pyxis.commands` supplies argv wrappers for ports;
ordinary `pyxis.run`, shell syntax and libuv spawn remain unchanged.

[Declared contexts](program-bundles.md#private-job-contexts), accepted by the
owner 2026-10-12, gate fresh private RAM `job_work` provisioning on `jobs=true`
and usable selected clock/pipe/delegated-launch grants. Lua's declarations are
optional; users invoke it normally. Ordinary children receive no implicit
context, provisioning ancestor or descendant launcher in this capture profile.
A surviving creator cleans after confirmed client/group retirement. Stranded
named trees remain private until reboot under the accepted
[cleanup limit](../technical-debt.md#private-job-context-cleanup-after-caller-failure).
Continuum supervision and nested groups remain parked; no new executor/broker
or capability-valued environment mechanism is added.

## Adapter qualification (2026-10-12)

Delivery: [userland #239](https://git.internal/PyxisOS/pyxis-userland/pulls/239),
`1dc88c178e659ada4d9a2eabc36ffcad61322bac`, then
[ports #102](https://git.internal/PyxisOS/pyxis-ports/pulls/102),
`f93becb9e5455f19e0389f6dd1b19240bdb38a87`, then
[parent #760](https://git.internal/PyxisOS/pyxis-os/pulls/760).
The accepted design and lifetime record is #757; its history replaces the
completed worklist. No compiler-container rebuild is needed.

Before-code source image: main `36bcd7e9`, userland `3daa2461`, ports `fd1eb30f`,
fs `30f625a2`, lwIP `a1aadb91`, LLVM 23.1.3 builder `2d23443`, LOG_LEVEL=info.
Default image build passed; kernel SHA256
`dca3f0eb181e9ffb92d2d044c82bd62f3288020b0a608437333500ca1fbacf2c`, initrd
`e477a59b2a5492440a17ed7c559d54fcd4574fca5c9f7490055355a712afe4c9`.
The implementation used current-main kernel `9bd3d66d` after #758 and the
dependency heads above. Source SDK/ports/userland/default-image builds passed;
the SDK records pre-qualification-doc parent `6fa9e785` with modified pins, clean
userland/fs sources. Kernel SHA256
`cefb436979e4524b4a9bb9201cd2889c41c32bfde0845acfff30c078d41a3c15`, normal initrd
`674664eeef27689279a1475b3611857e19a36f07bc8242e234a515675e3bc795`.

Before reporting, rebased onto main `4366eb28` after managed libc #756,
with userland main `21cf935c` plus this task (`1dc88c17`), unchanged ports
`f93becb9`. The bundle-schema overlap keeps main's `threads/manage` and adds
only keyboard/pointer/job_work; intrinsic caller-only FS bootstrap is not a
manifest right. Ordinary SDK/ports/userland/image build passed again with the
same compiler. SDK parent `02dbd067` has qualification-doc changes only;
userland/fs are clean. Kernel SHA256
`043fb05d4cfb2534307cd3fdc08fb16db5d624781a0100a48e0084cf5a368846`, normal initrd
`05694c5194be337381cd8d8b41a5a45f925b53f87af87239fb447cfedf072f17`.
Earlier costs remain pre-managed-libc measurements, not an isolated attribution
against the new runtime. Functional rechecks use the rebuilt current image: binary pipelines on one CPU,
local mux and remote plain sessions on four, with Lua error/GC recovery and
libuv FILE/PIPE/detach checks. Earlier split/remote-mux/GDB evidence predates this dependency rebase.

Interactive QEMU 10.2.2 q35/nested KVM, host CPU, 512 MiB, one/four CPUs,
fresh OVMF variables, read-only FAT VirtIO boot disk, VirtIO RNG and PS/2 input.
Local baseline/adapter boots use no NIC/USB and software GOP 1280x800. Separate
four-CPU session checks add VirtIO networking with localhost TCP forwarding;
only that local qualification archive enables Development mux and Remote jobs.
Ordinary images retain their configured gates. Local C consumers and required
manifest fixtures stay in the ignored qualification archive, not the product.

| Check | Measured/observed result |
| --- | --- |
| Ordinary shell launch and pipeline; Lua `pyxis.run` | Same output/native failure behavior; absent new API before implementation. |
| 150,000-byte binary input through `cat` → `cat` | Byte-for-byte captured stdout, empty stderr, both EXITED/0 and all input accepted on one/four CPUs. |
| Early first-stage exit with 200,000-byte input | EPIPE closes only feeder; 4,096 accepted / 195,904 unsent in the sampled runs; remaining output drains and both results survive. |
| Silent child with 50 ms deadline; explicit cancel | Actual TERMINATED result after observed cleanup; policy cause remains separate. |
| Output limit 64 bytes on 20,000-byte input | 64 returned, truncated; sampled stored size 4,096, showing the polled cap's overshoot. |
| Protected Lua error, GC, BUSY close, `pyxis.jobs()` | One retained record; cancel/wait/close recovers it and registry returns to zero. |
| Read-only selected root/cwd; empty roots and absent cwd | Mutation refused; exact minimal echo still succeeds. Positive timeout enforced before effects. |
| Replay to read-only FILE | Actual EXITED/0 preserved alongside write error/uncertain delivery; no replay and no retained completed record. |
| Optional context absent / required context absent | Pure Lua runs; job refuses before admission / client admission refuses respectively. |
| Exact libuv FILE and PIPE selection | Buffered FILE output flushed before child; independent child cursor starts at zero, parent's stays 16; sources survive. Native EXITED/7 and EXITED/4 callbacks; PIPE output/EOF drains. |
| libuv failed spawn / live close | Inactive observerless handles close cleanly; independent WAIT confirms EXITED/9 after detach without an exit callback or hidden stop. |
| Local mux panes, remote plain shell and remote mux | Native captures/pipelines run; remote error/GC recovery works; final session drain completes. |

Read-only GDB inspection confirms the selected memory/threads/clock/random
inventory before the normal intrinsic caller-only FS bootstrap, directional PIPE stdout/stderr, no stdin/roots/cwd/namespace and
owned `X=owned` environment. It also sees a distinct empty RAM leaf while Lua is
alive, normal leaf removal after retirement, local pane subtree removal after
confirmed close, and zero Remote provisioning children after group cleanup.
Unknown admission, creator faults and stalled provider cleanup are source/lifetime
limits, not injected or natively measured cases.

Inline 1000 ordinary `pyxis.run{"cat","tmp://job-empty"}` calls, twice per CPU
configuration: before 3/3 seconds (one CPU), 5/8 (four); after 3/3 and 6/7 in the
initial adapter samples, with 8/6 on the final pre-managed-libc four-CPU image
(userland `143c8954`, ports `f93becb9`). One-second clock quantization and shared-host scheduling
prevent a native latency/speedup claim. Screens/logs/consumers remain under
`build/adapters-baseline` and `build/adapters-qualification`. All owned QEMU,
debugger and remote-client processes are stopped after qualification. No native
hardware, Git/Neovim integration or full guest recipe execution is claimed.
