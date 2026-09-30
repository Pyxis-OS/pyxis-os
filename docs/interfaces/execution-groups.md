# Execution groups and launch containment

Execution groups bind descendants to one supervision lifetime independently of
terminal sessions, process observers and spaces. Creation, membership and sealed
launch admission are implemented. Stopping members and group-completion waits
remain [task 5](../wip/execution-group-termination.md).

## Creation and launch authority

Trusted init receives CREATE_GROUP in addition to LAUNCH on its ordinary launcher.
The default session program delegates only LAUNCH to the local shell. A caller
with CREATE_GROUP can use `launcher_create_group()` to obtain two handles
atomically: a CONTROL-authorized supervision handle and a LAUNCH-only launcher
bound to the new group. Failure installs neither handle. The creator stays outside
the group, and an empty group remains open while supervision exists.

The group fixes the creator's space and assigned CPU. Its bound launcher implements
the existing single/batch launch protocol, with no extra group argument or implicit
resource grants. It can be delegated to an ungrouped helper in the same placement;
children launched through it still join the bound group. Use from another space or
CPU is DENIED. A bound launcher cannot create groups.

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

## Sealing and lifetime

`execution_group_seal()` requires CONTROL. It permanently and idempotently closes
launch admission. Existing members continue executing, exiting and faulting
normally. There is no terminate operation or group wait/readiness interface in
this task, and sealing does not imply cleanup completion.

The last CONTROL-authorized grant also seals the group. Capability entries and
queued IPC transfers count as controlling grants; zero-right handles, bound
launchers, prepared children and internal references retain storage only. Delivery
moves authority without a gap. Discarding the last queued controlling grant seals
admission. Acquiring another reference cannot reopen a sealed group.

A process owns group storage from preparation through reclamation. Only published
children contribute to the member count. Preparation failure releases its reference
without becoming a member. Normal retirement removes membership after private VM,
capabilities, kernel stack and task metadata are reclaimed. Existing process WAIT
handles retain their non-owning, immutable completion semantics. Natural shell exit
does not seal the group or stop descendants; the later server defines that policy.

No fixed group/member quota is introduced. Storage is reclaimed after the final
member, launcher, supervision/observation grant, endpoint-policy reference and
temporary reference is released. Endpoints retain the immutable receiver policy
even if an outside caller/export/receipt outlives the receiver process; this keeps
only group storage alive, never controlling supervision.
A zero member count is internal accounting, not proof that all asynchronously
retired resource objects have completed destruction.

## Launch publication and ownership

The BSP owns allocation and inactive child preparation through the existing launch
request service. A batch's temporary preparation object is separate from its
execution group. It owns prepared children and provisional process observers until
publication or discard.

Admission is checked during preparation and again at publication. A group lock
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
[execution_group.h](../../include/abi/execution_group.h). The termination audit
records the remaining [safe-stop and cleanup obligations](../wip/execution-group-termination.md).
