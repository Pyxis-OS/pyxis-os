# Hosted toolchains and language runtimes

Status: LLVM/Clang is the chosen toolchain direction as of 2026-09-29, including
Clang as the first large hosted C toolchain. Since the
[host milestone](../development/llvm-toolchain.md), Pyxis builds with its Clang and LLD on the
host and GCC/binutils are retired; no hosted LLVM support is implemented. The boundaries
below guide a pinned investigation, not an implementation assignment or a fixed
schedule. Other language runtimes remain future candidates.

## Distinct results

The [LLVM toolchain](../../toolchain/README.md) runs on the host and targets
Pyxis. TCC already provides a native [edit/build/run loop](../development/edit-build-run.md).
Keep three larger achievements separate:

1. A host-running compiler produces programs that run on Pyxis.
2. A compiler and its tools run on Pyxis and build a selected application there.
3. Pyxis rebuilds those tools, eventually supporting larger self-hosted builds.

Cross-building the tools can establish the second result before their build
systems run in the guest. Compiler drivers still need real file, process-launch,
completion and diagnostic behavior on Pyxis. Building the compiler itself adds
its build tools and dependencies; it is a later completion point.

## LLVM/Clang transition and hosting

LLVM/Clang replaces the earlier open GCC-versus-LLVM choice. The intended
toolchain includes Clang, LLD and the LLVM archive/object tools needed by Pyxis.
This supports the owner's language experiments as well as a later Rust direction;
it is not a measured claim that LLVM will be the easiest compiler to host.

Keep three implementation milestones separate, refining their scope after a
pinned probe:

1. **Host-running LLVM toolchain.** Completed 2026-10-07:
   [LLVM toolchain on the host](../development/llvm-toolchain.md). The `pyxis-llvm`
   fork carries the Pyxis target and driver contract, compiler-rt builtins replace
   libgcc, and the owner-built container supplies the toolchain to CI.
2. **Native C++ and OS prerequisites.** Clang is a C++ application even when it
   compiles C. The userspace part completed on 2026-10-08:
   [C++ in userspace](../development/cxx-userspace.md) puts libc++, libc++abi and
   libunwind in the SDK, without threads or localization. Still open: the further
   runtime/library subset Clang requires, and useful native file, process,
   memory, synchronization and thread contracts, established through bounded
   consumers. The [shared-process thread investigation](threads.md) records VM,
   lifetime, TLS and libc/C++ prerequisites and proposed native contracts;
   implementation remains unassigned. Do not bury missing OS behavior in
   compiler-specific stubs.
3. **Clang hosted on Pyxis.** Cross-build the selected compiler, linker and tools
   to run in the guest. First completion: compile, link and run one small C program
   entirely inside Pyxis. Rebuilding LLVM itself in the guest is a later result,
   with its own build tools, resource requirements and dependencies.

