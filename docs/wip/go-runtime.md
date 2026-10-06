# Cross-compiled Go on Pyxis: initial investigation

Status: bounded source investigation and host compile/conversion probes,
2026-09-30. No Pyxis Go target or runtime is implemented, and no Go program was
run in the guest. The stages below are proposals, not implementation approval.
The goal is the standard Go compiler/runtime targeting native Pyxis, initially
without cgo. TinyGo, WebAssembly hosting and a replacement language runtime were
not investigated.

## Conclusion

This is a native runtime port with OS prerequisites, rather than a new compiler
flag or a libc-only port. The existing ELF-to-P1F converter already accepts the
two static Linux Go examples built here. Their Linux startup, TLS and syscall
contracts still make those files unusable as Pyxis applications.

For the normal threaded runtime, the largest gaps are shared-process threads and
synchronization, a usable thread-local storage contract, and private virtual-memory
reservation/commit semantics. Pyxis already has capability-based output, clocks, entropy, process
exit and image loading to adapt. LLVM migration is not a prerequisite: Go has
its own compiler, assembler and linker for this cgo-disabled slice.

Prefer native runtime foundations that also serve C/C++ and libuv, then a small
`GOOS=pyxis` backend. Do not emulate Linux syscalls in the kernel or disable the
GC/scheduler to make a printed line look like a working Go port. There is not
enough implementation evidence here for a credible elapsed-time estimate.

## Baseline and reproducible probes

- Pyxis: `3ed181115b9a737aaf2f002ce67817d1fb5441f1`
  (merged remote-terminal milestone).
- Userland: `5cbfbbd5189c2159e6059f4281bceb43af5d77e8`.
- Official Go tag: `go1.26.7`, commit
  `e3336a22ad3f0a90bd252c95d8b5544e02674205`.
