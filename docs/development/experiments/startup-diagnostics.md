# Runtime startup diagnostics (2026-10-22)

Kernel base `96532feb`; userland `0cdffd1`, ports `8418c14`. The final PR carries
only the native completion/reporting changes and their documentation. Builds use
the existing `pyxis-llvm23.1.3-3d69e83` container; no compiler rebuild. Ordinary
`make -j16 image DISTRIBUTION=full` passes, including Lua, libuv, luv and Neovim.
No performance claim or native run.

## Reproduction

Nested QEMU/KVM, q35, host CPU, 4 GiB, OVMF, VirtIO network/RNG and a private
virtio-fs fixture export. Live RAM roots; no target disks or native machines.
A small C program prints `startup reached main` and returns zero. Only its
handle-query object is rebuilt with `SYSCALL_HANDLE_INFO=5` instead of 9, then
linked explicitly before the SDK archives. This deliberately disagrees with the
kernel in exactly the historical stale runtime's query path; no LLVM rebuild,
compatibility syscall or production test hook is needed.

Before the fix, the mismatched child exits 1 without reaching main or writing
stdio. The current shell already reports `Exited with status 1`; it cannot infer
which startup operation failed. The matching control reaches main and exits 0.
This reproduces the loss of the cause, rather than claiming the current shell
suppresses ordinary nonzero exits.

With the rebuilt runtime and parent, the same retired-number query yields:

```text
shell: host://after-stale.pxe: did not start: standard input: unknown system call
```

The first three optional runtime queries remain absent after refusal; standard
input is the fatal fourth query. The named unknown-call status survives into
`PROCESS_START_FAILED`, step 7/status 0xffff (packed reason 524287). This is an
explicit runtime report, not a kernel inference from the application exit code.

## Functional and debugger checks

Both one- and four-CPU guests pass matching startup, the deliberate query
mismatch, and a valid bundle omitting MEMORY. The latter reports
`did not start: memory resource: resource not found` before TLS/FS initialization.
Four CPUs additionally pass:

- Child stderr redirected to a file: parent still names the cause, and `wc -c`
  finds zero bytes in the child's stderr file. No alternate child stream or
  kernel-log fallback is used.
- A failing first pipeline stage is identified while the normal last-stage exit
  policy is preserved. Invalid executable input is a distinct launch refusal.
- An application returning 1 remains EXITED/1; a trap remains FAULTED.
- `pyxis.run` names the startup step/error. Neovim's direct luv callback gets
  code 524287, signal 0, reason `start_failed`; its native job callback gets
  failure 1 rather than the negative still-running sentinel.

Read-only GDB inspection with hardware breakpoints observes the terminal syscall
entering `user_start_failed(524287)`, then BSP process-control completion with
kind 4/reason 524287 and a null process pointer after execution retirement.
No injected debugger calls or user-memory mutations.

A separate never-shipped Remote fixture changes only its child-image selection
from `init-shell.pxe` to the mismatched `fail-shell.pxe`; boot's script
interpreter stays matching. Two one-CPU connections each receive exactly one
`remote: shell did not start: standard input: unknown system call` line and a
final `start_failed`/524287 with complete drain. The production archive and
service selection are unchanged. Early setup attempts replacing the shared
script interpreter are not valid service-start qualification.

Success captures include complete Remote drain. Expected-error command captures
stop at the command-completion error; those do not claim final drain. The
existing capture helper is reused for its command/output records; its package
phase/timing fields are irrelevant to this check.

## Limits and records

Already linked stale programs retain their silent `_Exit` path. Package and
launch ABI checks reject known mismatches; this runtime change helps programs
rebuilt with it. No promise is made for arbitrary incompatible layouts or an
older kernel that does not implement the new completion operation. Constructor
or application `_Exit`/`abort` remains an ordinary exit, not a guessed startup
failure. First-result election, sibling stop, cleanup and repeated WAIT remain
the existing process lifetime contract.

Raw builds, fixture sources/commands, GDB inspection and captures stay in
`/shared/present/startup-diagnostics/`, especially `final-one/`, `final-four/`
`root-parent/`, `root-parent-repeat/` and `retirement-gdb.txt`. Initial malformed fixture/VM setup attempts are not
passing qualification records. The old native Clang failure and earlier QEMU
inspection remain historical evidence in Git; this check does not execute the
full old Clang closure.
