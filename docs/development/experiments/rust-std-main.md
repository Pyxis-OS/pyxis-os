# Rust std main

2026-10-25, gate 2 of the [std milestone](../../wip/rust-std.md).
This is the first ordinary `fn main`, with native startup/args, owned TEXT,
allocation, descriptor I/O, clocks/entropy, lexical paths and initial-thread
TLS cleanup. Worker creation, FILE adapters and hexyl remain later gates.

## Inputs and build

Baseline main `eff8dbb2`, userland `88872fe3`, ports `e6c51d9d`, fs `f3f22b6e`,
lwIP `d52ebd20`; captured before adapter edits. Control source `1230d8da` and final sample source `16b1abc6`
pin userland `14be162e` and inherits ports `fa9998d5`. Intervening port changes
are confined to unused LuaLS sources; reused Lua/HTTP/TLS library recipes are
unchanged. Selected SDK and native programs are rebuilt, not the entire port
collection. Builder LLVM 23.1.3-3d69e83 is unchanged.

The fork compiler is Rust `5e85f86de5f7dc45a485fa8a3eb376487923bc0b`, based on
nightly-2026-10-24 `48f4b055`, with Rust CI LLVM 23.1.4. The libc 0.2.191 overlay
is `a6aac31c`, pinned through HTTPS in compiler, library and consumer workspaces.
[Build/export commands](../rust-toolchain.md#fork-built-compiler-and-built-in-target)
use checked cache inputs, offline bootstrap and the Pyxis Clang/LLD final linker,
without user `RUSTC_BOOTSTRAP`, LTO or embedded cross-toolchain bitcode. The known
upstream std dylib warning remains visible for this static-only target.

The private fixture uses a RAM home, freshly built startup/session/Remote tools,
Lua and Rust bundles. Kernel bytes are unchanged from the baseline
(`9a83ebd757ee37088e75bc743ecdccfbf49944f9ffe0fbb6e451532f4660b50d`);
`kernel/` and `include/` have no intervening source changes. No disk, game data,
native profile, kernel instrumentation, ABI or compiler-container change.

## Runtime and artifact checks

Nested QEMU/KVM q35, host CPU, one/four vCPUs, 2 GiB, OVMF, modern VirtIO
GPU/net/RNG and VirtIO-SCSI read-only ISO; host-loopback Remote/GDB only.
The sample is built through the published exporter and ordinary SDK contract:

```sh
toolchain/build-rust-samples.py PREFIX SDK FRESH_OUTPUT WORK --fork --std
```

| Exercise | One CPU | Four CPUs |
| --- | --- | --- |
| Main: Vec/String, byte argv, 16 KiB alignment, growth/shrink | Exit 0 | Exit 0 |
| Owned private/exported TEXT and non-UTF-8 values; CAP excluded | Exit 0 | Exit 0 |
| SchemeRoot components, component equality/hash, original byte storage/cwd | Exit 0 | Exit 0 |
| Real clocks, minimum sleep, entropy-backed HashMap | Exit 0 | Exit 0 |
| Initial TLS String and destructor diagnostic | Exit 0 | Exit 0 |
| Pipe/file input and exact NUL/FF output bytes | Exit 0 | Exit 0 |
| Reduced manifest without clock or random | Diagnostic, exit 127 | Diagnostic, exit 127 |
| Background child without stdin | Native error reported | Native error reported |
| Duration/deadline overflow | Diagnostic, exit 127 | Diagnostic, exit 127 |
| Explicit panic / real oversized allocation | Exit 1 / 102 | Exit 1 / 102 |
| Healthy main after failures; rebuilt core+alloc hello/raster | Exit 0 | Exit 0 |

Reduced manifests remove the selected grant while retaining other services;
unsetting shell TEXT does not remove the shell's retained launch authority.
Missing clock reports native errno 4; missing random reports errno 8. Explicit
panic follows libc abort's exit 1, without an invented Unix signal. Backtrace
capture is Unsupported, as are the sample's File/Command/TcpStream operations.
Absent stdout/stderr were source-inspected, not runtime-qualified here.

Runtime exposed an SDK linker defect: orphan `.got` inherited PT_TLS after
`.tbss`, shortening the relocation basis to 24 bytes despite a 104-byte template.
Explicit `.got :data` before TLS fixes it. ELF/P1F load entries, permissions,
file/memory extents and every payload byte agree. TLS initialized/total sizes
are 16/104 bytes at alignment 8, and INITIAL is at FS-88. GDB confirms libc's
FS self-pointer, C-main arguments/alignment and the live five-byte `owned` String
at destruction, without injecting guest calls. Local console launch also passes.
The final scan covers 26 rlibs/506 ELF archive members, with no embedded
`.llvmbc`. Final std-main P1F SHA-256 is
`d07baa2d65d77f7bc6d60f10dd46c4b664a8f7441e09447b18e783db124a5edc`.
Duration conversion and absolute clock-deadline overflow each report native
LIMIT (11) and exit 127, without clamping or starting a long wait.

The new cleanup hook is opt-in: the C++ exit-handler qualification first creates
TLS after ordinary drainage and still completes both that callback and an older
exit handler. This preserves existing non-Rust behavior; it does not promise an
extra late destructor pass. Worker/key exhaustion and contention remain gate 3.

## Matched native-program controls

These measure ordinary native launch/exit, not Rust-main performance. The after
fixture's native programs and runtime use userland `14be162e`; Rust bundles are
not executed during the measured envelopes. No concurrent build runs; GDB is
detached. After 16 warmup children, three fresh sessions per CPU count execute:

```text
lua -e 'for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'
exit
```

| 1,024-child whole Remote session | Initial baseline (s) | Repeated control (s) | After (s) |
| --- | --- | --- | --- |
| One CPU | 0.815, 0.815, 0.815 | 0.800, 0.796, 0.792 | 0.787, 0.790, 0.795 |
| Four CPUs | 0.865, 0.915, 0.865 | 0.867, 0.856, 0.858 | 1.027, 0.891, 0.873; 0.855, 0.850, 0.858 |

One-CPU order is initial baseline/after/control; four-CPU order is
initial baseline/after/control/after. The first after cohort's slow sample
is retained. It does not reproduce in the second comparison, whose ranges overlap.
There is no Rust performance or native-hardware improvement claim. Every recorded
program/shell exits zero with `drain=complete`; timings include Lua, Remote and
drain rather than isolating loader work.

Whole-guest PMM snapshots follow all three launch cohorts, then hold one Remote
shell after an echo, then close it:

| Allocated 4 KiB frames | Baseline | After |
| --- | --- | --- |
| Warm idle | 9,713 | 9,917 |
| Held session | 10,104 | 10,308 |
| After session exit | 9,715 | 9,919 |
| Active-session delta | 391 (1.527 MiB) | 391 (1.527 MiB) |

These include kernel/page-table/cache backing, not a deduplicated process-owned
resident census. Larger qualification archives reserve different boot memory;
absolute frame counts are not attributable solely to runtime size. Both retain
two additional frames after the observation; no leak or cache-cause claim.

Raw image/configuration hashes, compiler receipts, serial/client completions,
debugger captures and the local-console screenshot stay in
`/shared/present/rust-std-gate2/`. No native/PXE machine is accessed.