- [Official source archive](https://go.dev/dl/go1.26.7.src.tar.gz), SHA-256
  `0ed24eac755105085b89fe9cabc2742b91a0ad7b94b59d3ad364918ebc8956ad`,
  matched against the [Go release manifest](https://go.dev/dl/?mode=json&include=all).
- Executed compiler: Fedora `golang{,-bin,-src}-1.26.7-1.fc44`, reporting
  `go1.26.7-X:nodwarf5 linux/amd64`. The inspected runtime/startup and linker/TLS
  source files were compared with the official archive. Probe sizes belong to
  this packaged compiler, not to a future Pyxis backend.

Create two separate source files in a temporary directory:

```go
// println.go
package main
func main() { println("Hello, Pyxis") }
```

```go
// fmt.go
package main
import "fmt"
func main() { fmt.Println("Hello, Pyxis") }
```

From the repository root, with `probe_dir` naming that directory:

```sh
GOTOOLCHAIN=local GOOS=pyxis GOARCH=amd64 CGO_ENABLED=0 \
  go build -o "$probe_dir/println.pyxis" "$probe_dir/println.go"

GOTOOLCHAIN=local GOOS=linux GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  go build -o "$probe_dir/println.linux" "$probe_dir/println.go"
GOTOOLCHAIN=local GOOS=linux GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  go build -o "$probe_dir/fmt.linux" "$probe_dir/fmt.go"

make -C tools elf2pxe
readelf -lW "$probe_dir/println.linux"
readelf -lW "$probe_dir/fmt.linux"
build/tools/elf2pxe --format p1f -o "$probe_dir/println.pxe" "$probe_dir/println.linux"
build/tools/elf2pxe --format p1f -o "$probe_dir/fmt.pxe" "$probe_dir/fmt.linux"

GOTOOLCHAIN=local GOOS=linux GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  go list -deps "$probe_dir/println.go"
GOTOOLCHAIN=local GOOS=linux GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  go list -deps "$probe_dir/fmt.go"

gdb -q -nx -batch -iex 'set auto-load off' \
  -ex 'set environment GOMAXPROCS 1' \
  -ex 'set environment GODEBUG asyncpreemptoff=1' \
  -ex 'break main.main' -ex run -ex 'info threads' -ex continue \
  "$probe_dir/println.linux"
```

Observed results:

| Probe | Result |
| --- | --- |
| `GOOS=pyxis GOARCH=amd64` build | Rejected: `go: unsupported GOOS/GOARCH pair pyxis/amd64`. |
| Linux builtin `println` | Built; 31 dependency-list entries including the main package. ELF 1,869,737 bytes; P1F 1,193,905 bytes. |
| Linux `fmt.Println` | Built; 62 dependency-list entries including the main package. ELF 2,458,955 bytes; P1F 1,580,977 bytes. |
| Both ELF layouts | Fixed-address x86-64 `ET_EXEC`; three page-aligned LOAD segments, RX/R/RW, no INTERP/DYNAMIC/TLS program headers. Converter succeeded unchanged. |
| Host debugger, builtin hello | With `GOMAXPROCS=1` and `GODEBUG=asyncpreemptoff=1`, stopped at `main.main` with three Linux threads: main, one in `runtime.usleep`, one in `runtime.futex`. Continued, printed the message and exited normally. This is one host observation, not a promised thread count. |

Dependency lists describe selected Linux packages, not a complete Pyxis porting
inventory or a count of executed initializers. Conversion establishes container
compatibility only. The probes did not execute either converted image in Pyxis,
rebuild Go, add a target, or measure runtime resident memory. A planned host
syscall trace was unavailable because `strace` is not installed; thread and memory
requirements below are source-derived, with the separate host GDB thread
observation above. No tests, boot automation or compiler container changes were added.

## What happens before hello world

The pinned [amd64 startup](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/asm_amd64.s)
and [runtime process initialization](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/proc.go)
establish the relevant sequence:

1. The Linux entry reads argc/argv from the initial stack. Pyxis passes a
   `startup_info` pointer in RDI instead. A native entry must consume that record,
   retain its granted resources and initialize Go's arguments/environment.
2. `rt0_go` establishes g0 and per-thread `g` storage. The Linux `settls` uses
   `arch_prctl(ARCH_SET_FS)`; it is not supplied by converting the ELF.
3. `schedinit` initializes locks, randomness, allocation, stacks and scheduler
   state. `procresize` creates Go execution resources before user main.
4. `runtime.main` starts an OS thread for `sysmon` on amd64. It enables GC,
   including sweeper/scavenger goroutines, and completes package initialization
   before calling `main.main`.
5. Builtin `println` uses runtime printing and normally writes to stderr. It
   bypasses `fmt` and libc, but does not bypass startup, allocation or scheduling.

`GOMAXPROCS=1` limits simultaneous Go execution through one P; it does not limit
the process to one OS thread (M). `haveSysmon` is false for wasm, not for amd64.
Disabling asynchronous preemption does not remove monitor-thread creation either.
Go's internal goroutine stacks are separate from the process's initial OS stack;
increasing Pyxis's existing 1 MiB initial stack does not solve these requirements.

Wasm/WASI has a deliberately single-threaded runtime path, demonstrating that
such a design is possible. Reusing that idea for amd64 would require deliberate
changes to scheduling, blocking calls, timers, locks and allocation, with an
explicitly restricted progress contract. It is a separate port strategy, not a
flag or a harmless stub for `newosproc`; this report does not recommend it as the
default route to the future Go application list.

## Native contract gaps

| Area | Existing Pyxis foundation | Work still required |
| --- | --- | --- |
| Target and output | amd64 CPU, static P1F loading, working ELF converter | Register Pyxis in Go's target/build-tag/generated GOOS tables, select native runtime files, and define linker/TLS behavior. Audit `internal/syslist`, `internal/platform`, `cmd/dist`, `cmd/internal/objabi`, amd64 assembler and linker switches. This is not a proved exhaustive patch list. Do not select Unix build tags merely to reuse Linux files. |
| Startup and calls | [startup ABI](../../include/abi/startup.h), [syscall ABI](../../include/abi/syscall.h) | Native Go assembly entry and syscall wrappers, preserving Go's ABI, stack-growth and runtime lock constraints. Early helpers must work before Go heap/scheduler initialization. A normal libc/libpyxis C call is not automatically a valid Go runtime call. |
| Threads and TLS | Per-task execution state and internal FS/GS-base preservation | User-visible thread creation/exit/join or equivalent lifecycle, stacks, TLS-base setup and shared-process cleanup. There is no public TLS setter; FSGSBASE is disabled. Kernel GS remains CPU-local. |
| Synchronization | Scheduler wait/wake machinery and object readiness | Thread-safe, lost-wakeup-safe parking/waking with deadlines and shutdown behavior. Existing `wait_many` observes selected capability objects and permits one active wait per process; it is not a user-word futex or a complete runtime semaphore API. Choose native semantics first. |
| Memory | [MEMORY_ALLOCATE/RELEASE](../../include/abi/memory.h): eager zeroed private pages and exact whole-region release | Reserve unbacked address ranges, map/commit subranges, alignment/address placement, reclamation and partial-region semantics, or an explicitly bounded alternative Go allocator backend. Kernel-internal VM facilities are not public user contracts. |
| Time and randomness | [clock](../../include/abi/clock.h) and [random](../../include/abi/random.h) capabilities | Early no-allocation adapters, required launch grants, units/deadlines, failure behavior and scheduling interaction. Do not copy Linux vDSO, auxv, `/proc` or `/dev/urandom` assumptions. |
| Output and exit | Dedicated startup streams, console/file/pipe protocols, native process exit | Runtime stderr diagnostics and process exit first; then standard-stream adoption, partial writes and errors for `os.File`. Grants remain authoritative; no ambient descriptor 1/2 authority. |
| Faults and preemption | Kernel preemption and user-fault termination | Decide synchronous fault-to-Go-panic delivery and runtime asynchronous preemption. Killing a faulting process is not recoverable Go nil-pointer panic behavior; scheduling a different kernel task is not Go goroutine preemption. POSIX signals are not a prerequisite interface choice. |

Monotonic time is needed during runtime initialization, before the print call.
Calendar time is not established as a requirement for builtin `println`; it
belongs to later standard-library consumers.

The thread work is substantial even if siblings initially share one CPU.
Today's [private-memory loans](../kernel/memory.md) rely on parking the only task
and leaving its private address space before the BSP mutates it. A second thread
invalidates that assumption. Capability-table operations, pending I/O buffers,
process exit and VM mutation must become safe with siblings before a runtime
thread may operate concurrently. Follow the
[thread direction](scheduling-and-threads.md#multiple-user-threads); Go does not require
space migration or multi-CPU sibling execution as the first functional result.

### Reservation is not physical allocation

The pinned [64-bit page allocator](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/mpagealloc_64bit.go)
reserves sparse summary and scavenge-index arrays during initialization, mapping
only needed portions. With the default amd64 constants, the five summary arrays
reserve 585.125 MiB and the scavenge index another 512 MiB. This is calculated
virtual address space, not measured resident RAM or Go's total memory footprint.
The calculation uses 48 heap-address bits, 4 MiB page-allocation chunks and
eight-byte summary/scavenge entries. Ordinary amd64 heap arenas are 64 MiB.

Implementing every `sysReserve` as today's eagerly backed MEMORY_ALLOCATE would
therefore fail on the normal 256 MiB guest before it becomes a useful allocator.
More RAM would hide the mismatch rather than establish reserve/commit semantics.
A bounded memory backend could choose different sizing, but merely copying the
WASM `mem_sbrk` backend does not change amd64 metadata reservations. Reservation,
commit, release and reclamation policy must be settled explicitly.

## From println to useful Go

`fmt.Println` adds `fmt`, `os`, reflection, synchronization, time and file/poll
support to the selected dependency graph. The Linux `os` implementation creates
standard `File` values from numeric descriptors and uses Unix descriptor/poll
helpers. Polling is conditional: blocking standard output does not itself demand
epoll, and a native backend should use Pyxis readiness where appropriate rather
than reproduce epoll/eventfd. Pyxis needs its own implementation that adopts the
supplied streams;
its existing C descriptor table is not automatically present in a cgo-disabled
Go executable. Small Go formatting programs need not wait for sockets or a
complete filesystem API, but their reachable initialization and error paths
must build and behave honestly.

Full file/network programs, `os/exec`, netpoll cancellation, `crypto/rand`, shared
libraries/cgo, race detection and profiling are later consumers. Unsupported
operations must return meaningful errors or stay outside the declared slice,
never claim successful I/O. Hosted `go`, module downloads and rebuilding Go
inside Pyxis are separate from cross-compiling an application on the host.

## Proposed bounded progression

1. **Settle native thread/TLS/parking contracts.** Coordinate with the existing
   scheduling plan and Neovim/libuv findings. Define sibling-safe VM/capability
   ownership and whole-process shutdown before implementing thread creation.
2. **Expose useful private-VM reservation and backing operations.** Specify
   authority, alignment, commitment, release and failure atomicity. Retain BSP
   ownership through a thread-safe handoff. This should serve native runtimes,
   not be a Go-shaped mmap compatibility layer.
3. **Build the minimal Pyxis Go target/runtime.** Pin a maintained Go fork or
   patch set; use `amd64/v1`, static executables and no cgo. Implement startup,
   TLS, OS threads, runtime synchronization/memory, time, entropy, diagnostics
   and exit. Preserve GC and the normal scheduler. Define fault/preemption
   limitations explicitly before calling this a supported runtime.
4. **Run builtin hello, then exercise runtime progress.** Print through inherited
   stderr, exit with an observable result and reclaim the process. Subsequently
   exercise allocation/GC, stack growth, channels, timers and blocking output
   with another goroutine progressing. A printed literal alone is insufficient
   evidence for the later applications.
5. **Add standard output through `fmt.Println`.** Support adopted native streams
   and their errors/closure without changing the application's output code.
   Files and networking then get separate concrete consumers before attempting
   pup or a larger service.

Before implementation, decide whether to pursue these prerequisites now, the
exact first runtime support profile (especially faults and preemption), and
repository/container ownership. Nothing here requests a compiler-image rebuild.

The upstream [porting policy](https://go.dev/wiki/PortingPolicy) concerns admission
and maintenance in Go's own repository, not permission to develop a local port.
The [source build guide](https://go.dev/doc/install/source) describes bootstrapping
the host tools. Neither is evidence that Pyxis already satisfies the runtime.
