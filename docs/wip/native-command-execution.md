# Native command execution

Status: **shared milestone contracts accepted 2026-10-11; task 1 merged in #745
and userland #228. Task 2 merged in #749 and userland #232. Task 3 merged in
#754 and userland #235. Task 4 assigned 2026-10-12;
all task 4 decisions are accepted 2026-10-12; implementation is authorized.**
Extract common work from [Claude task 1e](claude-on-pyxis.md),
[Git run-command](git-on-pyxis.md#process-model-and-a-first-cli-slice),
Lua/ports and libuv; no consumer must finish first. Each task needs a separate
owner go. Task 4 is assigned for baseline and surface proposal first; adapter
implementation is authorized under the accepted contracts below. The trusted-caller delegation boundary
below remains accepted.

## Deliverable and boundary

Lua and C consumers can launch native argv commands with selected streams,
capture output, apply deadlines and observe per-stage results and cleanup.
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
  [Implemented contract](../userland/native-jobs.md). Merged.
- [x] **2. Private FILE capture and deadlines.** Implement the profile above for
  direct callers and the trusted Claude group backend; retain storage/observers
  through actual stage retirement, with sizes, truncation and stop cause.
  **After this task the owner can:** capture separate stdout/stderr from silent,
  noisy and failing programs and distinguish timeout from actual termination.
  Implemented in [userland #232](https://git.internal/PyxisOS/pyxis-userland/pulls/232),
  exact pin `40f81eb2299d9eb0f06fc4d6440401d85437e352`; dependency merges first,
  then [parent #749](https://git.internal/PyxisOS/pyxis-os/pulls/749).
  [Implemented contract](../userland/native-jobs.md#private-capture-and-deadlines). Merged.
- [x] **3. Native pipeline and event pump.** Reuse batch launch and bounded pipes,
  concurrent input/output, per-stage results, early-reader-close handling,
  deadlines and all stage/endpoint cleanup. No threads assumption.
  **After this task the owner can:** run a described pipeline with captured output
  without deadlocking on a full stdout/stderr pipe.
  Assigned 2026-10-12; three defaults accepted that day. Merged
  in [userland #235](https://git.internal/PyxisOS/pyxis-userland/pulls/235), exact
  pin `06275d0feeb30c96e142f8e681cee392b1c18b00`; fast-forward dependency first,
  then [parent #754](https://git.internal/PyxisOS/pyxis-os/pulls/754).
  [Implemented contract](../userland/native-jobs.md#native-pipelines-and-the-event-pump).
- [ ] **4. Lua/C delivery and reusable port adapters.** Add the accepted native
  Lua argv/pipeline/capture options and libuv FILE/exact-resource opt-in,
  preserving ordinary `pyxis.run`, `uv_spawn` and loop/callback behavior. Give
  ports native argv run/capture wrappers; full recipes still need tools and the
  required nested-group direction below. Qualify caller exit/fault limits
  separately from session cleanup. Ordinary shell behavior stays unchanged.
  **After this task the owner can:** run/capture direct commands and native
  pipelines from Lua and C, with real deadlines and results.
  Assigned 2026-10-12; baseline recorded, all decisions accepted 2026-10-12; implementation authorized.

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
root, copied private cwd, and console bypass refuse with DENIED. A wrong-type
image refuses direct admission without a record; fresh-group admission returns
an owned record, confirms empty group cleanup, and releases with result_valid
false (no invented process result).

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

## Task 3 checkpoint and accepted choices (2026-10-12)

Fresh branch `execution/native-pipeline` from main `ecdeb0f4`, userland main/pin
`d6842f87`, fs `28386cc7`, ports `28b5a3bc`, lwIP `a1aadb91`. At that checkpoint,
before production code changes, `make -j16 image LOG_LEVEL=info` passed using the existing
LLVM 23.1.3 builder. Ordinary source build; copied port caches were rebuilt as
required, with no PREBUILT substitution. Raw artifacts stay local under
`/home/chronium/src/pyxis-pipeline-artifacts/pre-tls-build/pipeline-baseline`.

Interactive baseline: QEMU 10.2.2, q35/nested KVM, host CPU, 512 MiB, identical
1/4-CPU images, fresh OVMF variables, read-only FAT VirtIO boot disk, VirtIO RNG,
PS/2 keyboard, no NIC/USB and software GOP 1280x800. A local native caller uses
existing batch launch and four directional pipes, explicit memory/clock grants,
try-I/O, mixed waits and native process results. No normal-image test program.

Both configurations complete the full two-way case: 196,608 input bytes accepted,
294,912 stdout and 196,608 stderr bytes drained, expected byte sums, EXITED/0 for
both stages. The input writer reports WOULD_BLOCK 16 times while both output
preambles are drained, so the workload exceeds the 64 KiB pipe capacity. Early
reader exit drains 4,096 stdout bytes, returns producer EXITED/7 and reader
EXITED/0, and reports EPIPE after 0–4,096 input bytes accepted. Silent returns
0/0 with empty output; failing returns 3/0 with exact 16/15-byte streams. Timeout
and the local 1 MiB trigger stop/observe both stages. Trigger output drains to
1,114,112 bytes per stream, illustrating queued tails rather than a hard quota.

Two-sample end-to-end baseline ranges, excluding debugger pauses/output reading
outside the pump; nested-QEMU observations, not native performance:

| CPUs | Full two-way (ms) | Early reader exit (ms) | 250 ms timeout (ms) |
| --- | ---: | ---: | ---: |
| 1 | 82.70–112.13 | 7.18–8.04 | 256.95–257.29 |
| 4 | 57.83–65.95 | 6.33–7.37 | 256.96–258.05 |

The accepted concurrent pump, all stage results, batch publication, absolute
deadline and stop/observe-all rules remain unchanged. Existing 64 KiB pipes,
4 KiB try transfers, eight-stage batch and 32-interest wait suffice. Reuse the
capture inventory policy and private unlinked FILE storage, with distinct PIPE
wiring. The existing single-capture stream shape (absent stdin/FILE outputs) is
not the pipeline shape. Unknown batch admission follows task 2: retain owned
cleanup-unconfirmed storage without replay; sole-CONTROL fresh-group cleanup
never invents stage results. No kernel/ABI or consumer adapter is planned.

**Three defaults accepted by the owner, 2026-10-12:**

1. **Finite owned input snapshot.** Explicitly choose absent stdin or supplied
   finite bytes, copied into the job record before launch/effectful setup. An
   explicitly supplied empty input closes its pipe for real EOF; absence stays
   absent. Allocation failure refuses before launch. The pump feeds it with
   bounded try writes while draining both outputs; no arbitrary live descriptor,
   FILE producer or callback source in this slice.
2. **Final stdout and combined stderr.** Capture the last stage's stdout and one
   combined stderr pipe from every stage into two separate private unlinked RAM
   FILEs, retaining the accepted configurable 1 MiB polled trigger/returned cap
   per stream. Intermediate stdout flows only to the next stage. Return every
   stage's actual result, but promise no stderr attribution, writer atomicity or
   cross-stream chronology. Drain buffered output through EOF after stop or
   stage retirement before final sizes/release.
3. **Early stdin closure is observable, not automatic job cancellation.** On
   EPIPE, close that feeder, report bytes accepted by the pipe and unsent bytes,
   then continue draining/observing every stage. Acceptance does not prove the
   child consumed those bytes. Other transfer errors stop/observe all stages.
   Native callers decide success from the stage array and delivery report;
   existing shell last-stage behavior stays unchanged.

## Task 3 implementation and qualification (2026-10-12)

Userland #235 implements `pyxis/pipeline.h`, shared all-stage preparation and
one batch admission, plus the concurrent pump. It reuses the direct encoder and
capture grant audit; children receive directional PIPE streams, not FILEs or
private-root authority. The ordinary one-child FILE-capture path is preserved.
No kernel/ABI, compiler rebuild, shell/Lua adapter or descendant work.

While this task was active, main #748 introduced static TLS and the published
LLVM image changed. The initial pre-code samples above remain separate. The
matched final comparison refreshes the raw-primitive baseline at main `7db4cb31`,
userland main `2bc56bf4`, ports `fd1eb30f`, fs `28386cc7`, lwIP `a1aadb91`, then
uses task userland `4163f84c` on four CPUs and `06275d0f` on one CPU (the
intervening change only expands a capped-read conditional into the required
style). Both use the published LLVM 23.1.3 image
`pyxis-llvm23.1.3-2d23443` and ordinary `make -j16 image LOG_LEVEL=info`, which
passed. QEMU configuration is unchanged from the checkpoint. Kernel SHA256 is
`40db238acdc04a4f4b373b1882b978a8e27af761b9db7f5d61c392fcfb97ecea` for both;
the baseline default initrd is
`67df4d4bb7845d3307f76b6e00c594bd76099c07c5a580c9db5756f3cc022faf`.
A qualification-only caller and space configuration replace the same payload in
otherwise matched archives. They are local artifacts, not installed tests.

Two-sample end-to-end ranges exclude capped-output reading and debugger pauses.
The raw and library callers use the same child workloads; execution/preparation
and host scheduling dominate these variable nested-QEMU samples. They establish
progress and deadline behavior, not a native or general speed comparison.
The raw baseline counts drained bytes in memory; the library also owns metadata
and appends both private FILE captures. These are not equal storage-cost paths.

| CPUs / path | Full two-way (ms) | Early reader exit (ms) | 250 ms timeout (ms) |
| --- | ---: | ---: | ---: |
| 1 / raw primitives | 96.74–118.26 | 8.42–17.48 | 257.59–263.19 |
| 1 / pipeline library | 201.84–217.08 | 14.85–16.45 | 256.70–275.17 |
| 4 / raw primitives | 59.27–71.37 | 6.92–7.93 | 258.06–262.32 |
| 4 / pipeline library | 38.02–62.80 | 8.04–8.97 | 256.97–259.95 |

Both one- and four-CPU paths complete the full two-way workload above with exact
sizes and byte sums, all stage results valid and EXITED/0. The implementation
caller overwrites its source input immediately after start; the output still
contains the original bytes, establishing the owned input snapshot. Early exit
retains 4,096 stdout bytes with producer EXITED/7 and reader EXITED/0; the feeder
reports its confirmed prefix and unsent remainder without job cancellation.

Silent/failing children return exact empty/16-byte stdout and empty/15-byte stderr
with actual 0/0 and 3/0 exits. Explicit empty input returns EOF; absent input
returns the child's input error exit (9), not EOF. Eight-stage two-way execution
completes with all eight EXITED/0 results. Deadline and explicit cancel observe
both actual TERMINATED results; early close is BUSY, repeated wait returns the
same snapshot and late cancel/close succeed. Fresh-group full two-way, early exit,
timeout and cancellation also complete. Both ordinary and fresh-group single
FILE captures retain their prior 16/15-byte output and EXITED/3 result.

The 1 MiB trigger stops both stages and drains queued tails; four-CPU backing
sizes were 1,114,112 stdout and 1,118,176 stderr bytes, with 1,048,576 returned
from each and both truncation flags. This is intentional polled overshoot, not
a quota. Wrong-kind image, oversized metadata, expired deadline and private-root
delegation refuse with no returned record. A native batch with an empty image
at stage 1 refuses BAD_REQUEST atomically.
The direct path returns no record; the fresh-group path retains and confirms
group cleanup, reports failed_stage=1 and both stage_valid flags false, then
allows release without inventing results.

GDB separately inspected a live fresh-group timeout, then stopped immediately
before release. Both retained process controls were complete with NULL process
links and no waiters. The sealed/stopped group had one controller, zero members,
launches and cleanup pending, and complete=true. Input/output PIPE roles were
closed with no pending append bytes. Both RAM FILEs remained owned, with no
operation waiters, and the private RAM directory was empty throughout. Children
had directional PIPE streams and no private root, launcher or CONTROL grant.
Debugger pauses are excluded from the timing samples.

Source review covers uncertain admission and transport-failure cleanup; no
transport-failure injection or native hardware run was performed. The accepted
caller-death/admission/retirement limits remain. Raw callers, screens and logs are
local under `build/pipeline-current-baseline` and `build/pipeline-after`; original
pre-TLS artifacts are preserved separately as noted above. Task 4 was not started
at that checkpoint.

## Task 4 surface proposal (2026-10-12)

Assigned after #754 and userland #235 merged. **All three decisions accepted by the owner,
2026-10-12. Implementation is authorized; this PR records documentation only.** Ordinary shell commands/pipelines,
`pyxis.run` and `uv_spawn` retain their behavior.

Future shell capture waits for [capability-valued variables](capability-environment.md), with a held result rather than an implicit current-job slot.

### Before-code baseline

Fresh worktree `pyxis-job-adapters`, branch `execution/adapters-surface`, main
`50140b52`, userland `06275d0f`, ports `fd1eb30f`, fs `28386cc7`, lwIP `a1aadb91`.
Reused the task-3 final source-built normal image with those exact dependency
pins, LLVM 23.1.3 `pyxis-llvm23.1.3-2d23443` and `LOG_LEVEL=info`. Checked that
tracked kernel/ABI/toolchain/build inputs and all dependency source pins match
fresh main; the intervening parent changes are documentation/merge history.
No stale implementation or modified runtime is substituted. Kernel SHA256
`41463360d4b55767218ccb75ff08eac9f8db4ab990159fb8a2a622a82d6253db`, initrd
`354f5d4c802d1e3d595caa6a43d26537ce0d6026076c2b3ebe96c04a511a2b3b`.

Interactive QEMU 10.2.2, q35/nested KVM, host CPU, 512 MiB, one/four CPUs,
fresh OVMF variables, read-only FAT VirtIO boot disk, VirtIO RNG, PS/2 keyboard,
no NIC/USB, default software GOP 1280x800. Both configurations pass ordinary
`echo`, `echo pipeline | sha256sum` and `pyxis.run{"echo","lua-direct"}`. A
missing-file `cat | echo` reports the first-stage EXITED/1 while retaining
last-stage success; ordinary Lua returns 1 for that `cat`, and missing program
lookup raises its existing error. `pyxis.job`/`pyxis.pipeline` are nil.

After creating an empty `tmp://job-empty` with existing Lua `io.open`, ran this
inline workload twice per CPU configuration, without a new test/benchmark tool:

```lua
local a = os.time()
for i = 1, 1000 do assert(pyxis.run{"cat", "tmp://job-empty"} == 0) end
print("runs", 1000, "seconds", os.time() - a)
```

Observed 3/3 seconds on one CPU and 6/6 on four CPUs. These have one-second
clock quantization and shared-host/nested-KVM scheduling variation, not native
launch latency or a performance acceptance threshold. Repeat unchanged ordinary
calls after implementation; new capture/loop semantics need their own functional
qualification. Raw screens/logs stay in `build/adapters-baseline`; owned QEMU
processes are stopped. The task-3 core's richer baseline remains above.

### 1. Declared context grants — accepted 2026-10-12

**Owner accepted 2026-10-12:** `jobs = true` space opt-in (false when
absent), private RAM `job_work` directories, and the minimal captured-stage
resource profile below. No per-invocation wrapper, prefix, keyword or launch
option; users type `lua script.lua`, `nvim`, or another client normally.

Use the existing [bundle manifest grant model](../userland/program-bundles.md#app-views-grants-and-lifetime).
An execution client declares `job_work` plus the existing `launcher/launch`,
`pipe/create` and runtime grants it needs. Extend the recognized manifest
resource set with `job_work`: a native DIRECTORY resource with `create`,
`remove`, `read-files`, `write-files`. This is a real grant set over existing
interfaces, not a new JOB protocol or service. `jobs = true` makes context
provisioning available to the trusted launch path; the declaration requests it.
The manifest alone supplies no authority. The gate controls this new context,
not existing ordinary launch permissions. Default packaged Development opts in;
other spaces retain an explicit choice.

For example, the Lua bundle's job-related requests are optional so pure Lua
still starts where jobs are unavailable:

```json
{"grants": [
  {"name":"job_work","resource":"job_work", "required":false,
   "rights":["create","remove","read-files","write-files"]},
  {"name":"launcher","resource":"launcher","rights":["launch"],"required":false},
  {"name":"pipe","resource":"pipe","rights":["create"],"required":false}
]}
```

These are entries in the existing `grants` array, alongside memory/clock and
other declared ordinary Lua resources. A client whose operation requires jobs
marks the relevant requests required. Apply today's grant-all-when-available
policy: unavailable optional context is absent and the job API refuses before
launch; an unsatisfied required request refuses client admission. Future
required/optional first-launch consent follows the accepted [bundle grant direction](program-bundles.md#future-consent-policy), without implementing consent now.

Trusted boot/session/mux/remote startup retains a private RAM provisioning tree,
outside public roots, cwd and namespaces. The trusted launcher reads the
client's explicit declaration, checks the space gate and its selected authority,
then creates a fresh `job_work` directory for that launch. It requires usable
clock/PIPE/LAUNCH selections too; optional context is absent if the complete
context cannot be supplied. Shell delegation of LAUNCH comes only from
`child_launcher`, never its own supervision launcher or a `launch = false`
bypass. The client receives the leaf grant; the provisioner retains the trusted
ancestor. Ordinary children get no automatic `job_work`, ancestor or alias;
only an explicit declaration through a context-authorized launch path can
request a new context. Captured stages receive no `job_work`, provisioning
ancestor or launcher; their audited resource selections follow the profile below.

The creator owns the leaf until client retirement, removes private entries/link
and closes it afterward. Uncertain client admission retains the context until
session teardown, without replay or invented retirement. Existing boot init's
private RAM backing suffices; neither a kernel RAM factory nor shared `tmp://`
is used. Captures are created then unlinked inside the leaf.

Package the interpreter as the normal `lua.pxb` command, with its native binary
inside `app/`; remove the top-level `lua.pxe` candidate that currently shadows
bundle lookup. Ordinary `lua script.lua` therefore resolves the declaration,
without an alternate command name or flag. Other native clients use their
normal bundle entry; trusted C embeddings can explicitly supply the same grants.
No inference from an executable basename or argv, and no implicit forwarding.

The adapters construct each final inventory explicitly. Default runtime roles
are the **present** memory, threads, clock and random grants; selected content
roots/cwd and string environment are copied from the caller. Network, PIPE
creation and system-info roles require explicit selection. Never forward the
ordinary terminal/display/log/audio/clipboard/power/launcher inventory into a
captured stage. Initial symbolic resource selectors are `memory`, `threads`,
`clock`, `random`, `tcp`, `udp`, `pipe`, `system_info`; each selects an already
held grant with its documented native role rights, intersected with held rights.
Explicit missing or refused selections fail. Root selection can narrow to
read-only; rebuild/attenuate the entire cwd chain under those selected roots too,
or refuse an incompatible cwd before effects. Copying old write-capable cwd
handles would undo that attenuation. The core audits every final grant; the
trusted caller owns namespace/provider and credential reachability throughout
the job.

New job lookup selects native `.pxe` files (bare bin, then boot on absence) or
explicit native FILE paths. Bundle/catalog and shebang expansion remain on the
ordinary paths until their final expanded inventories are explicitly adapted
and audited; never forward an ordinary bundle's launcher into this profile.
Graphical/interactive programs needing bypass grants refuse this captured mode.

### 2. Lua surface — accepted 2026-10-12

Accepted for Lua only; no shell command, builtin, capture syntax or current-job
slot. Native status integers and owned job objects retain the accepted options
and result shape:

```lua
job, status = pyxis.job(argv, options)
job, status = pyxis.pipeline({argv1, argv2, ...}, options)
result, status = job:step()
result, status = job:wait()
status = job:cancel()
bytes, status = job:read("stdout" or "stderr", offset, count)
status = job:close()
jobs = pyxis.jobs()
```

Both entry points capture; `pyxis.job` is a one-stage convenience. Options are
`timeout_ms` (required), binary-string `input` (nil means absent; `""` means EOF),
`stdout_limit`, `stderr_limit`, `resources` (exact role list), `roots` (list of
`{name=..., access="read" or "read-write"}`), `cwd` (path or false for absent),
`environment` (exact string map) and `namespace` (false by default; true explicitly
selects held LOOKUP, with the trusted audit above). Omitted roots/cwd/environment
use owned current snapshots. Roots and resource names select held grants;
paths/strings confer no authority. NUL in argv, path, environment or selector
text refuses; finite input remains binary. Dense argv/stage arrays retain native
metadata limits. Stages use the same explicit profile; per-stage arbitrary
capability construction is not part of the Lua binding.

Results mirror the core's stage validity, actual kind/exit status, admission/
operation status, local stop, sizes/caps/truncation/incomplete flags and input
acceptance/unsent/uncertainty. A nonzero child exit is data, not a Lua exception.
Argument errors may raise before effects; native start can return an owned job
**and** a nonzero status. Read is explicit, bounded and available only after
confirmed cleanup. No job/observer/FILE handle number appears in text.

### 3. Adapter ownership and port/loop integration — accepted 2026-10-12

Unknown direct admission stays quarantined until caller/session teardown,
without normal release or replay.

Lua keeps a strong native ownership registry from before admission until the
native close consumes the record. A completed close attempt can consume it while
returning an error: invalidate/unregister then, never retry that release. BUSY
preserves it. Losing a Lua variable, GC or a protected Lua error does not
release a live observer or capture. Explicit `cancel`, `wait` and `close` own
normal retirement; GC never performs an unbounded blocking wait. Unreached
records remain held until caller/session teardown. `step`/`wait` drive the pump
and policy: there is no hidden background producer or automatic deadline worker.
A completed record returns stable repeatable snapshots. Killing/exiting Lua
still has the accepted session-group fallback, without a per-job caller-death
stop guarantee. A captured stage receives no descendant launcher.
`pyxis.jobs()` returns the still-owned userdata records for explicit recovery
after a protected Lua error; it prints no handles and invents no background work.

Port-facing pure-Lua adapters reuse those entry points:

```lua
local commands = require "pyxis.commands"
result, status, job = commands.run(argv, options)
stdout, stderr, result, status, job = commands.capture(argv, options)
stdout, stderr, result, status, job = commands.pipeline(stages, options)
```

These synchronous conveniences wait, read capped binary outputs and close after
confirmed cleanup. `run` replays captured stdout/stderr to selected caller outputs
after completion: current `io.stdout`/`io.stderr`, or explicit writable
`options.stdout`/`options.stderr` Lua FILEs. Read/replay/flush errors return an
operation error alongside the actual child results, including partial delivery;
never replay uncertain writes. Capture/pipeline return both streams without
trimming. The optional final `job` is nil when ownership was consumed, otherwise
the retained record for explicit cancel/wait/close. They retain unresolved cleanup
in the same ownership registry and never rerun a failed/uncertain command.
Nonzero stage exits remain in the result. The host ports runner extracts its existing shell boundary into a host-only adapter,
preserving current recipe `ctx.run`/`ctx.capture` behavior, including the latter's
single trailing-LF removal. The native adapter never calls `os.execute`/`io.popen`
or turns argv into shell text. Full native recipes/make/driver-launched tools
remain blocked on actual tools and nested groups; this is not a recipe runner
qualification or a broker workaround.

Accepted libuv opt-in:

```c
int uv_pyxis_spawn_job(uv_loop_t *loop, uv_process_t *process,
    handle_t launcher, const struct pyxis_job_options *command,
    uv_exit_cb exit_cb);
```

It selects the complete native image/argv/grants/resources/roots/namespace/cwd/
environment and live descriptor inventory, replacing additive inheritance.
FILE streams use libc's bridge/selected-output flush and independent child
cursor semantics. The caller can prepare/register its own PIPE endpoints with
the loop. Ordinary `uv_spawn` and additive `uv_pyxis_spawn` stay unchanged.
Reuse shared libc preparation through an explicit observer-returning entry:

```c
enum call_status pyxis_job_launch_observer(handle_t launcher,
    const struct pyxis_job_options *command, handle_t *observer);
```

This returns the owned native WAIT|TERMINATE observer directly to the loop,
without adopting the owned-job record's BUSY-close contract. Its preparation,
final grant audit and no-replay rule are the same direct core. Observer output
is invalid on failure; source grants/descriptors survive. Failed UV spawn
handles still require the usual close. Loop readiness,
actual native exit reason/callback and observer-only `uv_close` remain intact;
close does not become cancellation or hidden blocking. Capturing job records
retain their stronger explicit stop/observe/release rules.

### Relation to parked directions and qualification

Jobs are local owned records, not environment values. Startup grants and Lua
userdata remain real native ownership; no handle is printed/parsed, smuggled in
argv/environment or converted into a capability-bearing string. The resource
selectors only name authority already held by the caller. Task 4 implements
neither capability-valued environment inheritance nor prepared-request bindings.
Their object/rights/transfer design remains separately parked.

Every explicit stage is directly launched through the client's existing launcher.
Captured children receive no launcher; no nested group, outside-group executor
or service protocol is added. Native result arrays stay available to Lua/C
consumers. This does not implement prepared HTTP objects or shell bindings.
Nested execution groups remain required before real descendant jobs and
transitive caller-death cleanup.

After acceptance, qualify both new adapters and unchanged ordinary paths in
interactive one-/four-CPU QEMU/GDB: full two-way and early exit, timeout/cancel,
limits, FILE cursor/flush behavior, exact child inventories, Lua errors/GC/explicit
close, native UV loop exit/detach and port-wrapper binary output/results. Caller
exit/fault limits are checked separately from eventual session cleanup. Publish
userland/ports dependencies before the parent pin; native implementation still
is authorized under the accepted decisions.

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
