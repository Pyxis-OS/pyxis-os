# Lua launcher source and lifetime qualification

2026-10-28. Accepted contract: [script authority](../../userland/script-authority.md).
Candidate userland `b714efe`, ports `f24ee20`, on source mains `7f82db8` /
`c5291f7`; compiler `pyxis-llvm23.1.3-8a4d001` unchanged. SDK runtime and the
changed Shell/startup/session/Remote/mux programs were rebuilt, as were the full
libuv, Lua 5.5 and Lua 5.1 recipes. Lua 5.1 retains two upstream indentation and
signedness warnings; changed code compiled cleanly. The final userland amendment only updates API
comments; runtime code matches the qualified `9e62870` tree.

## QEMU

Nested Q35/KVM, host CPU with invariant TSC, four CPUs, 4 GiB, OVMF, virtio-net/rng,
no disk. Reused frozen kernel `ddf2291dd78d`; rebuilt userspace above and the same
existing public Mesa triangle fixture. This is component qualification, not an
exact-head full-image or installed-package/native check.

Commands used explicit `boot://lua.pxb` / `boot://lua5.1.pxb` plus script paths,
`-e`, `- < script`, native shebangs, cross-interpreter calls, a pipeline and a
shell capture. Lua 5.5 `pyxis.run` and Lua 5.1 luv both selected llvmpipe and drew
the triangle with JIT enabled. Both selected softpipe with the space's JIT source
absent. A Lua bundle omitting its JIT request selected softpipe even in the
JIT-enabled space. Existing shell script launch still selected llvmpipe.

Matched Shell/Lua background lifetime checks launched a delayed FILE writer.
After handed-script completion it wrote successfully; after positional-script
exit it did not. Lua's normal `os.exit` and unhandled-error cases preserved this
distinction. A synchronous `pyxis.run` of an interpreter retired that foreground
command's descendants before returning. These are observed ordinary lifecycle
checks, not a kernel object-count/leak proof. Captured shebang jobs returned
`CALL_BAD_REQUEST`, without expansion or fallback.

The first fixture attempt mixed the new `path` Shell with an old `commands`
session; Remote could not start. Rebuilding the affected startup/session chain
resolved it. It is excluded from results. A background fixture initially tried
to inherit absent stdin and correctly failed `EBADF`; the corrected check omitted
stdin. All final Remote sessions drained and all QEMU processes stopped.

The final ports restack includes the merged native usable-CPU queries; libuv and
Lua 5.1 were rebuilt and these cohorts rerun on that head.

## Costs

Existing Remote command intervals: one warmup, five retained samples per row;
includes host transport, interpreter, child and cleanup. No new benchmark code.
The before artifacts were captured before implementation and retain their hashes,
but are reused executables rather than exact source-main rebuilds. The after
startup/session chain also includes the merged native `path` work. These figures
therefore do not isolate a Lua code-change speedup or a native CPU cost.

| Workload, JIT source | Before median [min–max], ms | After median [min–max], ms |
| --- | --- | --- |
| Native echo, on | 5.983 [5.301–9.909] | 4.392 [3.595–9.901] |
| Lua 5.5 run echo, on | 8.542 [7.590–11.540] | 7.595 [7.097–8.256] |
| Native echo, off | 5.956 [4.176–6.153] | 3.495 [3.335–5.273] |
| Lua 5.5 run echo, off | 8.009 [7.487–13.977] | 6.860 [6.186–7.138] |
| Lua 5.1 luv echo, on | No comparable successful baseline | 7.426 [7.237–7.934] |
| Lua 5.1 luv echo, off | No comparable successful baseline | 7.340 [6.995–7.600] |

The requested before Lua 5.1 artifact lacked a luv preload and failed before
spawn. A separately identified older artifact could spawn echo but had unknown
exact source provenance; its intervals are supplementary raw evidence only.
The candidate's restored standalone preload makes the current reached path work.

Small raw records, commands, hashes and baseline caveats are under
`/shared/present/script-authority/lua-qualification/`. Roots, SDKs, builds and
images stay on the local disk. REPL interaction, forced interruption and native
hardware were not newly qualified; shared routing and embedding opt-in were
source-inspected. The separate parent CI checks the submitted integration head.
