# LLVM native JIT adapter checks

2026-10-27, disposable nested-KVM QEMU, four vCPUs, Q35, `host,+invtsc`, 4 GiB
preallocated RAM, VirtIO display/network/RNG and OVMF. Diskless live archive;
only the temporary Remote space enables JIT. `nvme.write=0`,
`display.timing=off`, `log.level=info`. No native-hardware or performance result.

Baseline development libraries `a63ce5822d3b5319a568b499dba8f3172633eaf9`;
updated fork `60aa4d7687d51b99998332bb5f82638a61650d3d` (pyxis-llvm #20).
Kernel/SDK build uses parent `1da64367` with the development-prefix producer
changes, based on merged JIT stack `d180f7eb`; kernel runtime code is unchanged.
Userland `79035bb9`, ports `f209c917`. Compiler remains
`3d69e83c443e2acf7cf83db35037899f99cf76b3`; no compiler/container rebuild.

## Build and commands

The normal `toolchain/build-llvm-development.sh` rebuilds the complete target
closure, adding `orcjit` to the producer's component list. Its Linux host
generators and native target archives build successfully. The ORC C header
needed its own `<stddef.h>` include, as the execution-engine header did earlier.
The independent native consumer uses only exported headers, archives and
`llvm-config` flags, not source/build-tree include paths.

Exported prefix identity
`0d36f168e87e531aaef47d11855de5b4f619c21b46e64603a5b757769b947cf5` contains
71 native archives and the existing notices/provenance inventories. This is a
library-prefix rebuild, not a hosted-Clang or compiler-container update.

External manual source, manifests, compile commands, hashes and captured results:
`/shared/present/jit-grant/llvm-check/`. The normal/deny bundles use the same
consumer, with/without an explicit JIT declaration. The baseline consumer is
compiled against the old prefix without its absent OrcJIT component. Run via
Remote:

```text
boot://llvm-baseline.pxb baseline
boot://llvm-deny.pxb deny
boot://llvm-check.pxb mcjit
boot://llvm-check.pxb orc
```

## Results and remaining gates

- Old-prefix baseline PASS: SMM allocation/finalization refuses, no generated
  code executes and JIT gauges remain zero, despite held JIT authority.
- Missing-grant PASS: SMM code and read-only-section allocation refuse, no JIT
  backing is charged. It does not enter LLVM's fatal allocation-failure path.
- New ORCv2 PASS: ordinary mapped RW allocation remains on MEMORY while JIT is
  held. Direct SMM sections execute 42/43 after finalization, later sections
  finalize on the same manager, and earlier code remains usable. ORCv2 then
  compiles two IR modules whose functions volatile-load separate constant data
  and compute 35+7=42 and 36+7=43. Executable and read-only gauges are positive
  while live; every JIT byte/region/state gauge returns to zero on destruction.
- MCJIT is **not qualified**: after the same direct SMM checks, EngineBuilder
  refuses with `dlopen() not supported on this platform` and the process exits with status 1.
  No MCJIT-generated function is called. Its unconditional process-symbol scan
  needs the [open decision](../../wip/jit-llvm.md#open-decision-1--mcjit-process-symbol-bootstrap).
- Source review also found MCJIT discards permission-finalization errors. ORC's
  RuntimeDyld path propagates them. The MCJIT correction remains undecided and
  must precede an execution-success claim; it was not exercised by inventing a
  successful native permission call or bypassing the standard engine builder.

ORC explicitly selects RuntimeDyldObjectLinkingLayer with SectionMemoryManager,
baseline x86-64 CPU, large code model, no process-symbol linking, inactive
platform and no compiler workers. This does not qualify default JITLink, ORC
stubs, remote transport or shared-library discovery. No llvmpipe, native
performance, live patching, strict-near placement or allocation-failure claim is
added. No native packet was staged.
