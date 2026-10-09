# Execution-group termination ownership matrix

Implemented ownership paths for [remote terminals](../userland/remote-terminal.md).
The public contract is in [execution groups](execution-groups.md).

## Stopping boundary

A stop request prevents further userspace execution at the assigned scheduler's
safe point. Group members and process-control objects borrow process links; the
process owns its group member record, terminal result and sole task stop target.
There is still exactly one user task per process. Both group stop and a process
observer's [TERMINATE](processes.md) operation target that process through the
same path, for grouped and ungrouped processes alike. The lock order is control
or group, then process lifetime, then scheduler queues. Running/runnable user
state retires locally; an interrupted syscall
continues solely to detach waits, return loans and dispose owned results. Its stack
stays alive through that unwind. Stop wakeups leave subsystem registration removal
to the continuation, under the resource lock. Shared workers remain alive.

The sole task commits its exit, fault or termination result once, then
release-publishes `result_set` without taking the process lifetime lock, including
on user fault entry. Stop targeting acquire-reads this marker and preserves an
already committed result. Borrowed control/group links remain protected until
final process reclamation; the task stop target is detached before task freeing.

Published BSP/HOST requests retain their uninterruptible handoff. Their request
header borrows cleanup attribution from the caller, whose membership cannot end
before result collection. Worker context attributes provisional-object failure
cleanup before waking that caller. Deferred object/worker cleanup owns a pending
group token; callbacks transfer attribution to transitive releases before ending it.

## Ownership matrix

Each row describes retained ownership and the implemented stopping action. Function
names identify the relevant ownership transitions; line numbers are intentionally
omitted because those implementations will change.

| State / subsystem | Retained ownership and required stopping action | Final handoff owner / source |
| --- | --- | --- |
| Running/runnable user task | Task owns kernel stack, saved CPU state and request/profile storage; it borrows the process that owns private VM and the terminal result. Reschedule on assigned CPU; prevent user return. Leave private root/stack and clear entry state before retirement. Remove runnable membership under scheduler lock. User syscall execution is currently non-preemptible. | Local scheduler then BSP reaper: [task.c](../../kernel/task.c), `task_preempt`, `complete_task`, `reap_completed`; [process.c](../../kernel/process.c), `process_task_detach`, `process_task_reclaimed`. |
| Clock sleep / timed resource wait | Timer list retains embedded wait. Remove timer membership and separately detach the resource registration; an expired timer does not erase the resource pointer. | Scheduler queue lock: [task.c](../../kernel/task.c), `sleep_wait`, `task_wait_wake`, `expire_timed_waits`; [clock.c](../../kernel/object/clock.c). |
| Process WAIT | Completion object retains task-owned waiter link. Detach only the dying observer; preserve the observed process's independent execution. | Completion lock/publisher: [process.c](../../kernel/object/process.c), `process_control_call`, `process_control_complete`. |
| Local console input | FIFO reader or active read ownership plus input waiter. Detach queued/active wait and return acquired reader ownership, including concurrent handoff. Console survives process exit. | Input lock and `begin_read`/`end_read`: [console.c](../../kernel/object/console.c). |
| Terminal input/output | FIFO reader ownership, input waiter or writer links. Detach under session lock before releasing capabilities. A closure wake is not necessarily an ownership grant. Preserve external attachment/application owners. | Session lock, `begin_read`, `end_read`, `enqueue_output`, authority release: [terminal.c](../../kernel/object/terminal.c). |
| Pipe read/write | Pair holds task-owned reader/writer link. Detach under pair lock before closing handles; subsequent final endpoint closure wakes peers normally. | Pipe lock and endpoint destruction: [pipe.c](../../kernel/object/pipe.c). |
| RAM/initrd file operation | FIFO link or exclusive busy ownership; wake can transfer ownership directly. Remove waiter or return already-acquired ownership with `file_end_operation`. Do not clear busy/free backing during a published replacement loan. | File owner then BSP replacement executor: [file.c](../../kernel/object/file.c). |
| Endpoint CALL | Delivery retains caller state/waiter, request/reply grants, receipt and export. Cancel queued/delivered states distinctly, end caller ownership, release discarded results; provider-owned receipt may legitimately survive. | Endpoint lock/state transitions: [endpoint.c](../../kernel/object/endpoint.c), `cancel_delivery`, `free_delivery`, `release_receipt`. |
| Endpoint RECEIVE / provider exit | Receiver waiter and process-owned endpoint list; receipts/grants in capability table. Detach receiver wait before existing endpoint-close and receipt-abandonment paths. Do not recall grants delivered outside the group. | BSP `endpoint_process_exit`, `close_endpoint`, `receive_endpoint`: [endpoint.c](../../kernel/object/endpoint.c). |
| TCP control/connect/listen/accept | Shared slot, borrowed stream/listener or capability table; DONE awaits caller collection. Return borrowed ownership, dispose abandoned created handles, release accept direction and reclaim slot without returning to userspace. | Network worker: [control.c](../../kernel/net/lwip/control.c), `exchange_control`, `complete_control`, `finish_created`. |
| TCP read/write | Shared operation slot and direction exclusion; read additionally retains stream and receive-credit accounting through CONSUMED/RECLAIMING. Remove waiter, dispose successful abandoned read results/credit and release slot only after worker use ends. | Network worker plus current caller collection: [receive.c](../../kernel/net/lwip/receive.c), [send.c](../../kernel/net/lwip/send.c). |
| UDP control/send/receive | OPEN lends table; I/O borrows endpoint and retains token, waiter and DONE slot. Cancel queued ARP packet by token; return table/endpoint ownership and reclaim direction/slot. Submitted packet/DMA storage follows transport lifetime. | Network worker: [udp.c](../../kernel/net/udp.c), [udp_io.c](../../kernel/net/udp_io.c), `net_udp_service`, `net_udp_stop_io`. |
| Echo / network configuration | Shared copied/scalar request slots persist through DONE. Echo can own queued ARP work; RUNNING configuration mutates shared state. Cancel token-owned echo work; let configuration finish its handoff and reclaim slots. | Network worker: [echo.c](../../kernel/net/echo.c), [config.c](../../kernel/net/config.c). |
| wait_many | Task request retains object interests and is on terminal-readiness or network-worker incoming/active list. Remove from actual owning worker, release every interest and reservation; omit user output copy after stop. | Owning worker and `readiness_complete`: [readiness.c](../../kernel/user/readiness.c), [TCP readiness](../../kernel/net/lwip/readiness.c). |
| BSP request / HOST loan | Reserved request survives PREPARED through COMPLETE/result consumption. Loans include VM, table, receiver list, file ownership and HOST nodes; results can own captured bytes or objects. Published work must return ownership before teardown. Dispose owned results and release reservation. HOST delay has no immediate cleanup bound; completed host mutations cannot be undone. | BSP executor or forwarded worker: [request.c](../../kernel/service/request.c), [request states](../../include/kernel/service/request.h), [hostfs.c](../../kernel/fs/hostfs.c). |
| Launch capture / prepared batch | Capture/image ownership, borrowed parent table, inactive children/task stacks and provisional observers can survive between requests. Track/dispose them when stopping caller, including between stage preparations. Published children follow ordinary member stopping. Sealing already serializes final publication. | BSP launch executor: [launcher.c](../../kernel/object/launcher.c), [spawn.c](../../kernel/user/spawn.c). |
| Keyboard / pointer / display | Raw keyboard and pointer owners can retain a reader wait; release currently requires no reader. DISPLAY lends process/VM to BSP; presenter can independently retain frame backing. Detach keyboard and pointer waits, finish display loan, then release ownership/mappings without freeing retained pixels. | Keyboard and pointer locks and BSP exit cleanup: [keyboard.c](../../kernel/object/keyboard.c), [pointer.c](../../kernel/object/pointer.c), [display.c](../../kernel/object/display.c). |
| Random / entropy | Shared RNG slot/waiter can be entropy worker's current request; DONE reclaimed only by caller. Detach request attribution and reclaim slot independently of pending DMA storage. Deadline expiry is an existing detachment precedent. | Entropy worker: [random.c](../../kernel/random.c), `complete_call`, `expire_calls`, `finish_chunk`. |

