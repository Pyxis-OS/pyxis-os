# Mesa softpipe library

The `mesa` port builds Mesa 26.2.5 as a static `libOSMesa.a`, with the OSMesa
context/render frontend restored from 25.0.7. Sources and signatures are pinned
in the [Quake 3 plan](../wip/quake3-llvmpipe.md#source-cache). Upstream MIT and
per-file terms, original reached-source/header notices, licence texts and
provenance accompany the development prefix.

The selected profile is desktop OpenGL through softpipe, no LLVM/JIT, GPU,
GLX/EGL/GBM, GLES, shared objects or disk shader cache. `OSMesaPostprocess` is
absent from the header/library and its procedure lookup returns NULL. The
optional SSE runtime vertex translator is also disabled: softpipe uses the
existing C translator. Compiled SIMD operations remain available when the CPU
supports them; executable allocation is unavailable. This is not a formal OpenGL
conformance qualification.

Native integration uses libc C11 synchronization and monotonic clock/sleep,
including native pthread condition attributes for monotonic waits. Pyxis is not
identified as POSIX. The program must declare memory, threads and clock authority;
clock failure terminates rather than inventing time or successful sleep. CPU
availability stays at Mesa's conservative one-CPU fallback, without claiming a
caller affinity count. Physical/available-memory queries stay unknown, and the
memory-info capability is not advertised. DRM descriptor sharing, anonymous
shared files and automatic PID-based logging are unavailable; use an explicit
native `MESA_LOG_FILE` for logs.

## Build and manual check

Use the current SDK/compiler and the ports runner:

```sh
lua build.lua mesa --sdk /absolute/path/to/pyxis/build/sdk --work /absolute/path/to/mesa-work
```

Host dependencies include Meson 1.4+, Ninja, Python Mako/PyYAML/packaging, flex,
bison and pkg-config. Builds are offline after the checked mirror download.
`stage/dev` contains the headers, archive, `osmesa.pc` and notices. Debug
sections are removed only from the staged archive. The opt-in parent target is
`make -f scripts/ports.mk mesa` after SDK export; ordinary images do not build it. Link C clients
with the SDK C++ link closure, since Mesa includes C++ compiler code. The `.pc`
uses `sdk://` and a private `libcxx` dependency for a future installed SDK view;
this task adds no package or archive application.

The opt-in manual bundle is `stage/manual/mesa-triangle.pxb`. Copy it into an
ordinary writable root, then run it with an output path in that root:

```text
home://mesa-triangle.pxb home://triangle.ppm
```

It renders a 256×256 colour triangle, reports `MESA_RENDERER softpipe`, checks
background/centre pixels and the unavailable postprocess lookup, then writes a
PPM. It has no display or JIT grant. SDL2 OpenGL presentation/resize/input is the
next slice; no game, native-machine rendering or llvmpipe is qualified here.
See [qualification](experiments/mesa-softpipe.md).
