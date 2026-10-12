# Native command execution

Status: **shared milestone contracts accepted 2026-10-11; task 1 merged in #745
and userland #228. Task 2 delivered for review by Codex alpha 2026-10-12; the exceptional
lifetime decision is accepted and implementation is authorized.**
Extract common work from [Claude task 1e](claude-on-pyxis.md),
[Git run-command](git-on-pyxis.md#process-model-and-a-first-cli-slice),
Lua/ports and libuv; no consumer must finish first. Each task needs a separate
owner go. Task 2 has the owner go under the accepted capture profile; tasks 3–4
remain unassigned. The trusted-caller delegation boundary below remains accepted.

## Deliverable and boundary

Lua and the shell can launch native argv commands with selected stdin/stdout/
stderr, capture output, apply deadlines and observe per-stage results and cleanup.
The shell already runs native pipelines; the new work is reusable capture,
deadlines, per-stage observation and Lua access, including a concurrent I/O pump.
Use existing FILE, PIPE, CONSOLE, launcher and process contracts. No POSIX shell,
fork, numeric PID killing, synthetic signals or successful `system`/`popen`
substitutes. The shell owns its parser; native callers pass argv/stage arrays.

Consumers are the [Claude harness](claude-on-pyxis.md),
[Git executable children and remote helper](git-on-pyxis.md#process-model-and-a-first-cli-slice),
[Lua](../userland/lua.md), the [ports runner](../development/ports.md#building-and-packaging),
[source builds](source-builds.md) and [libuv/Neovim jobs](neovim-libuv.md).
The first shared profile is direct programs and explicitly described pipelines,
with no child launch authority. Descendant containment is the required shared
follow-up below; Git's fork callbacks/sideband adaptation, PTYs, background jobs
and full build-tool ports remain consumer work. No HTTP, credential-store, thread or FAT32 implementation.

## Inspected foundation and direct-launch assessment

Main `69047eee`, userland `d560fbb0`, ports `df3210b0`; source inspection only.
[Shell pipelines](../userland/shell-streams.md) already select three streams and
use atomic batch launch for up to eight stages. FILE descriptors start at offset
zero; PIPE/CONSOLE are sequential. Delegation transfers no FILE cursor or input
read-ahead. Flush selected buffered output before delegation; borrow live stdio,
not closed immutable startup entries. Absent stdin returns EBADF, not invented EOF.
[Pipes](../interfaces/pipes.md) have try-I/O and mixed readiness waits.

**Direct launch works for this first profile.** [Launch preparation](../../kernel/user/spawn.c)
returns WAIT|TERMINATE for every child. [Process TERMINATE](../../include/abi/process.h)
uses safe stop, covers that process's threads and blocked-operation unwind,
and preserves an exit/fault already committed. With no launcher, space factory
or launch-broker bypass delegated to children, the explicitly listed stages are the whole job.
Terminate each stage, then observe WAIT_COMPLETE and retrieve its real result.
[Foreground Ctrl+C](../../userspace/shell/launch.c) already follows this pattern.
No CREATE_GROUP, nested group or same-space executor is required for ordinary jobs.

A group-bound client's children remain in its existing pane/session group;
[session group termination/completion](../interfaces/execution-groups.md) therefore
covers them without separate sibling-group accounting. Per-process WAIT observes
process/task/VM/grant-table reclamation, **not all deferred object destruction**;
full attributed cleanup remains the group's completion boundary. An open session
group cannot be waited to completion after each job. Release the caller's captures
and pipe endpoints after stage completion; report that boundary honestly.

Closing an observer does not stop execution. Normal cancel/deadline/error paths
must explicitly terminate and observe every admitted stage. If the caller faults
or is forcibly stopped first, children can survive until their enclosing session
group stops; a surviving session does not automatically stop them on a Lua caller's
fault. Ungrouped callers have no such fallback. This profile does not promise
per-job fail-closed behavior on caller death. The accepted trusted Claude harness
keeps its stronger sole-CONTROL fresh-group lifetime. Termination never rolls back committed effects,
and published HOST work can delay retirement indefinitely.

Lua `pyxis.run` inherits live streams, cwd/roots/environment and ordinary resources,
blocks on process completion and lacks capture/deadlines. `os.execute`/`io.popen`
and libc `system`/`popen` are absent; the host ports runner quotes argv into shell
APIs. Libuv already launches pipe children asynchronously but rejects inherited
FILEs and adds resources to its inherited inventory instead of replacing it exactly.

## Accepted first profile (owner, 2026-10-11)

Use the client's existing, possibly bound launcher and per-process WAIT/TERMINATE.
Job children receive **no descendant launcher in this milestone** and stay inside
that client's group and cleanup when it is group-bound. No same-space executor,
sibling-group task or new job-lease protocol. Keep the caller-death, no-descendant
and deferred-cleanup limits above. A local owned job record retains each observer;
normal cancel/error paths explicitly stop and observe all stages.

Use explicit image READ, argv, selected roots/namespace, owned cwd/env snapshots
and exact child resources. Reject direct space-factory, administrative and launch
grants. The trusted caller must omit launch-capable brokers, including through
bundle/namespace resources; never add authority implicitly or replay an uncertain
launch. Libc owns descriptor/FILE
bridging, userland execution ownership, and Lua/ports application bindings.
Preserve ordinary `pyxis.run`/`uv_spawn` behavior and the accepted Claude harness's
trusted fresh-group/sole-CONTROL backend, which ordinary direct jobs do not need.

## Accepted capture and pipeline contract (owner, 2026-10-11)

1. **First capture keeps the accepted Claude storage/authority profile.** Separate
   private RAM stdout/stderr FILEs are created then unlinked under an explicitly
   provisioned private work root; shared `tmp://` or a filename is not privacy.
   Give children stream WRITE and selected project/runtime/network grants; omit
   private roots, credentials, descendant launchers, named console input/output,
   keyboard/pointer/display and terminal-service bypass grants. Absent stdin is
   deliberate. Claude seals its fresh group; ordinary direct jobs use each stage's
   process observer within the client's existing group.

   Return capped bytes with truncation and local stop cause alongside actual
   EXITED/FAULTED/TERMINATED results; fault details remain in klog. Default to the
   accepted configurable 1 MiB per-stream polled trigger: consumer
   policy, not a hard backing quota or universal output cap; it can overshoot.
   No stdout/stderr chronology is promised. Use one absolute job deadline with
   bounded native waits; request stop on deadline/size/cancel, then observe actual
   stage retirement (and the Claude group when used) before releasing captures.
   Check expiry when launch returns: synchronous HOST/image preparation cannot
   be interrupted by a userland deadline, so admission and cleanup have no hard
   wall-time bound. A stop race may yield an already committed normal exit or fault.
2. **Pipelines pump concurrently and return all stage results.** Use explicit
   stage arrays, batch publication and directional pipes; close unused endpoints
   promptly and pump input/stdout/stderr with try-I/O/readiness. Avoid serial
   blocking drains or waiting before draining a full pipe. Short counts, EOF,
   EPIPE and transfer failure stay distinct; progress never refreshes the deadline.
   Check request/attachment and launch metadata limits before effects. Failed
   batch publication creates no children, but prior FILE creation/truncation is
   not rolled back. Stop/observe each stage on cancellation or failure. Preserve
   shell last-stage success by default; native callers and prepared HTTP
   redirection can require all producers to succeed.

## Ordered delivery tasks

- [x] **1. Small first task: one-child direct job core.** Explicit streams and
  resource lists, owned cwd/env, native result, terminate and stage retirement
  over existing launcher/process APIs. No Lua, shell grammar, service protocol or
  capture policy. Qualify normal/failing/stopped children and observer ownership.
  **After this task the owner can:** launch one known native program with selected
  streams from a LAUNCH-authorized caller, inspect its result and stop it.
  Implemented in [userland #228](https://git.internal/PyxisOS/pyxis-userland/pulls/228),
  exact pin `20fb34f871beed7e25f6ac454e0d53f51dba5f3f`; merge dependency before parent.
  [Implemented contract](../userland/native-jobs.md). Merged; task 2 is now assigned.
- [x] **2. Private FILE capture and deadlines.** Implement the profile above for
  direct callers and the trusted Claude group backend; retain storage/observers
  through actual stage retirement, with sizes, truncation and stop cause.
  **After this task the owner can:** capture separate stdout/stderr from silent,
  noisy and failing programs and distinguish timeout from actual termination.
  Implemented in [userland #232](https://git.internal/PyxisOS/pyxis-userland/pulls/232),
  exact pin `40f81eb2299d9eb0f06fc4d6440401d85437e352`; dependency merges first,
  then [parent #749](https://git.internal/PyxisOS/pyxis-os/pulls/749).
  [Implemented contract](../userland/native-jobs.md#private-capture-and-deadlines).
- [ ] **3. Native pipeline and event pump.** Reuse batch launch and bounded pipes,
  concurrent input/output, per-stage results, early-reader-close handling,
  deadlines and all stage/endpoint cleanup. No threads assumption.
  **After this task the owner can:** run a described pipeline with captured output
  without deadlocking on a full stdout/stderr pipe.
- [ ] **4. Lua/shell delivery and reusable port adapters.** Add native Lua argv/
  pipeline/capture options and shell capture/deadline/per-stage observation with
  existing launch authority. Preserve ordinary run/pipeline behavior. Carry
  exact per-stage results into HTTP redirection coordination. Offer libuv FILE/
  exact-resource selection as opt-in, preserving ordinary `uv_spawn` inheritance
  and loop/callback behavior. Give ports native argv run/capture replacements
  for host `os.execute`/`io.popen`; full recipes still need tools, adaptation and
  the required nested-group direction below. Qualify caller exit/fault limits separately from
  session cleanup.
  **After this task the owner can:** run/capture direct commands and native
  pipelines from Lua or the shell, with real deadlines and results.

## Task 1 checkpoint (2026-10-12)

Branch `execution/direct-job`, fresh main `088816ab`, userland `e878f4f9`
(also dependency main), ports `28b5a3bc`, fs `b427df29`, lwIP `a1aadb91`.
Before-code build: `make -j16 image PREBUILT="sdk ports" LOG_LEVEL=info`,
existing LLVM 23.1.3 builder. Verified SDK/ports bundles retain their recorded
task-1 pointer provenance; current public ABI and userland libc/lib/include/crt
are unchanged from those inputs. Kernel and current userland apps are freshly
built; no implementation or pin changes. Kernel SHA256
`655ba0a3e03ff295146dc5e7f44cc68423263802ea082462184a572d044a03fe`,
initrd `f94e987b6cc5bdeb833ded76968a382456009ef9ced7c135179ec14d3e931d3a`.

Interactive QEMU 10.2.2 Q35/nested KVM, host CPU, 512 MiB, separate 1/4-CPU
boots with identical images, fresh OVMF variables, read-only FAT VirtIO boot
disk, virtio RNG, PS/2 keyboard and no NIC. Existing shell one-stage launches
produce the same native results in both configurations:

| Child | Result |
| --- | --- |
| `echo baseline` | EXITED, 0 |
| `cat boot://missing-file` | EXITED, 1 |
| blocked `cat`, then Ctrl+C | TERMINATED, 0 |

GDB sees a detached process link before each completion publication. The stop
request has WAIT|TERMINATE rights, targets the still-live process and does not
itself report retirement. This establishes the existing stage boundary, not
group/deferred-object cleanup or isolated launch cost. Shell launch currently
allows descendants where configured; it is a baseline, not the new restricted
core. Raw logs/debugger output/screens stay local in `build/direct-job-baseline/`;
all owned QEMU/debugger jobs are stopped.

**Trusted-caller boundary, accepted by the owner 2026-10-12:** the core authenticates
and rejects known direct launch/administrative grants across the **entire final
grant array**, not just named resources, and adds nothing implicitly. A trusted
caller is responsible for provisioning/auditing selected namespace bindings and
provider semantics for the job's lifetime. Accept a known native image and final
explicit inventory; bundle lookup/manifest expansion remains outside the core.
This is a trusted delegation policy, not a kernel-enforced transitive sandbox.

[Handle metadata](../../include/abi/handle.h) reports protocol/kind/rights/
transport, without provider identity or behavior. [Namespace LOOKUP](../interfaces/namespaces.md)
does not enumerate, filter or freeze bindings; another management holder can
change them. [Endpoint transfers](../interfaces/endpoints.md) can return grants,
and opaque providers can broker effects under their own authority. The kernel
[installs every grant entry](../../kernel/user/spawn.c), including unnamed ones.
Proving arbitrary providers cannot launch would need new mechanisms outside
task 1. This acceptance authorizes task 1 implementation, without new kernel
capability/environment/group interfaces or bundle expansion in the core.

Implementation is based on updated main `48219eca`, with userland `2ccb58eb`
and fs `b5003ab3`; the baseline above predates that update. Public ABI and
userland libc/lib/include/crt are unchanged across the baseline and updated
main. The implementation build rebuilds the SDK from its current inputs.

## Task 1 implementation and qualification (2026-10-12)

Libc's `pyxis/job.h` owns one native child and its WAIT/TERMINATE observer.
Preparation deep-copies explicit metadata, appends directional live-descriptor
grants, authenticates every final grant (named or unnamed), then retains exact
restricted copies through synchronous launch capture. Existing capture/startup
and stack bounds are checked before selected output flushing. The observer is
allocated before publication; successful wait caches the actual native result.
Close refuses with BUSY before successful wait and releases once afterwards.
Malformed single-launch replies now report OUTCOME_UNKNOWN, matching batch
launch; automatic replay remains prohibited.

Ordinary `make -j16 image LOG_LEVEL=info` passed with a fresh SDK/runtime and
ports build; no compiler-container rebuild. Interactive qualification uses the
same QEMU/device/1- and 4-CPU configuration as the baseline. A local native caller
is added only to the qualification archive, not the delivered image or a test
framework. It supplies the selected memory grant explicitly; the failing `cat`
also receives the boot root explicitly. Both CPU configurations show:

| Check | Observed result |
| --- | --- |
| `echo direct-job` | EXITED, 0; selected buffered output precedes child output |
| `cat boot://missing-file` | EXITED, 1; missing-file diagnostic, as baseline |
| blocked `cat`, explicit terminate twice | TERMINATED, 0; requests acknowledged without replacing result |
| observer ownership | WAIT/TERMINATE retained; WAIT-only copy cannot stop; close before wait BUSY |
| retirement | repeat wait stable; WAIT_COMPLETE ready; independent WAIT copy works after job close |
| explicit state | child has selected memory only, no launcher, no roots; saved `tmp://` cwd and environment survive parent mutation |
| FILE stdin | child reads `ab` from offset zero; parent's offset remains 3 and next byte is `d` |
| absent stdin | child read fails with EBADF; no console input is added |
| unnamed launcher grant, including zero rights | DENIED; no job/child returned |
| READ-only descriptor selected for output | DENIED; no job/child returned |

GDB sees the exact captured inventory, zero cwd/environment/namespace when
omitted, LOOKUP-only selected namespace, and distinct directional stream grants.
Stop on a live process requests safe retirement; completion publication has a
detached process link. These are stage ownership/results, not group or all
deferred-object cleanup. Uncertain publication and caller-death limits are source
contracts; no fault injection or native-hardware measurement is claimed.

Qualification kernel SHA256
`fa5ac5994f455633b0cc69ed4a900683340eef8c423fc1e383e947bc8ddf93dc`,
ordinary initrd `c4e8c1a94311a0a8354fcd49b288b39c3aef09ed89fc1663010f3d57a2ed2fea`,
local caller archive `8b2ac7d4623f42ceb760ce9cb6cb4799cf6ec3edb1229d7c30d2bfd73c1221b9`.
Raw caller source, screenshots and GDB/log records remain local in
`build/direct-job-qualification/`. No default klog output, kernel/ABI changes,
Lua/shell adapters, captures, deadlines, pipelines or descendant groups were added.
Before opening the parent PR, the branch rebased cleanly onto main `5dc08c5b`
(fs `9b4479e8`). This brings the independent FAT32 validation update and docs;
the public ABI and direct-job/runtime sources used for the checks are unchanged.
The ordinary image is rebuilt for that integrated input set and exact-head CI
qualifies the published dependency pin.

## Task 2 qualification (2026-10-12)

**Accepted exceptional lifetime (owner, 2026-10-12):** unknown admission without
an observer retains captures in an owned cleanup-unconfirmed record until
caller/session teardown. Normal release and launch replay refuse. A retained
fresh group stops/waits cleanup, leaving the native process result unknown.
Implemented; uncertain transport publication is source-reviewed, not injected.

Baseline was sealed before production code: main `1536d997`, userland `20fb34f8`,
fs `9b4479e8`, ports `28b5a3bc`, lwIP `a1aadb91`; ordinary info-level image build
passed. A local native caller received a private RAM `work` volume exclusively
from trusted boot init, then manually created/unlinked FILEs, selected WRITE-only
streams and used existing job/clock/wait APIs. Silent/failing/noisy/20 s sleeping
children exercised a 1 MiB trigger and 250 ms timeout (otherwise 5 s).

Implementation was built from current main: `173f1321`, userland #232 above and
fs `28386cc7`; ports/lwIP unchanged. The final source image build passed. The
one-CPU implementation samples preceded the unrelated FAT32 integration rebase;
the four-CPU samples use the published capture revision. Capture source is the
same. Local qualification archives differ only in caller/config, with unchanged
baseline payloads. No test program or framework is installed in normal images.

Matched interactive QEMU 10.2.2: q35, nested KVM/host CPU, 512 MiB, fresh OVMF
variables, read-only FAT VirtIO boot disk, VirtIO RNG, no NIC/USB, PS/2 keyboard,
1280x800 software GOP and LOG_LEVEL=info. Both 1 and 4 CPUs pass direct and fresh
group silent/failing/noisy/timeout cases, cancellation, capped short reads/EOF,
repeated waits, late cancellation, and once-only release. Silent is EXITED/0;
failing is EXITED/3 with exact 16/15-byte stdout/stderr; noisy and timeout return
actual TERMINATED with separate limit/deadline causes. Noisy reads return exactly
1 MiB per stream with truncation, verified `O`/`E` bytes. Early close is BUSY.
Already-expired deadlines return TIMED_OUT without a record. Unnamed private
root, copied private cwd, and console bypass refuse with DENIED.

End-to-end timings include preparation and retirement, exclude capped-output
reading and debugger pauses. One-CPU ranges are three samples; four-CPU ranges
two samples. These are nested-QEMU policy observations, not native speed claims.

| CPUs / path | silent (ms) | failing (ms) | noisy stop (ms) | 250 ms timeout (ms) |
| --- | ---: | ---: | ---: | ---: |
| 1 / existing primitives | 4.16–6.42 | 3.61–4.87 | 54.75–171.82 | 255.24–261.70 |
| 1 / capture wrapper | 3.75–4.65 | 3.86–4.93 | 60.09–69.14 | 253.55–255.36 |
| 4 / existing primitives | 4.54–5.36 | 3.46–5.34 | 48.11–54.16 | 254.42–255.87 |
| 4 / capture wrapper | 4.48–7.81 | 3.75–5.61 | 60.87–62.05 | 254.38–254.41 |

GDB separately inspected a live fresh-group timeout and confirmed retirement.
The child holds WRITE-only output FILE grants and no private root, launcher or
CONTROL grant. The private RAM directory is empty while both FILEs remain
retained. Before release, the process control is complete with a NULL process
link and no waiters; the group has one controller, zero members/launches/cleanup
pending, and complete=true. The capture still owns both FILEs and its observer.
Debugger pauses are excluded from the timing table.

Polled backing overshoot is intentional and variable with scheduling. The
one-CPU baseline retained 1,159,168–1,417,200 stdout bytes; the wrapper retained
1,318,912–1,368,064, but returned only 1,048,576. Metadata/policy calls can park;
no backing quota, admission or cleanup wall-time guarantee is implied. Source
review covers unknown-without-observer quarantine and retained-group cleanup;
no transport-failure injection or native run was performed. Raw screens, logs
and local callers remain under `build/capture-baseline`/`build/capture-after`.

## Required descendant follow-up

[Nested execution groups](nested-execution-groups.md) are a parked **required**
direction accepted by the owner on 2026-10-11. They precede Git descendant helpers,
Clang driver-launched tools and real make/ports builds. They replace the former
executor-first proposal; they are not an optional broker workaround or part of
this milestone's implementation. A per-process stop cannot cover unknown
children; the future subgroup tree must keep them in the parent's stop/cleanup
boundary. No nested-group code is authorized here.

Assigned tasks use ordinary builds and interactive 1/4-CPU QEMU/GDB, with matched
baselines where existing paths change. Exercise denied/absent streams, exact
resources/no launch bypass, FILE cursor independence, short/full two-way pipes,
silent children, early reader exit, uncertain publication, deadline races,
caller death and session stop. Inspect owned handles/registrations and distinguish
stage retirement from group cleanup.
