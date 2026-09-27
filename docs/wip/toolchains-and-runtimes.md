# Hosted toolchains and language runtimes

Status: future directions, not an implementation milestone or a fixed sequence.
The completed [libc portability milestone](../libc-portability.md) supports
cksum and restricted tee. Promote one result below into a separate milestone
after a pinned build probe and discussion of missing OS contracts.

## Distinct results

The existing [GCC/binutils toolchain](../../toolchain/README.md) runs on the host
and targets Pyxis. TCC already provides a native [edit/build/run loop](../edit-build-run.md).
Keep three larger achievements separate:

1. A host-running compiler produces programs that run on Pyxis.
2. A compiler and its tools run on Pyxis and build a selected application there.
3. Pyxis rebuilds those tools, eventually supporting larger self-hosted builds.

Cross-building the tools can establish the second result before their build
systems run in the guest. Compiler drivers still need real file, process-launch,
completion and diagnostic behavior on Pyxis. Building the compiler itself adds
its build tools and dependencies; it is a later completion point.

## Binutils, P1F and a hosted C compiler

Start a future investigation with a pinned binutils subset: assembler, linker
and the archive tools actually needed to assemble and link a small program in
Pyxis. Probe libc, path handling, temporary files and build dependencies rather
than assuming that mostly C source means a trivial port.

Current binutils uses ELF objects/executables and ordinary ar archives; elf2pxe
converts the linked result to P1F/PXE. Investigate teaching binutils/linking tools
about native executable output, but compare that with retaining the converter.
P1F is an executable image, not today's relocatable object or symbol-bearing
library format. Supporting it does not require inventing replacements for ELF
object files and static archives. Decide relocation, layout and diagnostic needs
before proposing a BFD backend or changing the format.

GCC is a natural hosted candidate because the Pyxis target integration already
exists. It is not a C-only project: current upstream
[GCC prerequisites](https://gcc.gnu.org/install/prerequisites.html) require a
C++14 compiler, while [LLVM](https://llvm.org/docs/CMake.html) requires C++17.
Both need investigation of their actual target runtime dependencies.

Keep GCC versus Clang/LLVM open until comparable pinned probes establish the
smallest useful hosted configuration, runtime/library gaps, memory requirements
and maintenance cost. Existing GNU work gives GCC a head start; that is a
reason to investigate it, not a measured claim that it will be easier. A first
hosted compiler should compile and run a small utility, without also requiring
its own bootstrap, every language frontend or a complete OS build.

## C++ in userspace

Cross-compiled C++ applications can precede a hosted C++ compiler. Start with an
explicit runtime subset and a small real consumer, then expand toward ports
such as DevilutionX and other C++ libraries. This does not change the kernel's
language or authorize reviving the kernel C++ experiment.

Probe startup/destruction, allocation, ABI support and the chosen standard
library. Decide exceptions/unwinding, RTTI, local-static initialization, TLS and
thread support explicitly; do not advertise a complete hosted C++ environment
because one freestanding example links. Standard-library coverage and unsupported
features must be visible to downstream ports. Compiler/container changes remain
separate from ordinary SDK changes.

## Go cross compiler, then hosted Go toolchain

The first Go result would be a host-running toolchain targeting Pyxis plus enough
native runtime support to execute selected Go programs. A target name or binary
conversion alone is insufficient. Investigate memory/GC requirements, execution
threads and synchronization, timers, filesystem and network integration through
Pyxis primitives. A larger libc does not automatically provide Go's OS port.
Use [Go's porting guidance](https://go.dev/wiki/PortingPolicy) as an investigation
entry point, not a claim of supported GOOS/GOARCH combinations for Pyxis.

Split that future effort into concrete runtime consumers: a basic executable,
then goroutines/channels and timing, then selected file and network programs as
the necessary contracts become available. These are candidate boundaries to
refine after the probe, not requirements for this libc milestone.

A **hosted Go toolchain** is a later result: run the compiler and go command in
Pyxis, compile a small program, then examine the wider package/build workflow.
Process launch, build cache, module acquisition and dependency tooling deserve
their own scope. Running Go applications and hosting Go development tools are
both valuable independently of the distant Tailscale idea.

## Rust without choosing the C compiler around it

LLVM makes Rust worth keeping on the list, but running LLVM tools inside Pyxis
is not a prerequisite for cross-compiling Rust applications to it. A pinned
host compiler can use a [custom target](https://doc.rust-lang.org/rustc/targets/custom.html).
Target/ABI integration, allocation and runtime support, and eventually a Pyxis
standard-library port remain real work. A small no_std program is a different
completion point from general std-based applications or a hosted Rust toolchain.

Keep this as a separate future investigation. Choosing GNU for initial hosted C
development would not prevent a later LLVM-based Rust cross toolchain.

## Distant application ideas

- **Wild possible future idea: Tailscale.** Access the owner's tailnet/homelab
  from Pyxis. This follows a usable Go runtime and a concrete networking/security
  probe. Upstream [userspace networking mode](https://tailscale.com/docs/concepts/userspace-networking)
  offers a proxy-oriented investigation without first requiring a TUN interface;
  full OS routing/interface integration is a separate goal. This is a candidate,
  not a promise that the existing socket or capability contracts suffice.
- **Wilder even later idea: Ladybird.** A full graphical browser after substantial
  userspace runtime, graphics and application infrastructure. Probe the selected
  [upstream revision](https://github.com/LadybirdBrowser/ladybird) and its language,
  library, process/IPC, sandbox and rendering requirements before defining a port.
  Desktop and eventual GPU work may help; do not make accelerated rendering a
  verified prerequisite without investigation. This is deliberately distant.

Neither application drives immediate kernel abstractions. The next useful step
remains a bounded library/tool consumer, not a speculative compatibility layer
for the entire future port list.
