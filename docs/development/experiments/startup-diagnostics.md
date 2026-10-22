# Runtime startup diagnostics (2026-10-22)

Initial kernel base `87bbde00`; final integration is rebased onto `71369bf1`
(merged readiness sets, #1113). Userland/ports revisions are pinned by the task PR. The final PR carries
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
shell: host://after-stale.pxe: did not start: runtime resource retention: unknown system call
```

Unknown syscall results in optional resource queries are fatal, not absence.
The first runtime retention query reports `PROCESS_START_FAILED`, step 5/status
0xffff (packed reason 393215). This is an
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
entering `user_start_failed(393215)`. Earlier qualification of the standard-stream
failure (step 7/reason 524287) observed BSP completion with kind 4 and a null
process pointer after execution retirement.
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

## Review fixes

The repeated four-CPU query mismatch now fails at runtime resource retention;
the matching program still reaches main. Heap initialization also treats an
unknown environment/COPY call as START_FAILED and uses checked startup CLOSE;
that isolated heap-only error path is source-inspected and built, not separately
injected.

A one-CPU private archive launches a bad init between healthy spaces, retaining
a healthy Remote root ahead of it in the observer list. Boot-init prints exactly
one `space bad-init init did not start: runtime resource retention: unknown
system call` on its Caelum streams; Remote still answers `hostname`. Boot-init
releases configuration, mount/network views, startup environment entries and
captured authorities, then stays resident with root WAIT-only observers and its
runtime/stdio/clock references. No restart, new rescue or tab transition is
inferred from a runtime failure. A private installer entry keeps the real matching
init-install parent but selects the mismatched child; the parent prints the same
step/cause instead of `completion kind 4`. No target disks are attached.

The reaper reports otherwise-unobserved job-client START_FAILED results. The
foreground shell passes false because it prints its captured stage result after
cleanup, avoiding duplicate lines. This path owns ordinary client-leaf observers;
delegated provisioner trees keep their existing distinct lifetime rules. The
unobserved reaper formatting is source-inspected/build-qualified.

The separate Go runtime patch builds official hello using existing host tools
and the changed target runtime only. Four-CPU QEMU runs hello successfully;
a read+sleep clock without PRECISE gives `runtime resource retention: permission
denied`. Redirected child stderr stays empty while its parent prints the cause.
A never-shipped runtime variant changing only HANDLE_INFO syscall 9 to retired
5 reports `standard output: unknown system call` on the one-CPU guest.
The assembly terminal path has no stream, FS, heap or lock access.

After #1113 merged, Go #6 remained based on current Go main `40e13dd5c5`
(the rebase was a no-op); the recipe adopts published `b4c660167a2f` rather than
patching the obsolete runtime pin. The integrated ordinary image rebuild passes.
One- and four-CPU QEMU repeat hello, files/directories, positioned I/O, supplied streams,
buffered EOF, blocked close, deadlines and timers. Two rounds of 1,000 distinct
pipes on four CPUs pass sparse/all-ready identities, close/reuse, GC/copied stacks and deadline
changes using the existing readiness qualification callers rebuilt from this
runtime. GDB observes the Go missing-PRECISE report entering
`user_start_failed(327682)` (RESOURCES/DENIED); the child stderr file is empty.
These are functional checks, not new performance or native hardware measurements.

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
