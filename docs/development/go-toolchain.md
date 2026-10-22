# Native Go cross-toolchain

The [pyxis-go fork](https://git.internal/PyxisOS/pyxis-go) adds a static
`pyxis/amd64` target to Go 1.27.3. Its linker writes P1F directly; no converter,
libc or Linux syscall layer is involved. This first profile supports native
runtime bootstrap, explicit parallel Ps, native files/streams, fmt and time,
with normal multi-M/GC and cooperative Go preemption. See the [runtime milestone](../wip/go-runtime.md)
and the [startup](experiments/go-native-hello/README.md) and
[parallel](experiments/go-parallel/README.md) and
[files/time](experiments/go-files-time/README.md) and
[readiness/capacity](experiments/go-readiness-set/README.md) records for limits.

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
available to the bundle's declared memory, threads, clock and random grants.
The clock requires `read`, `precise` and `sleep`: the runtime checks all three at
startup, then uses precise monotonic/UTC reads for timers and `time.Now`. Missing
authority fails startup; there is no coarse or ambient replacement. No new compiler container is needed.

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

The target defaults to one P and `runtime.NumCPU()` reports one; there is no
automatic native CPU-count discovery. An exported `GOMAXPROCS=4` or `8`, or
`runtime.GOMAXPROCS(n)`, enables parallel Ps independently. These settings and
dynamic changes are qualified on four/eight-vCPU nested guests. Native thread
admission still includes sysmon and blocked Ms in addition to active Ps. Required
thread/memory exhaustion reports the native operation/status and terminates the
process; the runtime does not promise a P count beyond native admission.
Native Ms use guarded stacks and FS blocks, and
only native completion permits their release; idle retired contexts are collected
before another M is created or at process exit. Reservation bookkeeping pages
remain until process exit and reuse vacant slots.

Go safe points provide cooperative preemption. A call-free loop can indefinitely
delay another goroutine or GC. Hardware faults terminate the process; explicit
panic/recover works. Only amd64/v1 without cgo, external linking, PIE or dynamic
dependencies is supported. Kernel x87/SSE preservation does not imply AVX or
FSGSBASE usability; the target gates those assumptions.

Runtime diagnostics use blocking writes to the supplied console, pipe or FILE,
including under debuglock. Native calls carry up to 4096 bytes (4080 data bytes
for inline FILE writes); backpressure retains that P and can stall a one-P
program. Networking, hosted compilation, profiling and a complete standard-library
build remain later work. Entropy retries QUEUE_FULL within one five-second deadline;
other failure is fatal, with no predictable fallback. Before g/m, bootstrap
failures report a typed terminal startup result; the launching parent names its
step/status even without usable stdio. This needs no FS, heap, lock or output
grant. Once g/m exist, fatal native
hooks report the operation/status and throw a traceback. Missing wall time fails
explicitly rather than inventing a clock. Scavenging DECOMMIT metadata exhaustion
retains and clears the affected pages; it preserves zeroed reuse but can retain
RAM until later use or whole-identity release.

## Native files, streams and time

`os.File` owns an actual native handle, exposed as a borrow by `Fd`; `NewFile`
adopts one on success. Resolve `name://` against held environment/package roots,
or relative paths against the retained cwd directory chain. There is no ambient
slash root. Standard streams preserve their supplied interfaces and grants.
FILE-backed output shares confirmed positions with runtime diagnostics; independent
opens remain independent. Supplied stdout/stderr append companions select
non-atomic SIZE-before-WRITE behavior for ordinary output and diagnostics.
Malformed companions and unsupported streams fail I/O without stopping package
initialization; duplicate adoption fails. Native metadata validity is visible in `FileInfo.Sys()`
(`syscall.Stat_t.Valid`), without invented Unix permissions or timestamps.

Open/create, read/write, positioned I/O, seek, truncate, sync, enumeration and
native directory mutations work where granted. `O_APPEND`, `os.Root`, exported
providers and process execution fail explicitly in this slice. A native FILE
value received through a supplied pipe is read to EOF before the next segment;
exported FILE values fail before provider I/O and stay owned until close.

Ordinary pipe/console waits park goroutines through one lazy native
[readiness set](../interfaces/readiness-sets.md). Creation needs no grant; add
`pipe/create` only for programs needing `os.Pipe`. Imports, immediately successful
I/O and timer-only programs create no set or wake pipe. Dynamic descriptor
metadata replaces the 31-registration table; kernel metadata across all sets of
a process is bounded by 16 MiB, with explicit resource refusal. Ready replies
contain at most 64 records, independently of the number registered. Close wakes
Go waiters and joins native unregister before closing/reusing the handle.
Recoverable console input-loss or keyboard-unavailability events wake a native
read recheck without permanently poisoning the Go descriptor. Acknowledging loss
and restoring input works on the same stdin handle.
Many idle streams do not each require a native M. Regular FILE calls release
the P during native I/O; they have no readiness or deadline facility.

Go's initial native-M limit is 256, matching the kernel's total live/retiring
task capacity. Its table holds 256 noninitial contexts; the initial M is separate,
so the native total permits at most 255 siblings. A definitive CREATE LIMIT
without an observer can wait for a known retiring peer's actual completion and
retry. No live context, uncertain CREATE or backing is reused; genuine capacity
or allocation exhaustion still fails explicitly. The libc wrapper's independent
64-context bound is unchanged. The poller retains its geometric index and
reusable descriptor metadata high-water backing until process exit.

Timers use monotonic time. `syscall.ClockWallNow()` reports native wall-time
availability; `time.Now()` fails explicitly when unavailable. `time.Local` reads
the session-configured `TZ` and loads the archive's `boot://share/zoneinfo/<name>`
through the supplied boot grant. A bad/missing named zone fails explicitly.
Absent/empty `TZ` retains the existing UTC default. `time.LoadLocation` uses the
same archive; there is no embedded copy or host timezone-path fallback.

The readiness and thread-sizing prerequisites are qualified on one/four-vCPU
nested QEMU; native qualification and Go sockets remain open. The existing TCP
connection-pool limit must be addressed by networking task 4.
