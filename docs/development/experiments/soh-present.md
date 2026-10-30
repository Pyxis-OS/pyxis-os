# SoH presentation and synchronization attribution

## Native baseline (2026-10-30)

Vela, diagnostic kernel 9bae87a8 on 7481526a, userland 3a8a398c,
ports 90ed9e60, SoH 9.3.1, Mesa 26.2.5/LLVM 23.1.3 llvmpipe with its default 16
workers. Content/canvas 2880×1768, requested interpolation 40, MSAA/vsync off.
HWP applied 16/16 and XSAVE 0x7 were observed. Each condition has two 256-frame
captures. These are instrumented samples, not uninstrumented frame-rate claims.

| Condition | FPS | Observed thread CPU | SDL pixel copy |
| --- | --- | --- | --- |
| Native gameplay | 27.07–27.42 | 826–836% | 11.77–12.04 ms/frame |
| Advanced 320×240 | 22.62–22.71 | 923–927% | 11.48–11.57 ms/frame |
| Advanced 1920×1080 | 19.50 | 1021% | 11.42–11.44 ms/frame |
| Native pause | 16.28–16.35 | 1073–1077% | 11.6–11.8 ms/frame |

The SDL destination is an ordinary write-back RAM DISPLAY slot, not the GOP
aperture. The original path cleared 20.37 MB and then scaled/converted every pixel,
including coordinate division for matching-size canvases. It neither reads the
framebuffer nor performs cursor composition. The output rate was 1.69–1.78 GB/s;
three-pass logical traffic is not a measured DRAM bandwidth figure. Kernel
composition and the final WC scanout copy happen later, outside this stage.
The Intel flip backend still needs SDL's RAM-slot copy.

Advanced 320×240 workers spent about 105 user and 257–258 kernel CPU-ms/frame
inside raster-scene scopes; native gameplay spent about 261 user and 7 kernel.
These scopes include Mesa's per-tile mutex. This is a synchronization lead,
not proof that all worker CPU is shader work or that a particular syscall is
responsible. Mutex/condition and ADDRESS_WAIT/wake isolation precedes any fix.

Only 18 instrumented threads were accounted; omitted threads are not assumed idle.
Sampling overhead under contention is unqualified. Linux/Fedora comparisons use
LLVM 22, window-system GL and independent process samples, so they compare whole
stacks. Exact pause-page equivalence and loaded native frequencies were not
recorded. Source, commands, detailed stage tables and small evidence are in
`/shared/present/soh-frame-limiter/ALPHA-INVESTIGATION.md`.

## SDL fast path

Both SDL2 and SDL3 now copy matching-size canvases by row without coordinate
division. The RGB 16/8/0 path masks the spare byte to zero as DISPLAY requires;
other shifts retain RGB conversion. A completely overwritten viewport skips
clearing; borders and different-size nearest-neighbour scaling keep their pixels.
Submission, slot rotation and GL ownership/lifetimes are unchanged. This changes
no grants, ABI, worker defaults or compiler pin. Static consumers need relinking.

Validation used parent 63a044fa, userland 36d9bce5 and the published 8bdf136
compiler. All four ordinary/OSMesa SDL2/3 archives build. Both actual GL archives
complete native-sized → scaled/letterboxed → native-sized swaps and teardown in
4-vCPU/4-GiB nested KVM with invariant TSC passed through; renderer is llvmpipe.
1,041 expected-pixel cases per backend include RGB layouts, spare alpha, margins
and untouched row padding. Actual backend disassembly has a vector mask/copy
branch without pixel division for 1:1.

For the extracted pixel function alone, three 16-frame runs at 2880×1768 measured
7.65–7.68 ms/frame before and 1.61–1.67 ms/frame after on that nested guest.
This is not native SoH throughput. Native remeasurement remains pending.
Evidence: `/shared/present/soh-frame-limiter/sdl-present/`.
