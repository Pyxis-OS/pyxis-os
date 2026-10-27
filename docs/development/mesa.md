# Mesa software OpenGL library

The `mesa` port builds Mesa 26.2.5 as a static `libOSMesa.a`, with the OSMesa
context/render frontend restored from 25.0.7. Sources and signatures are pinned
in the [Quake 3 plan](../wip/quake3-llvmpipe.md#source-cache). Upstream MIT and
per-file terms, original reached-source/header notices, licence texts and
provenance accompany the development prefix.

The selected profile is desktop OpenGL through llvmpipe and softpipe, with no
GPU, GLX/EGL/GBM, GLES, shared objects or disk shader cache. LLVM MCJIT uses the
native JIT allocation/protection interface; only a space opting into JIT can
satisfy the optional bundle grant. Without it the normal selection uses softpipe;
an explicitly requested llvmpipe refuses. Mesa retains upstream worker defaults
and its CPUID/OSXSAVE/XCR0-gated AVX2 selection. This is not formal OpenGL
conformance qualification. `OSMesaPostprocess` is absent from the header/library
and its procedure lookup returns NULL.

Native integration uses libc C11 synchronization and monotonic clock/sleep,
including native pthread condition attributes for monotonic waits. Pyxis is not
identified as POSIX. The program must declare memory, threads and clock authority;
clock failure terminates rather than inventing time or successful sleep. CPU
availability comes from the process's usable CPU allowance via libc sysconf;
Mesa applies its upstream worker cap without a platform override. Physical/available-memory queries stay unknown, and the
memory-info capability is not advertised. DRM descriptor sharing, anonymous
shared files and automatic PID-based logging are unavailable; use an explicit
native `MESA_LOG_FILE` for logs.

## Build and manual check

Use the current SDK/compiler container and the ports runner. The container
supplies target LLVM libraries through its prefixed `llvm-config`; image/CI builds
consume them without building LLVM:

```sh
lua build.lua mesa --sdk /absolute/path/to/pyxis/build/sdk --work /absolute/path/to/mesa-work
```

Host dependencies include Meson 1.4+, Ninja, Python Mako/PyYAML/packaging, flex,
bison and pkg-config. Builds are offline after the checked mirror download.
`stage/dev` contains the headers, archive, `osmesa.pc` and notices. Debug
sections are removed only from the staged archive. The parent target is
`make -f scripts/ports.mk mesa` after SDK export; SDL builds also require it. Link C clients
with the SDK C++ link closure, since Mesa includes C++ compiler code. The `.pc`
uses `sdk://` and a private `libcxx` dependency. `mesa-dev` installs headers,
archive and metadata into that view, with licences/notices/provenance under
`pkg://mesa-dev/`. It depends on `libcxx-dev`; no GL application joins the archive.

## Front and back buffers

The port-local extension accepts `OSMESA_DOUBLEBUFFER` (`0x38`) with
`GL_TRUE`/`GL_FALSE` in `OSMesaCreateContextAttribs`; the upstream default stays
front-only. Double buffering adds real `FRONT_LEFT` and `BACK_LEFT` resources,
reports `GL_DOUBLEBUFFER=true` and supports ordinary `GL_BACK` drawing/reading.
`OSMesaSwapBuffers(ctx)` requires that context current on the caller's thread.
It finishes rendering, exchanges color resources, invalidates framebuffer
attachments, and copies the new front into the caller-owned CPU buffer using
the selected OSMesa pixel layout. Back contents after swap are undefined.
Front-only swap finishes/copies without exchange. Failure returns `GL_FALSE`;
resources may already have exchanged, so callers must not claim presentation.
MakeCurrent checks allocation/binding and preserves the previous binding on
failure. The [SDL profile](sdl2.md#opengl-through-osmesa) owns its CPU canvas and
native presentation separately.

The opt-in manual bundle is `stage/manual/mesa-triangle.pxb`. Copy it into an
ordinary writable root, then run it with an output path in that root:

```text
home://mesa-triangle.pxb home://triangle.ppm
```

It renders a 256×256 colour triangle, reports the selected renderer, checks
background/centre pixels and the unavailable postprocess lookup, then writes a
PPM. It has no display grant and declares optional JIT. See the original
[off-screen qualification](experiments/mesa-softpipe.md) and
[SDL/swap qualification](experiments/sdl2-osmesa.md). These original records cover softpipe. Current llvmpipe QEMU triangle checks
cover default/zero/four workers, absent/forced JIT and AVX-off selection; native
game comparison remains pending.
