# Hosted Clang reached libc operations (2026-10-11)

Task 3 follows the [real C++ closure](hosted-clang-cxx.md). Source audit:
LLVM fork `604f008d`, Pyxis base `34dfce9e`, userland `22287160`; correction
in userland `a84259c0` ([#217](https://git.internal/PyxisOS/pyxis-userland/pulls/217),
stacked on #216). The installed compiler remains `49e2c1a`; no container change.

## Compile and symbol inventory

Recompile the 170 Support C++ source units from task 1's real generated target
commands, with PCH excluded, against the real richer SDK/runtime. Same Release,
Generic/Pyxis, X86-only, workers/EH/RTTI/backtraces/optional libraries off;
no source workaround or fake declaration. 154 initially compile; APFloat fails
at `FP_ILOGB0/FP_ILOGBNAN` in bundled FPUtil. Supplying the standard INT_MIN
constants makes APFloat compile: **155/170**. The nine reached Support C units
(bundled regex and BLAKE3) and four BLAKE3 assembly units compile separately.

The remaining 15 C++ units fail at known synchronization facilities:
BalancedPartitioning, CodeGenCoverage, ExponentialBackoff (`this_thread`),
FileCollector, ManagedStatic, Parallel, PluginLoader, Statistic, ThreadPool
(future/async), TimeProfiler, Timer, DynamicLibrary, RWMutex and LSP
Transport/Logging. These wait on threads gates 5–8, not serial substitutions.
This is not a full Clang/LLD target link; task 4 must repeat the whole closure
and adapt any further compile-time unknown-metadata consumers.

The successful objects' C undefined symbols, compared with the real SDK
archives, reveal **`rint`**, reached by `Support/BranchProbability.cpp:29`.
Its declaration and implementation now use native x87 FRNDINT, with the existing
fesetround contract keeping x87/SSE controls aligned. No ilogb function or
successful placeholder is added. LLVM's own `llvm_reg*` and vectorized BLAKE3 names are supplied by the
separately compiled bundled sources. Comparing all successful objects with SDK
definitions leaves no unexplained C external. The
[disposition table](../../wip/hosted-clang.md#remaining-libc-names-classify-actual-consumers)
records absent Unix APIs, native equivalents and precise refusals.

```sh
make -j16 sdk
# Reached compile commands: actual source, no failed/stale PCH, real SDK.
clang++ --target=x86_64-unknown-pyxis --sysroot=<qualified-sdk>/sysroot \
  <generated Support flags/includes> -c <Support source.cpp>
nm --undefined-only <successful Support objects>
nm --defined-only <SDK static archives>
```

## QEMU qualification and limits

Same frozen kernel/control and nested-QEMU configuration as task 2; updated real
libc and math header, locally rebuilt richer C++ runtime, no stub symbols. Manual
foreground helper invokes bundled FPUtil's actual exponent implementation for
zero, NaN, infinity, ordinary and smallest-subnormal doubles. Native rint checks
cover ties-to-even, all four rounding modes, signed zero, FE_INEXACT, NaN/infinity,
large integral and subnormal values. All fourteen checks pass, with zero failures (manual `boot://apfloat-check.pxb`).

An attempted helper linking full APFloat also reaches gated global Support
initializers (Statistic/Timer/ManagedStatic); it does not link. The successful
helper exercises actual header FPUtil and native libc, not a substituted APFloat
class. The APFloat translation unit itself compiles. No compiler execution,
threaded/native hardware or full APFloat link claim.

Task 3 changes constants and supplies a previously uncallable function; ordinary
I/O/launch paths are unchanged. Task 2's matched cost record remains the baseline;
no speed or storage/durability claim is made here. No startup, allocator, TLS,
SDK/runtime makefile or delta-owned thread/memory file changes. No new klog lines.
