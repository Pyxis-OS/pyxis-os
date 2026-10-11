# Hosted LLVM Support task 1 (2026-10-11)

[Fork PR #4](https://git.internal/PyxisOS/pyxis-llvm/pulls/4), initial delivery head
`f02d210f656e3f2d71fde9d004982662793682f5`, started from pinned `49e2c1a1518b`.
Compiled/QEMU source `d68801c4` has an identical tree to the submitted head;
rebasing onto official branch `46a4376c` (itself tree-identical to the pin)
permits fast-forward integration.
[Platform contract](../llvm-support.md); no compiler/container or SDK fork pin
changed. No shared libc/runtime edit was made.

## Configuration and builds

Existing builder `pyxis-llvm23.1.3-49e2c1a`, ordinary CMake Release, X86 only,
LLVM workers/EH/RTTI/backtraces and optional compression/editing dependencies
off. Host builds use Linux and host standard libraries. Cross builds select
Generic with the Pyxis compiler target and real SDK; native TableGen executables
remain host tools. `LLVM_ON_PYXIS=1`, no Unix backend or falsely discovered
pthread/dl/rt companion libraries.

```sh
cmake --build <fork>/build/support-host --target LLVMSupport -- -j16
cmake --build <fork>/build/support-target --target LLVMSupport -- -k -j16
```

Host Support passes. Full target Support remains blocked by the planned
stream/wide/random/synchronization closure and reached libc
`tolower`/`FP_ILOGB0`/`FP_ILOGBNAN`. Final native Path, Program, Process, Memory,
Signals, crash policy and output units compile, as do VFS and Clang FileManager.
Changed compiler/linker units still meet existing C++/TLS blockers; no compiler
link or execution is claimed. A local manual helper links the actual successful
Support objects with section collection and real SDK libraries; no placeholder
symbols or substitute synchronization. It is not an ordinary image component.

QEMU 10.2.2, nested KVM, q35, CPU max, four cores/one thread, 256 MiB,
raw OVMF, standard VGA with display disabled, VirtIO-SCSI CD, VirtIO RNG and
modern VirtIO net. No disk, HOST mount, profiler or debugger during timed runs.
Remote client and SDK use Pyxis `2fca30fd`, userland `df210f1a`, ports `df3210b0`,
filesystem `b427df29`. Qualification has the same frozen kernel/SDK inputs plus
the manually selected fork helper. Parent docs main advances are not substituted
into this control; later IPC performance changes are excluded.

## Native I/O and launch boundary

Captured before Support edits: 1 MiB RAM writes, three verified samples after
warmup, median **10.061 ms** (9.975–11.601); five payload reads, median
**0.396 ms** (0.371–0.429). 1024 `echo -n` launches: **15.17 s**, all EXITED/0,
complete final output drain. These are native transfer and remote-shell launch
costs, not LLVM or compiler timings.

Interleaved order after implementation: B1, A2, B2, A3, B3, A4. A is the frozen
pre-change image (`2fca30fd`); B is ordinary image `1f230e79`, whose measured
kernel/ABI/SDK/userland source inputs are identical to A. B contains no helper.
One preliminary A run overlapping a local helper build is excluded. Each boot
repeats the same commands, accelerator, CPU count and devices; no build runs
overlap the six retained samples.

```text
iobench write tmp://support-io.bin --rounds 3
iobench read tmp://support-io.bin --rounds 5
# Separate fresh remote shell: 1024 lines of "echo -n", then exit.
```

| Measurement | A: three per-boot medians | B: three per-boot medians |
| --- | --- | --- |
| 1 MiB RAM write, ms | 9.919, 9.968, 9.961 | 9.999, 9.898, 9.965 |
| 1 MiB payload read, ms | 0.351, 0.348, 0.373 | 0.359, 0.359, 0.358 |
| Open/read/EOF/close, ms | 0.426, 0.422, 0.448 | 0.443, 0.435, 0.435 |
| 1024 remote-shell launches, s | 13.60, 13.23, 14.44 | 14.30, 13.69, 14.27 |

Every transfer verified and every launch exited zero, with complete drain.
Write samples span 9.885–10.050 ms for A and 9.893–10.054 for B; payload reads
span 0.340–0.376 and 0.347–0.415. Clock-call overhead is roughly 36 µs/read,
not subtracted. Writes grow from zero without sync. Ranges overlap; the ordinary
consumers do not link this Support fork, so these controls cannot attribute a
compiler or adapter performance change. RAM/nested-KVM results are not native
storage, durability or ThinkPad measurements.

Separately, the new helper's 1 MiB copied RAM reads took **3.637, 0.390,
0.339 ms**, with complete byte comparisons; first use includes cold allocation.
Staged output creation/allocation/copy/commit took **19.913, 18.158, 15.330 ms**,
all commits successful. Buffer destruction is outside both intervals. These
operations include different work from iobench's payload-only, unsynced transfer;
they establish adapter costs, not an apples-to-apples speedup or compiler budget.

## Functional qualification

Foreground QEMU helper, through a bundle selecting memory, clock, random and
child-launch authority: copied reads; held-versus-replacement identity; missing
component before `..`; unknown archive mtime; private zeroed page-rounded memory
and release; staged replacement, discard and positional writes; changed-parent
refusal; provider, permission, mode, append and protection refusals. A child sees
the copied cwd/environment and no launcher grant; EXITED/-1 remains distinct from
launch failure. Timeout and missing-authority requests refuse before publication.
Debugger inspection confirms retained identity references, unknown archive mtime
and native completion kind/status.

Further checks pass at the initial delivery head: adopted descriptor reads the held
original after name replacement and its status retains identity after descriptor
close; zero-length output; denied staging into boot; encoded staged output;
unknown stdout flags; async child EXITED/7 with an explicit environment replacement
and completion owner retired to zero; oversized capture refusal; legacy named
`ToolOutputFile` refusal preserving the existing bytes.

Publication uncertainty, failed sync after rename and fatal-fault behavior are
source-inspected, without injected failures, power-loss tests or hardware runs.
Real npfs/HOST output qualification, full compiler peaks/stack/handle budgets,
runtime thread gates and compiler/linker execution remain later tasks. Raw logs,
screenshots and the manual helper stay local or in PR discussion.

## Metadata review follow-up

Fork head `6951d3c955e769a566c238fb858fcf12b46ba46c` deletes unconditional native
metadata accessors and adapts the already reached callers to checked optionals.
The measurements above remain from the initial delivery; they are not timings
of this follow-up. No kernel, SDK or container input changed.

Host Support builds pass. All 145 previously successful native Support objects
remain buildable; the full target build has its existing prerequisite errors,
with no deleted-access diagnostics in Support. Native FileManager/FileEntry
and host FileManager/lexer units compile. Native lexer attempts retain their
existing wide-string, localization and threading errors, with no metadata errors.
A local compile-only probe rejects all 17 unchecked filesystem, VFS and
FileEntry/ref calls. Task 4's complete Clang/LLD build will expose the remaining
consumers the same way.

The rebuilt manual helper passes in the same QEMU configuration: archive time,
access time, link count and ownership remain absent; default filesystem/VFS
status supplies no identity or time, and unknown equivalence has no value.
Copied reads, held replacement identity, private memory, staged output,
changed-parent refusal and explicit child launch still pass. No Clang executable,
npfs sync measurement or native hardware result is claimed.