Objects and static archives stay ELF. Since milestone 1, LLD writes
[P1F executables](../development/llvm-toolchain.md#p1f-output) directly and
`elf2pxe` is gone, so hosted Clang links PXE without a converter, relocatable
format or loader change. Pyxis keeps one default toolchain.

The [Clang toolchain guide](https://clang.llvm.org/docs/Toolchain.html) separates
compiler, assembler, linker and runtime pieces. Cross-compilation and LLVM hosting
have different requirements: a host-running compiler targeting Pyxis does not
prove that LLVM's own OS-facing support library can run there.

A bounded LLVM hosting requirements probe, like the
[Neovim/libuv investigation](neovim-libuv.md), should record missing native contracts,
dependency/runtime gaps and measured resource needs. Select one implementation
milestone afterward rather than starting all prerequisites concurrently.

The owner's approximately 1,600-line, self-hosted C-subset compiler is a possible
later consumer for a guest compiler-to-compiler workflow. Its requirements and
reproduction steps have not been inspected; it is not a prerequisite or a promise
that it builds unchanged. TCC retains its existing small native development role.

## C++ in userspace

Completed 2026-10-08: [C++ in userspace](../development/cxx-userspace.md).
Cross-compiled C++ programs run with libc++, libc++abi and libunwind from the
SDK, and fmt is the first C++ port. Its [accepted limits](../technical-debt.md#c-runtime-subset)
name threads, localization and `<cmath>` as the next runtime additions, each
when a selected port such as DevilutionX or the Clang hosting milestone needs it.

## Go cross compiler, then hosted Go toolchain

The [initial Go runtime investigation](go-runtime.md) pins Go 1.26.7 and records
host compile/conversion probes plus the native contract gaps. No Go executable
has run on Pyxis. The key prerequisites are shared-process threads/TLS and
parking, plus private-memory reservation/backing; LLVM is not required for the
cgo-disabled first slice. Its proposed steps are not an implementation milestone.

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
both valuable independently of the future Tailscale target below.

One very distant illustrative progression is Go runtime, age and small Go
utilities, fzf and esbuild, gopls, CoreDNS and Caddy, rclone, restic and
Syncthing, Tailscale, then Forgejo. This is a way to record possible reach, not
a prerequisite graph or fixed implementation order. Hosted Go development is
also a separate result from cross-compiling Go programs on the host for Pyxis.

## Rust alongside the LLVM direction

LLVM makes Rust worth keeping on the list, but running LLVM tools inside Pyxis
is not a prerequisite for cross-compiling Rust applications to it. A pinned
host compiler can use a [custom target](https://doc.rust-lang.org/rustc/targets/custom.html).
Target/ABI integration, allocation and runtime support, and eventually a Pyxis
standard-library port remain real work. A small no_std program is a different
completion point from general std-based applications or a hosted Rust toolchain.

Keep this as a separate future investigation. Choosing LLVM does not provide
Rust's OS integration automatically, and the selected Clang release need not be
the LLVM version required by a future pinned rustc. Cross-compiled no_std, std
support and a hosted Rust toolchain are separate completion points.

## Homelab administration over Tailscale

Agreed future usability target: boot Pyxis, join the owner's tailnet and administer
existing machines through Tailscale SSH, without changing their server setup.
Those machines run Tailscale SSH rather than a separate `sshd`. This records a
target, not an implementation milestone or a change to current priorities.

[Tailscale SSH](https://tailscale.com/docs/features/tailscale-ssh) accepts ordinary
SSH clients over the tailnet; Tailscale supplies the remote server and applies
tailnet identity/policy. Pyxis therefore needs tailnet connectivity and an SSH
client, not its own SSH server for this workflow.

The first complete workflow should retain device identity across reboot, resolve
and connect to tailnet machines, and provide interactive sessions with working
control keys, terminal dimensions and escape sequences. Authentication checks
and disconnects need clear handling. Multiple sessions can later use the proposed
[terminal multiplexer](terminal-applications.md).

Probe Tailscale's Go runtime, networking, timers, cryptography and persistent
credential requirements before choosing port tasks. Cross-compiling the client
is sufficient initially; hosting the Go compiler is not a prerequisite. Upstream
[userspace networking mode](https://tailscale.com/docs/concepts/userspace-networking)
offers a SOCKS5/proxy path to investigate for outbound SSH before requiring a
native TUN interface or full OS routing integration. Its fit with Pyxis and the
chosen SSH client remains unverified; terminal and client-library gaps also need
their own bounded investigation.

## Distant application ideas

- **Wilder even later idea: Ladybird.** A full graphical browser after substantial
  userspace runtime, graphics and application infrastructure. Probe the selected
  [upstream revision](https://github.com/LadybirdBrowser/ladybird) and its language,
  library, process/IPC, sandbox and rendering requirements before defining a port.
  Desktop and eventual GPU work may help; do not make accelerated rendering a
  verified prerequisite without investigation. This is deliberately distant.

Neither target drives immediate kernel abstractions. The next useful step
remains a bounded library/tool consumer, not a speculative compatibility layer
for the entire future port list.
