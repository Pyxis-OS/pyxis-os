# Startup CPU allowance

2026-10-27. This supplies the CPU input for Mesa's upstream worker policy;
it does not qualify llvmpipe or measure rendering performance.

## Inputs

Kernel `ddf2291dd78d` on main `aa03ef2e4`, userland
`254353bb8158a18ec3e6c4b9b6062e47e8ab4e0a`, Go
`01cf32642ff970c21f93c05cf10762c86f2c16d4`, Rust target std
`35681ba73f8f36ac3e49ffd082294482042903d2`. Compiler `8a4d001d`
and its libc++ are unchanged; the SDK rebuild selects libc++'s existing
sysconf implementation. Rust std is built separately with the existing
`e635120a7` host compiler. No compiler container is rebuilt.

Disposable nested-KVM Q35, four vCPUs, host CPU with invariant TSC passed
through, 4 GiB preallocated RAM, OVMF, VirtIO display/network/RNG and a diskless
live fixture. Remote launches the same three C++/Rust/Go helpers with different
space CPU masks. Their manifests omit `system_info`; Go has no GOMAXPROCS
environment override. Retained launcher binaries are from the preceding
VM-admission fixture, not a claim of a fully rebuilt live image.

## Results

| Four-vCPU space allowance | Native getter and both C sysconf selectors | C++ hardware_concurrency | Rust available_parallelism | Go NumCPU/default GOMAXPROCS |
| --- | --- | --- | --- | --- |
| CPU 1 | 1 | 1 | 1 | 1 / 1 |
| CPUs 1, 3 | 2 | 2 | 2 | 2 / 2 |
| CPUs 0–3 | 4 | 4 | 4 | 4 / 4 |

All helpers exit successfully. C++ and Rust joined workers report the same
count. Go completes 32 communicating goroutines with the expected total
5,280,000. The C++ helper observes `NOT_FOUND` for `system_info`, and an invalid
sysconf selector returns -1/EINVAL.

The matched pre-change fixture uses kernel `bf74e0b7` with the shared-owner
admission fix but no CPU startup field, the prior SDK, and old Rust/Go runtime
helpers: C++ reports 0, Rust reports Unsupported, Go reports 1 even in the
two-CPU allowance. The initial new runs use kernel `4560ecb99` and
source-equivalent userland `08bb955`; all three allowances pass. After rebasing
onto current mains, rebuilding kernel/SDK/C++, the four-CPU check passes again.
The Rust CPU hook and Go artifacts are unchanged by the userland rebase.

Raw sources, build logs, exact kernel/archive hashes, command lines, masks and
Remote results remain in `/shared/present/usable-cpus/`: `before-4cpu-allowance2`,
`final-4cpu-allowance1`, `final-4cpu-allowance2`, `final-4cpu-allowance4` and
`rebased-4cpu-allowance4`. The external `run-final.py` only assembles the
disposable fixture and invokes the helpers through Remote. Own guests and
helpers stop after each run. No native, timing, live-affinity-change or Mesa
rendering result is claimed.