## Completion accounting

Once the local scheduler has left the task's private root and stack and all
loans have returned, the BSP detaches the process's sole task stop target under
its lifetime lock. It then frees the task kernel stack, request/profile storage
and metadata. `process_task_reclaimed()` ends the process's task-storage lifetime,
releases the lifetime lock, and detaches the borrowed control/group process links
under their respective locks. It destroys the process/private VM and capabilities
under group cleanup attribution, publishes and releases the process-control
completion, then completes the group member. The member count stays positive
throughout task and process reclamation. Admitted launch reservations also survive
unpublished child and capture disposal; unpublished destruction publishes no
process completion and releases only group storage.

Final object release carries the active group context into the BSP retirement queue.
Callbacks retain attribution through child releases. TCP/UDP wrappers and HOST nodes
transfer a pending token to their actual destruction worker; presentation retains one
until its last pixel-backing reference is released. TCP internal read references and
provisional creation carry operation tokens as well. The successful read token ends
after receive credit, stream reference and operation slot are returned.

Completion is published only once sealing and zero members, launches and pending
cleanup coincide. Group storage retirement itself is excluded to avoid a self-cycle.
Capabilities legitimately held outside the group, and independent TCP TIME_WAIT
maintenance after native-wrapper release, are excluded. A stalled published HOST loan
can delay completion indefinitely; no bounded cleanup deadline is promised. Ordinary
process observers retain their earlier reclamation boundary.

Nested groups, migration and space teardown are outside this implementation.
Foreground Ctrl+C uses process TERMINATE on each stage, through the same stop
path. Runtime evidence and unexercised paths are recorded in the
[remote-terminal validation notes](../userland/remote-terminal.md#validation-evidence); this matrix records ownership
review, not a claim that every race has been measured.
