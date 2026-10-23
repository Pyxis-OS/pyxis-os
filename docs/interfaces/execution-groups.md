# Execution groups and launch containment

Execution groups bind descendants to one supervision lifetime independently of
terminal sessions, process observers and spaces. Creation, membership, sealed
launch admission, safe termination and cleanup completion are implemented.

## Creation and launch authority

LAUNCH permits `launcher_create_group()` using the header-only CREATE_GROUP
operation. There is no separate privileged creation right. An unbound launcher
creates a root in an ungrouped caller's space; a bound launcher creates a
subgroup of its binding. The creator remains outside the new group. Two owned
handles return atomically: CONTROL|WAIT supervision and its LAUNCH-only bound
launcher. Failure installs neither; an uncertain response must not be replayed.

Every group fixes its space and parent at creation. Groups record no CPU;
children use their space's existing placement policy. A member may hold and use
own-group or descendant launchers. Ancestor, unbound and unrelated launchers
refuse, including zero-right handles, COPY and IPC. Endpoint admission checks
before enqueueing, so a rejected SEND cannot obstruct its FIFO; REPLY validates
before committing grants. Ungrouped helpers may hold bound launchers, but use
from another space is DENIED. Grouped SPACE_FACTORY_CREATE refuses before effects
in this fixed-space profile; trusted ungrouped space creation remains available.

Ordinary launch does not create a group automatically. It enrolls the child in
the selected launcher's group before installing grants. An optional declared
same-group launcher permits descendants; withholding it keeps a leaf profile.
No launcher or other resource is synthesized by the kernel. The creator can
keep sole CONTROL of a private job subgroup so its exit/fault closes that control
and requests subtree stop. Explicit CONTROL delegation remains possible.

Tunable kernel policy admits root depth one through depth 16 and at most 256
allocated group objects per space, including completed storage retained by
observers/launchers and objects awaiting destruction. Admission returns LIMIT
before publication. These bounds are not public ABI constants or process quotas.

Copies of a bound launcher keep the binding. Resource names do not select or
change membership. Applications receive a launcher only through explicit grants;
closing it or a process observer does not detach execution. The ordinary shell's
existing forwarding works when its initial launcher is the bound one. Supervision
is not implicitly passed to children. Explicit
CONTROL delegation is permitted and prolongs supervision until that grant closes,
even if the creator exits. This is authority deliberately given to its recipient.

## Sealing, termination and completion

`execution_group_seal()` requires CONTROL. It permanently and idempotently closes
launch and subgroup-creation admission throughout the subtree while existing
members may finish naturally.
`execution_group_terminate()` requires CONTROL, seals admission and requests that
all members throughout the subtree stop. Its success acknowledges the request;
it does not wait for cleanup.
The final CONTROL grant closing requests the same termination, including on its
owner's exit or fault. Explicit CONTROL delegation can prolong supervision.

Capability entries and queued IPC transfers count as controlling grants. Delivery
moves authority without a gap. WAIT-only grants, zero-right handles, bound launchers,
prepared children and internal references retain storage only. Discarding the last
queued controlling grant terminates the group. Neither operation can reopen admission.
A shell without CONTROL can exit without sealing its group or stopping descendants;
the [remote server](../userland/remote-terminal.md) terminates remaining descendants
when its root shell exits.

`execution_group_wait()` requires WAIT and uses a header-only request with no reply
payload. `execution_group_wait_result()` uses the same WAIT right and request
with an optional `execution_group_result` reply. Its sticky flags record any
PASSTHROUGH authority installed into the subtree and abnormal completion
(termination requested, member fault/termination/startup failure). Normal nonzero
exit codes do not count as abnormal. These are cleanup observations, not
aggregate program success. Flag recording is atomic-only along immutable
ancestry; it takes no lock or callback under a capability-table guard. A process
and its cleanup retain group storage through final publication. No user mapping
lease is held across the wait; failed reply copies leave completion repeatable.

