# Native Go cross-toolchain

The [pyxis-go fork](https://git.internal/PyxisOS/pyxis-go) adds a static
`pyxis/amd64` target to Go 1.27.3. Its linker writes P1F directly; no converter,
libc or Linux syscall layer is involved. This first profile supports native
runtime bootstrap and diagnostic hello, with one P, normal multi-M/GC and
cooperative Go preemption. See the [runtime milestone](../wip/go-runtime.md)
and [qualification record](experiments/go-native-hello/README.md) for limits.

## Build and run

Use a Linux amd64 host, git and a Go bootstrap supported by Go 1.27.3. This is
separate from the LLVM compiler/container and ordinary image builds. Source is
fetched shallowly from the internal fork at the exact commit in
[`build-go.sh`](../../toolchain/build-go.sh); no upstream or module fallback:

```sh
GOMAXPROCS=8 GOROOT_BOOTSTRAP=/usr/lib/golang \
  toolchain/build-go.sh "$HOME/opt/pyxis-go" "$PWD/build/go-work"
toolchain/build-go-hello.sh "$HOME/opt/pyxis-go" "$PWD/build/gohello.pxb"
make -j16 image GO_HELLO="$PWD/build/gohello.pxb"
```

The prefix and source directory must be fresh. The installed GOROOT contains
its sources and Linux tools, full Go license and exact fork revision. The hello
bundle retains that license and provenance. `GO_HELLO` stages only the selected
bundle in the normal live archive; empty is the default. Rescue remains unchanged.
Run `gohello` from a live shell. Native memory/thread/clock/entropy authority must be
available to the bundle's declared memory, threads, clock and random grants;
there is no ambient replacement. No new compiler container is needed.

For another pure Go program that fits this profile:

```sh
GOOS=pyxis GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  "$HOME/opt/pyxis-go/bin/go" build -trimpath -o hello.pxe hello.go
```

Package it in a bundle with the same explicit grants as
[the hello manifest](../../toolchain/go-hello/manifest.json). The runtime owns
native startup/TLS; never link libc startup into it. Exported text entries become
Go environment strings, while capability entries stay native held values.

## Initial limits

The default available CPU count is one in this target. Parallel Ps remain a
separate qualification task. Native Ms use guarded stacks and FS blocks, and
only native completion permits their release; idle retired contexts are collected
before another M is created or at process exit. Reservation bookkeeping pages
remain until process exit and reuse vacant slots.

Go safe points provide cooperative preemption. A call-free loop can indefinitely
delay another goroutine or GC. Hardware faults terminate the process; explicit
panic/recover works. Only amd64/v1 without cgo, external linking, PIE or dynamic
dependencies is supported. Kernel x87/SSE preservation does not imply AVX or
FSGSBASE usability; the target gates those assumptions.

Runtime diagnostics use supplied console/pipe output. While holding runtime
locks or on a system stack they use try writes, can lose bytes under backpressure
and refuse blocking FILE output. General os.File, networking, hosted compilation,
profiling and a complete standard-library build are later work. Native entropy
failure is fatal; no predictable fallback is used. Missing wall time also fails
explicitly rather than inventing a clock.
