# Hosted Clang task 4 integration record

2026-10-12, before implementation. Requested base `4366eb28`, userland
`21cf935c`, ports `fd1eb30`, fs `30f625`, lwIP `a1aadb91`; compiler fork
`2d234435`, published builder `pyxis-llvm23.1.3-2d23443`.
The reused qualified image was actually built at `2f963d05`, not `4366eb28`:
only three docs differ; kernel/ABI/SDK/boot inputs, dependency pins and active
configuration are identical. No compiler/container rebuild was needed.

Fresh one-/four-CPU QEMU 10.2.2 Q35, nested KVM/host CPU, 2 GiB, UTC,
raw OVMF, standard VGA, modern VirtIO SCSI/net/RNG. No task build, profiler or
debugger during timing; unrelated VMs remained, so host load was not isolated.
Existing commands:

```text
iobench write tmp://clang-baseline.bin --rounds 3
iobench read tmp://clang-baseline.bin --rounds 5
# Lua: 16 echo launches warmup, then three loops of 1024 echo -n children.
```

All 16 timed I/O samples and four warmups verified 1 MiB counts/content,
with no failures; launch counts matched and all 12 sessions ended FINAL 0/complete.
No exclusions. Median (minimum–maximum), milliseconds except launch wall:

| Interval | 1 CPU | 4 CPU |
| --- | --- | --- |
| RAM write | 12.289 (11.723–16.817) | 10.056 (9.997–10.101) |
| Read payload | 0.439 (0.399–0.888) | 0.419 (0.400–0.431) |
| Read open/read/EOF/close | 0.530 (0.480–0.985) | 0.496 (0.477–0.507) |
| 1024 launches, all wall samples s | 3.81 / 3.71 / 6.95 | 6.41 / 5.14 / 5.60 |

Clock calibration was 35,625–114,283 ns/read, not subtracted. Launch includes
discovery/session setup; RAM writes grow without sync. This is an OS baseline,
not compiler performance, physical-storage durability or native ThinkPad evidence.
This baseline preceded the task 4 cross-build; compiler execution remains pending.
Raw commands, provenance/hashes and captures remain local in
`build/hosted-clang-task4-baseline/`; owned VM/client jobs are stopped.


## Cross-build and qualification

Source baseline: fork `2d234435`, current threaded SDK above, verified payload
checksums. Main advanced to `505ec816` with docs-only changes; all non-doc inputs
remain identical. Current compiler/container and exact-pin host TableGen binaries
are reused; no target generator executes on the build host.

Release, Generic/Pyxis `x86_64-unknown-pyxis`, X86 target, projects Clang and LLD;
LLVM workers, tests, examples, benchmarks, docs, PCH and optional external
libraries off. Only `clang` and `lld` targets are built (`make -j8 -k clang lld`);
unrelated shared-library tools are disabled. Standalone LLD retains its real
format-driver closure. Hosted-only CMake defaults are `clang_resources://` and
`sysroot://`; no serial-only source replacement or target host-libc linkage.

