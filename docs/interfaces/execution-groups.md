# Execution groups and launch containment

Execution groups bind descendants to one supervision lifetime independently of
terminal sessions, process observers and spaces. Creation, membership, sealed
launch admission, safe termination and cleanup completion are implemented.

## Creation and launch authority

Trusted init receives CREATE_GROUP in addition to LAUNCH on its ordinary launcher.
The default session program delegates only LAUNCH to the local shell. A caller
with CREATE_GROUP can use `launcher_create_group()` to obtain two handles
atomically: a CONTROL|WAIT-authorized supervision handle and a LAUNCH-only launcher
bound to the new group. Failure installs neither handle. The creator stays outside
the group, and an empty group remains open while supervision exists.

The group fixes the creator's space. Its bound launcher implements the existing
single/batch launch protocol, with no extra group argument or implicit resource
grants. It can be delegated to an ungrouped helper in the same space, on any CPU;
children launched through it still join the bound group. Use from another space
is DENIED. A bound launcher cannot create groups. Children run on CPUs their
space allows; the group itself records no CPU.

Membership is permanent kernel state. Every child of a member belongs to the same
group, including background commands, pipeline stages and launched services.
Before installing a child's first grant, preparation attaches its group policy.
Capability insertion rejects unbound or foreign-group launchers in a member's table,
including zero-right handles and grants received through IPC. Endpoint admission also rejects incompatible request grants before enqueueing, so
a rejected SEND cannot block the receiver FIFO. Reply installation applies the same
table policy and releases rejected reply grants. Ordinary launchers remain caller-scoped for ungrouped processes.

Copies of a bound launcher keep the binding. Resource names do not select or
change membership. Applications receive a launcher only through explicit grants;
closing it or a process observer does not detach execution. The ordinary shell's
existing forwarding works when its initial launcher is the bound one. Neither
supervision nor group-creation authority is implicitly passed to children. Explicit
CONTROL delegation is permitted and prolongs supervision until that grant closes,
even if the creator exits. This is authority deliberately given to its recipient.

## Sealing, termination and completion

`execution_group_seal()` requires CONTROL. It permanently and idempotently closes
launch admission while existing members may finish naturally.
`execution_group_terminate()` requires CONTROL, seals admission and requests that
all members stop. Its success acknowledges the request; it does not wait for cleanup.
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
payload. Completion is immutable and repeatable: admission is sealed, every published
member is reclaimed, every admitted launch preparation is disposed, and deferred
cleanup caused by their resource releases has finished. An open empty group is not
complete. `wait_many()` accepts WAIT_COMPLETE for a WAIT-authorized group, alone or
mixed with terminal attachment and TCP interests. Completion reports cleanup, not
aggregate program success. Keep a WAIT-only copy when dropping the final CONTROL
handle and then observing shutdown.

Running userspace stops at an assigned-CPU scheduler safe point. A blocked syscall
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
indefinitely. No group/member quota is introduced. Group storage is reclaimed after
the final member, launcher, observation/supervision grant, endpoint-policy reference,
launch reservation and cleanup token is released. An outside endpoint reference can
retain the immutable receiver policy after receiver exit without retaining CONTROL.
Nested groups, migration and space teardown remain out of scope. Foreground
Ctrl+C terminates the job's processes individually and does not involve the
group; see [interrupting foreground commands](../userland/shell.md#interrupting-foreground-commands).

## Launch publication and ownership

The BSP owns allocation and inactive child preparation through the existing launch
request service. A batch's temporary preparation object is separate from its
execution group. It owns prepared children and provisional process observers until
publication or discard.

A launch reservation counts admission before image capture and survives every
preparation request until staging is published or discarded. Admission is checked
during preparation and again at publication. A group lock
serializes sealing with the complete batch's enrollment and scheduler enqueue.
Publication performs no allocation or other fallible work after admission succeeds.
Either all stages become group members and runnable first, or sealing wins and
publication returns ENDPOINT_CLOSED with no child runnable. In the latter case all
provisional observers and prepared children are discarded before return. A batch
publication failure uses LAUNCH_NO_STAGE and clears all returned handles; a failure
during a stage identifies that stage. Existing external file effects are not rolled
back. Short-lived children can finish before their launcher resumes.

Lock order is group then scheduler queues, with interrupts disabled and no allocation,
user access or context switch under the group lock. The caller retains its bound
launcher during synchronous launch; prepared children each retain group storage.
No child process or task is accessed after scheduler publication transfers ownership.

The public layouts are [launcher.h](../../include/abi/launcher.h) and
[execution_group.h](../../include/abi/execution_group.h). The [ownership matrix](execution-group-termination.md) records safe-stop
and cleanup paths.