Completion is immutable and repeatable: admission is sealed, every published
member is reclaimed, every subgroup has completed, every admitted launch/group
preparation is disposed, and deferred cleanup caused by their resource releases
has finished. An open empty group is not complete. `wait_many()` accepts WAIT_COMPLETE for a WAIT-authorized group, alone or
mixed with terminal attachment and TCP interests. Completion reports cleanup, not
aggregate program success. Keep a WAIT-only copy when dropping the final CONTROL
handle and then observing shutdown.

Running userspace stops at a scheduler safe point on its current CPU. A blocked syscall
resumes its kernel continuation to detach registrations and return ownership before
retirement; it cannot return to userspace. Timers and subsystem links are detached
separately. Network and entropy slots are cancelled through their owning worker and
collected before task teardown. Published BSP/HOST loans finish their handoff;
already-completed external effects are not rolled back. No live kernel stack is
forcibly freed. The [ownership matrix](execution-group-termination.md) records
these subsystem boundaries.

A terminated process observer returns PROCESS_TERMINATED with zero exit_status.
Previously committed exits and faults retain their original result. Process WAIT
still observes process/task reclamation; only group completion additionally waits
for attributed deferred object callbacks, transitive releases, HOST and network
wrapper destruction, retained display pixels and internal TCP read references.
Each pending cleanup owns group storage, and transfers attribution before releasing
its own token. Objects legitimately retained by outside capability owners do not
hold completion open. Independent transport maintenance, including TCP TIME_WAIT
after native wrapper retirement, is outside that boundary.

There is no fixed cleanup deadline: an in-flight HOST operation may delay completion
indefinitely. The allocated-group bounds above do not bound member count or
cleanup time. Group storage is reclaimed after
the final member, launcher, observation/supervision grant, endpoint-policy reference,
launch reservation and cleanup token is released. An outside endpoint reference can
retain the immutable receiver policy after receiver exit without retaining CONTROL.
The minimum subgroup foundation is implemented here; the broader
[nested execution direction](../wip/nested-execution-groups.md) remains parked.
Migration and space teardown remain out of scope. Interactive foreground jobs
use owned cleanup subgroups and cooperative first-stage interruption before
subtree cleanup; see [interrupting foreground commands](../userland/shell.md#interrupting-foreground-commands).
A shell script in a `launch = true` space is the first ordinary consumer: it keeps
sole CONTROL of a private subgroup for its foreground commands, so ending or
terminating the script stops their whole subtree ([script mode](../userland/shell.md#script-mode)).

## Launch publication and ownership

The BSP owns allocation and inactive child preparation through the existing launch
request service. A batch's temporary preparation object is separate from its
execution group. It owns prepared children and provisional process observers until
publication or discard.

A launch reservation counts admission before image capture or group creation and survives every
preparation request until staging is published or discarded. Admission is checked
during preparation and again at publication. The per-space tree guard
serializes ancestor sealing with the complete batch's enrollment and scheduler enqueue.
Publication performs no allocation or other fallible work after admission succeeds.
Either all stages become group members and runnable first, or sealing wins and
publication returns ENDPOINT_CLOSED with no child runnable. In the latter case all
provisional observers and prepared children are discarded before return. A batch
publication failure uses LAUNCH_NO_STAGE and clears all returned handles; a failure
during a stage identifies that stage. Existing external file effects are not rolled
back. Short-lived children can finish before their launcher resumes.

Lock order is space tree, process lifetime/capability table, then scheduler queues, with interrupts disabled and no allocation,
user access or context switch under the group lock. The caller retains its bound
launcher during synchronous launch; prepared children each retain group storage.
No child process or task is accessed after scheduler publication transfers ownership.

Each active parent-to-child edge owns child storage until child completion.
Completion removes that edge and propagates upward under the tree guard; a retained
WAIT-only child cannot delay parent completion. The immutable child-to-parent
reference survives until final child storage destruction, making ancestry checks
safe without nesting the tree guard inside table/endpoint locks. Only group
storage retirement (no cleanup-token acquisition or callback) occurs under the
guard; logical grant release and other object retirement occur outside it.

The public layouts are [launcher.h](../../include/abi/launcher.h) and
[execution_group.h](../../include/abi/execution_group.h). The [ownership matrix](execution-group-termination.md) records safe-stop
and cleanup paths.
