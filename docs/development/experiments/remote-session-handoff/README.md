# Remote session handoff investigation

2026-10-10, separate from threads PR #671 (unchanged). Worktree
`pyxis-remote-session-handoff`, branch `remote/session-handoff`, fresh main
`441fcd4aad7420224c4133425cdb1b9df6362c04`; rebased onto current main
`fa390738fc` for delivery. The owner accepted the waiting-supervisor default;
userland fix `4d64f51f7e9ce1585517332fc253601c7ce81a1c` is published in
[#203](https://git.internal/PyxisOS/pyxis-userland/pulls/203). Merge userland first,
then the parent pin/docs PR. Native post-fix results remain an owner check.

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

The corrected policy is documented in [remote consumers](../../../userland/remote-terminal.md#consumers-and-limits)
and [shell handoff](../../../userland/shell.md#session-handoff). It is a remote/session lifetime
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
The baseline sleeping producers, clients, debugger and QEMU were stopped afterward.
No benchmark, boot runner, fault injection or
test infrastructure was added. These are functional nested-VM results, not
performance or native post-fix evidence.

## Accepted behavior and implementation

**Accepted by the owner, 2026-10-10: remote waiting-supervisor mode.** After successful session
launch, keep the original shell parked until its successor completes; never
resume the original command reader. Preserve ordinary root-exit/background
cleanup and unconditional group termination on disconnect. Propagate normal
successor exit status; fault/termination becomes failure. Chained handoffs must
propagate this mode to successor shells and retain one shell per handoff.
Local caller-exits-without-wait behavior remains unchanged. No kernel, public ABI
or remote-wire change is needed.

The remote server sets private runtime policy `PYXIS_SESSION_WAIT=1`; shells
capture it at startup and pass it only through session launches. It grants no
authority. The borrowed environment-variable array is filtered before launch,
with allocation preflight; the owned environment snapshot is untouched. Waiting
callers retain the returned process observer and never resume their input reader.
Both script endings propagate successor status. Ordinary launches remove the
marker. The daemon's disconnect path and local terminal-event authority remain
unchanged. See [retained resources](../../../technical-debt.md#parked-remote-handoff-shells).

Do not merely remove group termination or seal on root exit: the former leaves
ordinary background descendants alive, the latter prevents the successor from
launching receivers. A pipe lifetime token alone cannot report the successor's
exit status and would change FINAL's meaning.

## Post-fix qualification

Verified current-main bundles from workflow
[#1731](https://git.internal/PyxisOS/pyxis-os/actions/runs/1731) supplied kernel,
SDK and ports; userland was rebuilt from clean fix `4d64f51` atop userland main
`0b1ede1ebb27b7f4fa85a92ff00e1d6433f5347b`. Ordinary
`make -j16 image PREBUILT="kernel sdk ports" REMOTE_BEACON=t14` passed without
warnings with the same existing compiler. ELF SHA-256
`a2b4a4dc63ef39884ca6b6ce84bb6e58a6b42c1ff662278af6bf3afdcc17b00e`;
ISO `08e79519c6ca9330250b4833cef12da6bad6f3f3133aa8277812062de6adfd79`.
Baseline and delivery main have no kernel/public-header source difference, but
the bundle/SDK refresh produced different ELF hashes; this is functional
qualification, not a performance comparison. Effective kernel configuration
SHA-256 is `ac12acc93c3fcbbdff1ace10d95b8f3cdf883e1c09d21ce413ba09df8324a98b`.

With the same QEMU configuration and reverse client:

- The original 300-second-stdin command delivered headers, warmup, five verified
  passes, summary and FINAL exited 0/drain complete. The sleeping producer was
  stopped only after FINAL; its host interrupt is not a guest failure.
- All four call/send × 64/4096 commands also completed with immediate stdin EOF,
  256 messages and five rounds: warmup, five verified passes, summary, FINAL 0
  and complete drain; each client returned 0. No pacing, guest exit or reattach.
- Lua exit -7 propagated as FINAL -7; two chained successor shells propagated
  final Lua status 23. Shebang scripts propagated 29 with final newline and 31
  without one. Each negative-status client returned failure as expected.
- An ordinary child saw no policy marker; a session successor saw value 1.
  Root exit still terminated an ordinary infinite-loop background child.
- On client disconnect, interactive GDB observed three group members: the root
  and successor shell parked in native waits, with the Lua successor active.
  Existing termination reclaimed all three; group state reached zero members,
  launches and cleanup pending, with no first member. A fresh reverse session
  was then admitted. No injected target calls, writes or faults were used.

Fault-to-failure mapping was source-inspected, not fault-injected. An independent
source review found no correctness issue in environment ownership, wait-observer
cleanup, status propagation, input/event authority or disconnect behavior. Raw
JSONL, build and GDB captures remain in ignored `build/remote-handoff-after`.
No new test infrastructure, klog lines or benchmark changes. The userland
repository has no existing CI tasks; parent exact-head integration CI is checked
at delivery rather than treating that absence as a pass.

The [LAN command loop](../../../userland/remote-terminal.md#consumers-and-limits)
is the owner rerun recipe. For QEMU NAT only, add
`--beacon-address 127.0.0.1`. Stage this same fixed userland in both #671 A/B
images, retaining kernels A `d71e4dfd` and B `ea39ff9f`; label captures A1/B1/A2/B2.
This change does not modify #671 or establish native post-fix performance.
