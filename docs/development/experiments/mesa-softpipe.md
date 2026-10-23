# Mesa softpipe qualification

2026-10-23, task 2 of [Quake 3/Mesa](../../wip/quake3-llvmpipe.md).
Mesa 26.2.5 with the 25.0.7 OSMesa context/render restoration; both cached
archives and signatures were checked in task 1. Builder
`pyxis-llvm23.1.3-3d69e83`; development LLVM is not used in this configuration.
SDK/runtime qualification used userland `9a70c09`; the submitted rebase onto
`67da447` changes only boot-init/installer, with identical libc/library sources.
The ports profile is recorded by PR #158; no kernel ABI or compiler pin changes.

## Build and rendering

The mirrored clean-source recipe configures and builds static softpipe, with
LLVM, window systems, shared libraries and disk cache off. The staged archive
has debug sections stripped; a separate consumer using only staged headers and
archive is byte-identical to the build-tree consumer. A whole-archive link with
section collection disabled also passes: disabled cache/DRI helper objects do
not leave unresolved imports hidden in the archive. Notices retain the reached
source/header and generated-parser terms, including Bison's larger-work exception.

The first triangle faulted on NX heap execution: even softpipe's optional SSE
vertex translator generated code, and its generic OS allocator returned ordinary
malloc storage. The native profile now selects its existing C translator and
refuses executable allocation. It does not disable ordinary compiled SIMD or
add JIT authority. `OSMesaPostprocess` is absent, as accepted.

Q35/nested KVM, four max CPUs, 4 GiB, OVMF, modern VirtIO network/GPU/RNG, no
audio. The existing full image `18bc650a` (main `f7cfda2b`, userland `f057a600`)
was reused: its kernel ABI is unchanged for this slice. The manually loaded
programs link the new SDK; unrelated image programs were not rebuilt.

The declared manual bundle prints:

```text
MESA_RENDERER softpipe
MESA_TRIANGLE COMPLETE width=256 height=256 postprocess=UNAVAILABLE jit=NONE
```

It checks background/centre pixels and the unavailable procedure lookup. Native
FILE readback is a 196,623-byte PPM showing the expected colour triangle;
the final recipe and initial corrected build produce identical pixels.
Hardware-breakpoint GDB reaches native main and `_mesa_Finish`; physical
page-table inspection finds the pixel buffer writable, user-accessible and NX.
This is an emission/permission observation, not a general leak or allocation
failure proof. No generated code or display service is needed.

## Shared libc checks and cost

A manual C program checks memory FILE empty/growth/embedded-NUL output, caller
and allocated buffering, backward/high-water and gap seeks, flush-all, close
ownership, read/wide/fileno refusal, negative/overflow positioning and recovery.
Reached math checks include a fused-rounding case that separate float multiply
and add would lose, plus upward/downward rounding through real fenv. All pass.
The ten imported math sources match the existing musl pin byte for byte.

Existing `iobench read tmp://mesa-bench.bin --buffer 4088 --rounds 15`, its own
1 MiB fixture generator, one warmup per cohort, A1/B1/A2/B2/A3/B3 on the same
quiet guest; no profiler or debugger during timing. Forty-five verified samples
per side. A is the pre-task `f64927b` SDK; B includes the libc changes on
`e57d687`, so this is an integrated comparison rather than isolated attribution.
Clock-call costs are not subtracted. All 257-read passes and EOF checks succeed.

| Phase, microseconds | A median [min, max] | B median [min, max] |
| --- | --- | --- |
| OPEN | 4.397 [4.043, 40.381] | 4.370 [4.010, 14.600] |
| Payload | 329.434 [312.914, 417.109] | 328.024 [314.139, 480.508] |
| Complete | 335.514 [318.241, 422.951] | 333.453 [319.764, 498.247] |

No measured regression; ranges overlap. These are nested FILE costs, not native
rendering or network throughput. No pre-existing Mesa consumer existed for a
rendering baseline. Physical-machine rendering, formal GL conformance, SDL GL
presentation/input/resize, game performance and llvmpipe/JIT remain unqualified.
Raw commands, logs, PPM/PNG, GDB observation, hashes and samples:
`/shared/present/quake3/mesa-softpipe/`.
