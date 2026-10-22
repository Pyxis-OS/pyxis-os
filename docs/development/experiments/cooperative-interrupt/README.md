# Cooperative process interruption

2026-10-22, nested QEMU/KVM q35, four vCPUs, `-cpu max,+invtsc`, 2 GiB,
OVMF, VirtIO RNG/network and read-only VirtIO-SCSI boot CD. The guest selected
stable KVM clock with invariant TSC exposed. No mounted writable disk or native
run. Existing Remote/QMP/GDB tools and temporary native callers were used;
no test framework, fault injection or default logging was added.

Kernel executable source: `b9260f29f0c2`, on main `71369bf173e0` plus
terminal commit `1b8a264450c0`. Userland helpers/shell: `ea0718aefb29`, on
`68b3ac8ddf79`. The private qualification archive reused the prior readiness
archive, replacing all three shell aliases (including `init-shell.pxe`) and
adding a temporary native caller. It is not a newly assembled normal release
image. Kernel/SDK, native caller and focused shell builds passed.
Exact commands, private sources, images' hashes and captures remain under
`/shared/present/go-cooperative-interrupt/`.

After rebasing onto main `0bf27b3b`, kernel `56a023c5` and userland
`65772bd3` built the complete normal image with the existing builder. A private
caller was then added to that matching archive. The receiver/authority,
coalescing, ready-set clearing, logical-close, forced-process/group and Remote
checks above passed again, including two Ctrl+C bytes in one packet. The normal
and private image hashes, build log and captures are in the `rebased/` directory
of the evidence path. QEMU was stopped after the check; no native run occurred.

| Check | Observation |
| --- | --- |
| Receiver and authority | Creation without a creation grant; second active receiver BUSY; foreign-process TAKE/wait DENIED; WAIT-only process observer could not send an interrupt. |
| Pending event | Two requests before TAKE coalesced. wait_many and a readiness set reported READABLE; TAKE consumed it, a second TAKE returned WOULD_BLOCK, and both observations cleared. |
| Logical close | Closing the last receiver allowed a new receiver even with an old readiness registration retaining storage. Parent delivery returned NOT_FOUND; the old registration returned one terminal closure and no repeat. |
| Forced cleanup | TERMINATE and group stop completed with PROCESS_TERMINATED despite an attached receiver. Graceful children returned their actual exit statuses 23/24. Group cleanup joined. |
| Local TTY and mux | First Ctrl+C allowed a native handler to exit with status 23. An unresponsive opted-in receiver survived the first press and was terminated by the second. A detached receiver reverted to forceful first-press behavior locally. |
| Remote | Same graceful/forced results; two Ctrl+C bytes sent together still forced on the second. A program without opt-in terminated on the first. Machine records preserved exited/terminated kinds. |
| Regression | Existing native readiness basic/concurrency/ownership and Go files/time, streams, deadlines and timers passed. Separate debugger inspection found closing-account, work, source and timer queues empty. |

The first private UI run accidentally retained an old `init-shell.pxe` alias;
its forced-stop result is excluded. The corrected image hash and the subsequent
successful checks are recorded in `current/provenance.json` and captures.

The measured layouts are 88 bytes for process control (one added weak pointer),
152 bytes per receiver, 264 per readiness set and 512 per registration; the
existing task request area remains 6,544 bytes. These exclude capability-table,
index and allocator overhead and are not RAM reservations. One receiver can be
attached per process; calls/readiness keep storage without prolonging opt-in.

The existing hello probe also completed eleven warm samples before and after,
without GDB during those samples; every launch exited successfully, first output
below 8 ms. These snapshots used different shell/runtime-library builds and
were not interleaved, so they do not isolate a performance change. No new Go
signal adapter or native startup-budget result is claimed.

Remaining: Go subscriptions, Stop/Reset/Ignore lifetime and netpoll delivery;
native hardware qualification; broader supervisor/descendant delivery. Owned
shell scopes and session/group stop stay forceful. Passthrough and raw-keyboard
semantics are unchanged; no automatic grace timeout is introduced.
