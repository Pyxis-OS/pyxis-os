# Hosted toolchains and language runtimes

Status: LLVM/Clang is the chosen toolchain direction as of 2026-09-29, including
Clang as the first large hosted C toolchain. The current build still uses
GCC/binutils; no migration or hosted LLVM support is implemented. The boundaries
below guide a pinned investigation, not an implementation assignment or a fixed
schedule. Other language runtimes remain future candidates.

## Distinct results

The existing [GCC/binutils toolchain](../../toolchain/README.md) runs on the host
and targets Pyxis. TCC already provides a native [edit/build/run loop](../development/edit-build-run.md).
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

1. **Host-running LLVM toolchain.** Establish the Pyxis target/driver contract,
   SDK discovery, startup and link defaults, predefined macros and compiler
   runtime helpers. Probe compiler-rt builtins as the replacement for libgcc.
   Preserve the existing ABI, kernel register restrictions and userspace CPU-state
   assumptions. Validate kernel, SDK, userspace and ports builds and ordinary boots
   before replacing the normal GCC/binutils build. Identify the exact owner-built
   container update; ordinary CI must consume it rather than rebuild LLVM.
2. **Native C++ and OS prerequisites.** Clang is a C++ application even when it
   compiles C. Determine the runtime/library subset it actually requires and
   investigate libc++, libc++abi and unwinding needs explicitly. Establish useful
   native file, process, memory, synchronization and thread contracts through
   bounded consumers. Do not bury missing OS behavior in compiler-specific stubs.
3. **Clang hosted on Pyxis.** Cross-build the selected compiler, linker and tools
   to run in the guest. First completion: compile, link and run one small C program
   entirely inside Pyxis. Rebuilding LLVM itself in the guest is a later result,
   with its own build tools, resource requirements and dependencies.

Retain ELF objects, static archives and the ELF-to-P1F/PXE conversion initially.
The hosted workflow must include a usable converter or equivalent explicit final
step. Native P1F linker output can be investigated later; this transition does not
require inventing a relocatable format or rewriting the loader. Keep the working
toolchain available during validation, without committing to maintaining two
permanent default toolchains.

The [Clang toolchain guide](https://clang.llvm.org/docs/Toolchain.html) separates
compiler, assembler, linker and runtime pieces. Cross-compilation and LLVM hosting
have different requirements: a host-running compiler targeting Pyxis does not
prove that LLVM's own OS-facing support library can run there.

The [planning agenda](storage-and-terminal-agenda.md) pairs a bounded LLVM hosting
requirements probe with Neovim/libuv inspection. Record missing native contracts,
dependency/runtime gaps and measured resource needs. Select one implementation
milestone afterward rather than starting all prerequisites concurrently.

The owner's approximately 1,600-line, self-hosted C-subset compiler is a possible
later consumer for a guest compiler-to-compiler workflow. Its requirements and
reproduction steps have not been inspected; it is not a prerequisite or a promise
that it builds unchanged. TCC retains its existing small native development role.

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
[terminal multiplexer](storage-and-terminal-agenda.md).

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