The untouched build reached deleted metadata accessors in Clang cache/header,
driver, dependency scanning and code generation; direct time-type includes,
file-stream inputs unavailable in the SDK profile, a Unix exit-code assumption,
and the absent native host-triple definition. Subsequent builds reached explicit
C API size-type includes, TextAPI equivalence and nondeterministic archive
metadata. [Fork #7](https://git.internal/PyxisOS/pyxis-llvm/pulls/7) checks metadata
or refuses unsupported operations, preserving native cache refusals before effects;
optional file inputs use the real copied-buffer backend. Reached changed target
TUs compile; both real target links complete with the math dependency below.
This is a native-target build, not a host binary renamed as a Pyxis program.

The reached links require `log2`, `log2f`, `logb`, `log1p`, `erf`, `exp2` and
`lround`. [Userland #242](https://git.internal/PyxisOS/pyxis-userland/pulls/242)
extends the existing FP-exception-only musl subset from its exact cached
`0784374d` source, with required `ilogb` and paired `erfc`; no new download.
The ordinary libc build passes. After main advanced with merged HTTP work,
qualification rebased on `e5dc4b2c`, userland `b0c0c0a`, ports `f93becb9`.
Ordinary SDK/kernel and fresh ports/userland image builds pass, including all
four bundle verifiers. Those later ABI/kernel changes are separate from this
compiler task; the earlier OS baseline does not establish their cost or source
equivalence. Setup-only missing Kconfig Python module was corrected before the
successful build. The complete final-header cross-build passes (fork `4c23a208`,
compiler/SDK C++ runtime source `2d234435`, LLVM workers off).

Full configuration and build logs remain local in the fork worktree's `build/`.
No new compiler pin was needed for local qualification; the current host
compiler generates the required code. Production fork adoption requires its
matching owner-published container under the single source-pin contract.


## Native artifacts and interactive result (2026-10-12)

P1F stores loadable bytes only, without ELF debug/symbol sections. Exact payload
lengths and page-rounded segments were inspected before boot; no ceiling changed.

| Tool | Serialized P1F bytes | Mapped span/page bytes | Zero tails |
| --- | --- | --- | --- |
| Clang | 90,335,834 | 90,927,104 | 586,912 |
| LLD | 55,731,474 | 56,340,480 | 603,608 |

A temporary unpacked Clang bundle lives in a private read-only virtio-fs export,
outside the boot archive. Its 8 MiB stack, `clang_resources`/`sysroot` roots and
four required runtime grants match the accepted profile. A declared Remote space
has read-only host access and writable `tmp://`; a separate network-owner space
performs normal network setup. Small source inputs alone are in `boot://`.
No call-site grant wrapper, installation or launcher grant is used.

Interactive QEMU 10.2.2 Q35, nested KVM/host CPU, 1 then 4 CPUs, 2 GiB, fresh
OVMF variables, VGA, VirtIO SCSI/net/RNG/fs. Qualification image `2a410105…` uses
the above `e5dc4b2c` kernel base, userland `b0c0c0a` and fs `30f625`; these actual
inputs were frozen before subsequent FAT32 main advances. Userland rebased as
`a53c1490` without math/header/build changes. Ordinary delivery CI uses the later
main; neither its cost nor native ThinkPad behavior is inferred from these boots.

```text
host://clang.pxb -c boot://share/clang/hello.c -o tmp://hello.o
host://clang.pxb -c boot://share/clang/bad.c -o tmp://bad.o
host://clang.pxb -c boot://share/clang/missing.c -o tmp://missing.o
# 4 CPUs: output to the read-only host root
host://clang.pxb -c boot://share/clang/hello.c -o host://denied.o
```

Default compilation exits 0 on both CPU counts. The SDK stdint/stdio source
produces the same 1,160-byte x86-64 ET_REL ELF object (SHA256 `1138fa33…`).
Syntax and missing-source cases diagnose and exit 1 on both boots; denied output
also exits 1. No failed output survives; ordinary shell listing, binary readback
and final 0/complete session cleanup work.

QEMU's GDB confirms `clang_main → CC1Command::Execute → ExecuteCC1Tool → cc1_main`;
FS points at its self-addressed libc record, with readable thread errno. On four
CPUs the process has one linked/storage member, one active root, no borrowers,
and exactly `memory`, `threads`, `clock`, `random` startup resources. A real
`pthread_mutex_lock` is reached through libc++abi `__cxa_guard_acquire` during
MemoryBuffer input. Creation/detach hardware breakpoints have no hits during
default compilation. The symbol ELF's load segments are byte-identical to P1F.
Together with the native no-recovery/no-helper source policy, this qualifies
integrated compilation without a child crash helper; no fault was injected.

Standalone LLD links but is not executed here; that and installed/native-hardware
budgets remain task 5. No compiler-performance or universal heap admission claim;
pure math additions introduce no new always-on libc work. Direct special-value
math testing was not performed. Raw configuration, hashes, GDB/remote/serial logs
remain in local `build/hosted-clang-task4-qualification/`; all owned VM/debugger
and filesystem-daemon jobs are stopped.
