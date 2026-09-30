# Execution-group termination ownership matrix

Task 4 audit for [remote terminals](remote-terminal.md). This is a task-5 design
input, not implemented cancellation. Group sealing currently prevents new launches;
existing members continue. No operation claims successful termination or group
cleanup completion.

## Required stopping boundary

A stop request must prevent further userspace execution when the assigned scheduler
reaches a safe stopping point. It must not destroy a running kernel stack, a task
wait record still referenced by a subsystem, or process state lent to a worker.
Published operations must return ownership; their completed results also need disposal
when the original caller will never resume. Shared kernel workers remain alive.

The current scheduler has no cancellation disposition describing the active subsystem
and outstanding loans/results. `free_task()` requires the task's wait, resource link,
timer membership and BSP reservation to be clear. Waking a task and setting an exit
flag does not establish those invariants. The implementation must choose explicit
subsystem cleanup paths before enabling termination.

## Ownership matrix

Each row describes current retention and the cleanup task 5 must supply. Function
names identify the relevant ownership transitions; line numbers are intentionally
omitted because those implementations will change.

| State / subsystem | Retained ownership and required stopping action | Final handoff owner / source |
| --- | --- | --- |
| Running/runnable user task | Private VM, kernel stack, saved CPU state, request/profile storage. Reschedule on assigned CPU; prevent user return. Leave private root/stack and clear entry state before retirement. Remove runnable membership under scheduler lock. User syscall execution is currently non-preemptible. | Local scheduler then BSP reaper: [task.c](../../kernel/task.c), `task_preempt`, `complete_task`, `reap_completed`. |
| Clock sleep / timed resource wait | Timer list retains embedded wait. Remove timer membership and separately detach the resource registration; an expired timer does not erase the resource pointer. | Scheduler queue lock: [task.c](../../kernel/task.c), `sleep_wait`, `task_wait_wake`, `expire_timed_waits`; [clock.c](../../kernel/object/clock.c). |
| Process WAIT | Completion object retains task-owned waiter link. Detach only the dying observer; preserve the observed process's independent execution. | Completion lock/publisher: [process.c](../../kernel/object/process.c), `process_control_call`, `process_control_complete`. |
| Local console input | FIFO reader or active read ownership plus input waiter. Detach queued/active wait and return acquired reader ownership, including concurrent handoff. Console survives process exit. | Input lock and `begin_read`/`end_read`: [console.c](../../kernel/object/console.c). |
| Terminal input/output | FIFO reader ownership, input waiter or writer links. Detach under session lock before releasing capabilities. A closure wake is not necessarily an ownership grant. Preserve external attachment/application owners. | Session lock, `begin_read`, `end_read`, `enqueue_output`, authority release: [terminal.c](../../kernel/object/terminal.c). |
| Pipe read/write | Pair holds task-owned reader/writer link. Detach under pair lock before closing handles; subsequent final endpoint closure wakes peers normally. | Pipe lock and endpoint destruction: [pipe.c](../../kernel/object/pipe.c). |
| RAM/initrd file operation | FIFO link or exclusive busy ownership; wake can transfer ownership directly. Remove waiter or return already-acquired ownership with `file_end_operation`. Do not clear busy/free backing during a published replacement loan. | File owner then BSP replacement executor: [file.c](../../kernel/object/file.c). |
| Endpoint CALL | Delivery retains caller state/waiter, request/reply grants, receipt and export. Cancel queued/delivered states distinctly, end caller ownership, release discarded results; provider-owned receipt may legitimately survive. | Endpoint lock/state transitions: [endpoint.c](../../kernel/object/endpoint.c), `cancel_delivery`, `free_delivery`, `release_receipt`. |
| Endpoint RECEIVE / provider exit | Receiver waiter and process-owned endpoint list; receipts/grants in capability table. Detach receiver wait before existing endpoint-close and receipt-abandonment paths. Do not recall grants delivered outside the group. | BSP `endpoint_process_exit`, `close_endpoint`, `receive_endpoint`: [endpoint.c](../../kernel/object/endpoint.c). |
| TCP control/connect/listen/accept | Shared slot, borrowed stream/listener or capability table; DONE awaits caller collection. Return borrowed ownership, dispose abandoned created handles, release accept direction and reclaim slot without user continuation. | Network worker: [control.c](../../kernel/net/lwip/control.c), `exchange_control`, `complete_control`, `finish_created`. |
| TCP read/write | Shared operation slot and direction exclusion; read additionally retains stream and receive-credit accounting through CONSUMED/RECLAIMING. Remove waiter, dispose successful abandoned read results/credit and release slot only after worker use ends. | Network worker plus current caller collection: [receive.c](../../kernel/net/lwip/receive.c), [send.c](../../kernel/net/lwip/send.c). |
| UDP control/send/receive | OPEN lends table; I/O borrows endpoint and retains token, waiter and DONE slot. Cancel queued ARP packet by token; return table/endpoint ownership and reclaim direction/slot. Submitted packet/DMA storage follows transport lifetime. | Network worker: [udp.c](../../kernel/net/udp.c), [udp_io.c](../../kernel/net/udp_io.c), `net_udp_service`, `net_udp_stop_io`. |
| Echo / network configuration | Shared copied/scalar request slots persist through DONE. Echo can own queued ARP work; RUNNING configuration mutates shared state. Cancel token-owned echo work; let configuration finish its handoff and reclaim slots. | Network worker: [echo.c](../../kernel/net/echo.c), [config.c](../../kernel/net/config.c). |
| wait_many | Task request retains object interests and is on terminal-readiness or network-worker incoming/active list. Remove from actual owning worker, release every interest and reservation; omit user output copy after stop. | Owning worker and `readiness_complete`: [readiness.c](../../kernel/user/readiness.c), [TCP readiness](../../kernel/net/lwip/readiness.c). |
| BSP request / HOST loan | Reserved request survives PREPARED through COMPLETE/result consumption. Loans include VM, table, receiver list, file ownership and HOST nodes; results can own captured bytes or objects. Published work must return ownership before teardown. Dispose owned results and release reservation. HOST delay has no immediate cleanup bound; completed host mutations cannot be undone. | BSP executor or forwarded worker: [request.c](../../kernel/service/request.c), [request states](../../include/kernel/service/request.h), [hostfs.c](../../kernel/fs/hostfs.c). |
| Launch capture / prepared batch | Capture/image ownership, borrowed parent table, inactive children/task stacks and provisional observers can survive between requests. Track/dispose them when stopping caller, including between stage preparations. Published children follow ordinary member stopping. Sealing already serializes final publication. | BSP launch executor: [launcher.c](../../kernel/object/launcher.c), [spawn.c](../../kernel/user/spawn.c). |
| Keyboard / display | Raw keyboard owner can retain reader wait; release currently requires no reader. DISPLAY lends process/VM to BSP; presenter can independently retain frame backing. Detach keyboard wait, finish display loan, then release ownership/mappings without freeing retained pixels. | Keyboard lock and BSP exit cleanup: [keyboard.c](../../kernel/object/keyboard.c), [display.c](../../kernel/object/display.c). |
| Random / entropy | Shared RNG slot/waiter can be entropy worker's current request; DONE reclaimed only by caller. Detach request attribution and reclaim slot independently of pending DMA storage. Deadline expiry is an existing detachment precedent. | Entropy worker: [rng.c](../../kernel/virtio/rng.c), `complete_call`, `expire_calls`, `finish_chunk`. |

## Completion decision before task 5

Existing process completion follows process/private-VM and task-stack reclamation,
but may precede deferred object destruction. TCP/UDP destruction can transfer to the
network worker, HOST wrappers to `hostfs_retire`, and presentation can retain frame
backing. Therefore member count zero alone is insufficient to define the future
group-completion contract. Task 5 must identify which group-owned deferred cleanups
completion awaits and how they report finishing, while excluding objects legitimately
retained by outside capability owners. No bounded cleanup deadline is promised.

Resolve that completion boundary and the cancellation/result-disposal mechanism
before implementing stop operations. Ordinary process observers keep their current
semantics. Foreground Ctrl+C cancellation, nested groups, migration and space teardown
are outside this milestone.
