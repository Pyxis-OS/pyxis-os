# LLVM native JIT adapter checks

2026-10-27, disposable nested-KVM QEMU, four vCPUs, Q35, `host,+invtsc`, 4 GiB
preallocated RAM, VirtIO display/network/RNG and OVMF. Diskless live archive;
only the temporary Remote space enables JIT. `nvme.write=0`,
`display.timing=off`, `log.level=info`. Functional qualification only; no native
hardware or performance claim.

## Inputs and build

Baseline development libraries `a63ce5822d3b5319a568b499dba8f3172633eaf9`;
final fork `2b3a3a41df746cc0ee3d4bfccc5dba495d232f0e` (pyxis-llvm #20), based on
`8a4d001d`. Final kernel build revision `702be811`, collector `ad4d1242`, parent
base `b1fd5a32`; changes between build and collector concern only library pin/docs.
Userland `333b094d`, ports `f209c917`. Final compiler/SDK `8a4d001d`; baseline
used compiler `3d69e83c`. This task does not move the compiler or hosted-Clang pin
or require an additional compiler-container rebuild.

Normal `toolchain/build-llvm-development.sh` builds the complete target closure,
including `orcjit`. Final prefix identity
`128535ce7630e0c16fbbb9794ae05aa513dd21ba6e2db7e0e4a36ca6366e00df` contains
71 native archives with the normal notices/provenance inventories. Its relocated
Meson config-tool discovery passes all eleven required component groups. A fresh
native consumer compiles/links using only exported headers, archives and
`llvm-config` flags, not source/build-tree include paths. Current kernel/SDK and
focused launcher/userland builds also pass.

External manual source, manifests, compile commands, hashes and captured results:
`/shared/present/jit-grant/llvm-check/`, with baseline `before-llvm-4cpu` and final
`final-llvm-4cpu`. Remote commands:

```text
boot://llvm-baseline.pxb baseline
boot://llvm-deny.pxb deny
boot://llvm-check.pxb mcjit
boot://llvm-check.pxb orc
```

## Results and limits

- Baseline PASS: old SMM allocation/finalization refuses, no generated code
  executes and JIT gauges stay zero despite held JIT authority.
- Missing-grant PASS: code and read-only section allocations refuse with zero
  JIT backing. The qualifier does not enter MCJIT's fatal allocation path.
- Direct SMM PASS: ordinary mapped RW stays on MEMORY while JIT is held.
  Separate code/RO sections execute 42/43; later publication on the same manager
  preserves earlier code. Borrowed descriptors retain native provenance.
- MCJIT PASS: two IR modules volatile-load separate constant globals and call a
  host function registered with `LLVMAddSymbol`, producing 35+7=42 and 36+7=43.
  Adding the second module preserves the first function. The standard engine
  builder now skips only its unsupported automatic process scan on Pyxis.
- ORCv2 PASS: two IR modules produce the same values through an explicit
  RuntimeDyldObjectLinkingLayer/SMM factory, no process-symbol linking, inactive
  platform and no compiler workers. Both engines use baseline x86-64 CPU, static
  relocation and the large code model.
- Every live execution check observes executable and read-only JIT backing.
  Destruction returns all byte/region/state gauges to zero. Both engines and
  the missing-grant process exit successfully; the QEMU guest/helpers stop.

Upstream MCJIT finalization-error behaviour is unchanged. Native protection
retries definite pre-effect BUSY only; this run does not claim retry exhaustion,
unknown-outcome or finalization-failure coverage. No fault is fabricated.
Default JITLink, remote transport, shared-library discovery, strict-near
placement, live patching, llvmpipe workers and native hardware remain unqualified.
