# Mesa software OpenGL library

The `mesa` port builds Mesa 26.2.5 as a static `libOSMesa.a`, with the OSMesa
context/render frontend restored from 25.0.7. Sources and signatures are pinned
in the [source inventory](#source-cache). Upstream MIT and
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
game comparison is recorded in the
[llvmpipe qualification](experiments/llvmpipe.md#native-vela-complete-stack-comparison-2026-10-28).

## Source cache

The native port retains Mesa 26.2.5 and the OSMesa frontend from 25.0.7. Sources
are fetched through checked internal mirrors; the LLVM development libraries
come from the compiler container's matching fork pin, not an image-time LLVM
build. The combined ioquake3/OpenArena consumer uses these pins:

| Input | Upstream | Cache | SHA-256 or commit |
| --- | --- | --- | --- |
| Mesa 26.2.5 | `https://archive.mesa3d.org/mesa-26.2.5.tar.xz` | `https://repo.internal/repository/raw-mesa/mesa-26.2.5.tar.xz` | `794b3d1729075dae7ebd47c7283b7ca0cdb5dec668f7f11cc0a5d82f6138a0a5` |
| Mesa 25.0.7 | `https://archive.mesa3d.org/mesa-25.0.7.tar.xz` | `https://repo.internal/repository/raw-mesa/mesa-25.0.7.tar.xz` | `592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6` |
| OpenArena 0.8.8 data | `https://download.tuxfamily.org/openarena/rel/088/openarena-0.8.8.zip` | `https://repo.internal/repository/raw-tuxfamily/openarena/rel/088/openarena-0.8.8.zip` | `5a8faf7f5b51f351b0a1618c06b6b98a5f1a6758f1d39818de2c87df2a0bac4a` |
| ioquake3 | `https://github.com/ioquake/ioq3.git` | `https://git.internal/mirrors/ioq3` | `87a99d18206e878a20289f05ac3c16005dcae5c3` |

On 2026-10-23, the Mesa detached signatures verified against release maintainer
Eric Engestrom's RSA key, fingerprint
`5755 1DE1 5B96 8F63 41C2 48F6 8D8E 31AF C324 28A6`. The key appears in Mesa's
tagged `docs/release-maintainers-keys.asc` and Debian's upstream signing-key
inventory. Recipes check archive SHA-256; the signature/provenance check occurred
when the cache was admitted. This does not establish a web-of-trust endorsement.
OpenArena provides an MD5 but no signed release or upstream SHA-2; its cached
SHA-256 is recorded trust on first use. ioquake3's exact commit is the pin.

Runtime limits and future work are separate from delivery: native game comparison
is recorded in [llvmpipe](experiments/llvmpipe.md),
[OpenArena](../userland/openarena.md) owns packaging/input behavior, and
[renderer/engine extensions](https://git.internal/PyxisOS/pyxis-os/issues/1698) need their own decisions.
