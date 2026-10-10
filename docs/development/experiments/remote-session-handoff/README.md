# Remote session handoff investigation

2026-10-10, separate from threads PR #671 (unchanged). Worktree
`pyxis-remote-session-handoff`, branch `remote/session-handoff`, fresh main
`441fcd4aad7420224c4133425cdb1b9df6362c04`. Implementation awaits the material
handoff-lifetime choice below; no fix or post-fix qualification yet.

## Cause: inspected and reproduced

The owner observed native PXE kernel `d71e4dfd` on the ThinkPad: remote
`session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 5` emitted
its two headers, then builtin completion and FINAL with shell exit status 0,
without samples. The input producer stayed alive for 300 seconds; kernel output
reported terminated tasks, no fault. Native raw JSONL remains on horse at
`~/xf/ipc-long.jsonl`; these are owner-reported results, not independently read.

Inspected current userland `3bd6c21fc6733ec8a683f993ddc6678bd92cbc9b`:

- `shell/launch.c`: successful SHELL_SESSION launch closes the successor observer
  and returns COMMAND_EXIT. `shell/main.c` reports builtin success and exits 0.
- `remote-terminal/main.c`: original-root process completion invokes
  `begin_close`, which terminates the entire supervised execution group.
- `ipcbench/main.c`: headers precede receiver creation/work. Sample reports,
  including warmup, are deferred until work/cleanup returns. Forced termination
  prevents that reporting; the daemon's status 0 describes the original shell.

The unchanged policy is documented in [remote consumers](../../../userland/remote-terminal.md#consumers-and-limits)
and [shell handoff](../../../userland/shell.md). It is a remote/session lifetime
failure, not evidence of lost stderr or a normal early benchmark return.

## Unmodified QEMU baseline

All four bundles from successful exact-head main workflow
[#1729](https://git.internal/PyxisOS/pyxis-os/actions/runs/1729) verified before
ordinary `make -j16 image PREBUILT="kernel sdk userspace ports" REMOTE_BEACON=t14`.
Existing LLVM 23.1.3 builder `49e2c1a`; host client built by `make -C tools remote`.
Kernel ELF SHA-256 `e8dc172110714a0f578ab173b7788177f0e5046cc16e174bd519e97e53bbecf8`;
ISO `c6559e09446766a3331d994be76ef007435e9f3151fb5cfb1a9490562a4a6831`.

QEMU 10.2.2 Q35/nested KVM, CPU host, four cores/one socket/one thread, 2 GiB,
UTC RTC, matching raw OVMF CODE/fresh VARS, standard VGA/display none, modern
VirtIO SCSI CD/RNG/net. NAT forwards host UDP 2324 to guest UDP 2324;
GDB uses 12831. No TAP or shared network configuration changes.

```sh
(printf 'session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 5\n'; sleep 300) |
  build/tools/pyxis-remote --machine --no-shell-echo --listen t14 \
    --beacon-address 127.0.0.1 0.0.0.0 2323
```

The loopback beacon destination is the documented QEMU NAT adaptation. The
unmodified LAN-broadcast invocation did not establish a session in this NAT
setup and was stopped; it is not reproduction evidence.

Two established sessions returned READY, tab width, builtin status 0, then
`FINAL {cause:shell_exit, process_reason:exited, exit_status:0, drain:complete}`
without any benchmark results. In QEMU the successor was stopped before even
the headers appeared; how far it runs before closure is scheduling-dependent.

The second session had separate interactive GDB inspection, target function
calls disabled. At `stop_locked` through EXECUTION_GROUP_TERMINATE (operation 2,
rights 3), the group had one live member, stopping=false, complete=false and
one controller. Its process had no terminal result and a runnable user task.
`process_request_stop` targeted that exact process. `process_set_result` then
committed kind 3 (PROCESS_TERMINATED) through `task_syscall_leave`, rather than
a normal program return. The original shell had already completed.

Raw JSONL/debugger/build captures remain in ignored `build/remote-handoff-baseline`.
The sleeping producers, clients, debugger and QEMU were stopped afterward;
ports 2323/2324/12831 are closed. No benchmark, boot runner, fault injection or
test infrastructure was added. These are functional nested-VM results, not
performance or native post-fix evidence.

## Material choice — pending

**Default: explicit remote waiting-supervisor mode.** After successful session
launch, keep the original shell parked until its successor completes; never
resume the original command reader. Preserve ordinary root-exit/background
cleanup and unconditional group termination on disconnect. Propagate normal
successor exit status; fault/termination becomes failure. Chained handoffs must
propagate this mode to successor shells and retain one shell per handoff.
Local caller-exits-without-wait behavior remains unchanged. This needs no kernel
or remote-wire change, but changes the remote caller lifetime and completion
timing and must be accepted before implementation.

Alternative: acknowledged transfer of a WAIT-only successor observer to the
daemon before the old shell exits. This releases old shells and observes the
actual successor, but adds a larger handoff protocol with acknowledgment,
failure/uncertain-reply and chained-handoff contracts.

Do not merely remove group termination or seal on root exit: the former leaves
ordinary background descendants alive, the latter prevents the successor from
launching receivers. A pipe lifetime token alone cannot report the successor's
exit status and would change FINAL's meaning.

After the decision: implement the generic cause in userland, qualify all four
remote IPC commands plus chained handoff, error status and disconnect cleanup,
publish userland PR before the parent gitlink/docs PR, record merge order and
exact-head CI. Native owner rerun and hands-free commands follow qualification.
