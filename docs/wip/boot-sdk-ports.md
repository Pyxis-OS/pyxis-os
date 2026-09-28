# Development milestone index

Status: working discussion after the first-shell milestone. These documents
separate the next concrete results from parked ideas; they do not authorize
implementation. The initial scope decisions are recorded; resolve each
milestone's remaining interface details before starting its code work.

## Suggested focus order

1. [Init and primitive scripts](../init.md) — complete: selected native or
   shebang init and explicit handoff to an interactive shell.
2. [SDK and repository separation](../sdk-and-repositories.md) — complete:
   exported SDK, prebuilt Pyxis compiler and pinned userspace submodule with
   the integrated build preserved.
3. [Port recipes and Kilo](../ports.md) — complete: pinned host Lua recipes,
   SDK-based Kilo build and ordinary boot-archive integration. The
   [edit/build/run workflow](../edit-build-run.md) with TCC is also complete.
4. [Filesystem mutations and Doom saves](../filesystem-mutations.md) — complete.
5. TTY horizontal tabs — complete: eight-column stops, clamped at the right edge,
   without erasing cells or wrapping. See [terminal controls](../terminal.md#tty-output-controls).
6. [UTC wall-clock and calendar conversion](../wall-clock.md) — complete:
   ISO date display, independent monotonic deadlines and TCC time features.
7. [Boot archive assembly](../boot-archive.md) — complete: install trees, Lua
   manifest and [independent build bundles](../build-bundles.md).
   [Automatic artifact selection](build-artifact-reuse.md) remains follow-up work.
8. Complete: [zoneinfo-backed local time](../timezones.md), including the full
   pinned database, UTC for absent/empty `TZ`, named zones and `date -u`.
   Locale and reverse conversion remain deferred.
9. Complete: [guest Lua](../lua.md), including scripts, REPL and default
   [session configuration](../session-configuration.md) for timezone and tab width.
10. Complete: [PCI and VirtIO host filesystem access](../virtio-fs.md).
    Init opens the opt-in export and delegates `host://` through the
    session launcher to the shell and children. Existing `ls`/`cat` use native
    directory/file capabilities; archive-only boot remains the default without
    a device/socket.
11. Complete: [initial networking](../networking.md), with loopback and virtio-net,
    manual IPv4 configuration and native ping. DHCP follows later through the
    same configuration interface; TCP and website hosting remain separate.
12. Complete: [userspace UDP datagrams](../networking.md#udp-tools), with explicit
    address binding, endpoint capabilities, bounded queues and loopback/host
    client-server use.
    DHCP and TCP follow as separate milestones.

13. Complete: [host-backed randomness](../randomness.md), using VirtIO entropy
    and a bounded native READ capability.
14. Complete: [DNS queries and hostname ping](../dns.md), using a shared
    userspace client, route-aware UDP opening and a configured default resolver
    at `1.1.1.1`. Numeric ping remains independent of DNS and randomness.

15. Complete: [outbound TCP streams](../tcp.md), native connection capabilities,
    a request/response client and a transmit-only ttcp tool. Listening and application protocols follow separately.
16. Complete: [per-CPU trusted init scripts](../init.md), with development,
    read-only and idle selections driven by Make/Limine configuration.
17. Complete: [writable virtio-fs](../virtio-fs.md), with persistent host-backed
    source and executables, and different grants in two spaces.
18. Complete: [space titles](../init.md#space-titles), with a caller-space
    capability and `title` shell builtin. Labels survive init exit; fixed tab
    widths and navigation are unchanged.

19. Complete: [allocation benchmarks and memory profiling](../allocation-profiling.md),
    with native heap/growth/page workloads and opt-in caller-scoped BSP timing.
    The measured follow-up now notifies the BSP promptly after private-memory
    publication, reducing queue delay without changing allocation policy.

20. Complete: [standard streams, redirection and pipelines](../shell-streams.md),
    with dedicated capability grants, bounded native pipes, all-or-none batch
    preparation, foreground shell pipelines and exact bounded `head` consumption.
21. Complete: [libc portability](../libc-portability.md), with descriptor ownership
    shared with stdio, public open/read/write/close, and packaged sbase cksum and
    restricted tee. The documentation handoff is complete; accepted compatibility
    limits and their revisit points are recorded in technical debt.

22. Complete: [userspace services and HTTP snapshots](../userspace-services.md),
    with bounded call/send/receive, deadlines, exported objects, scoped namespaces
    and ordinary file consumers using immutable text and HTTP snapshots. The
    documentation handoff is complete; accepted limits remain in technical debt.

23. Complete: [I/O and IPC performance baselines](../io-ipc-baselines.md),
    with verified file, pipe, endpoint and HTTP workloads, separate completion
    boundaries, and a recorded nested-KVM baseline. Owner-host results remain
    unavailable; capacity and attribution follow-ups are documented.

24. Complete: [I/O reliability and attribution](../io-reliability-attribution.md),
    with prompt receipt reuse, RAM/HOST attribution, and initial HOST publication
    notification. Remaining resolution and combined-matrix work is deferred.

Everyday use for simple tasks guides this order. Website hosting remains one
future application, not the primary completion target for the OS.

This focus order does not commit to working on the milestones together.
Ports depend on the SDK; init does not need the repository split. PCI/VirtIO
infrastructure can be developed independently, while its final mount setup uses
init. Each milestone should become several focused PRs where needed.

[Later directions](later-os-directions.md) park the remaining ports, later networking,
website hosting, block storage, filesystem-format choices and an installer.
The [edit/build/run workflow](../edit-build-run.md) now supports writing C in
Pyxis, compiling it there and running the native P1F result. Guest Lua now has a
concrete configuration consumer in the [session launcher](../session-configuration.md).
Clock/calendar functions and host Lua build tools
remain independent of that port.

The [Doom port](../doom.md) uses the mapped display, keyboard sessions and
monotonic clock for single-player gameplay and demo playback. Images include
shareware data; local retail WADs and demos are optional overrides. PCI/VirtIO
is not a prerequisite.

VirtIO driver order is agreed: virtio-fs, then virtio-net, then virtio-blk.
An opt-in [host-backed development overlay](host-development-overlay.md)
remains postponed: programs can already run from `host://`, so it is not needed
for the persistent development loop or as a replacement for the boot archive.

## Next milestone and later candidates

The [I/O reliability and attribution report](../io-reliability-attribution.md)
closes the performance milestone. The former IPC/HTTP failures are resolved,
and HOST publication now notifies the BSP. Profiling perturbation and remaining
measurement coverage are recorded in [technical debt](../technical-debt.md).

The selected next milestone is [verified HTTPS snapshots with Mbed TLS](https.md).
Task 1 selected Mbed TLS 4.1.1 / TF-PSA-Crypto 1.1.1 and settled the HTTPS
contract, including public roots augmented by optional instance-specific custom
roots. Task 2 packages the configured libraries and native platform support;
controlled TLS 1.2/1.3 guest connections and bounded failure paths have passed
manual validation. Task 3 adds verified HTTPS fetching to libhttp, with controlled
framing, certificate, deadline and cleanup checks. Task 4 packages public trust
and publishes independent HTTPS snapshots through configured session namespaces,
with optional startup failure handling. Task 5 remains the public/certificate
workflow validation and documentation handoff.
SSH/libssh remains deferred. The following remain later alternatives.

| Path | First concrete completion point | Decisions and supporting work |
| --- | --- | --- |
| SDL2 and graphical applications | A native software-rendered SDL2 backend supports a selected GrafX2 edit/save workflow. | Probe the pinned application first; settle input/presentation and image-library needs. zlib/libpng are useful shared candidates. Compositor and GPU support stay separate. |
| SQLite | A native SQLite library/CLI creates, queries and reopens a database with an explicitly supported persistence/access contract. | File identity, locking, journaling and sync need discussion; an in-memory slice can come first. Scheme views follow the port and provider infrastructure. |
| Terminal applications | PDCurses over native terminal facilities supports one selected application. | Probe its actual terminal/input/libc requirements; NetHack, Frotz and retawq remain candidates with different frontends. |
| Quake | A selected software-rendered port runs single-player or a demo. | Host/target compile probe, libc, display, input and timing gaps. Audio and multiplayer can follow; no GPU prerequisite. |
| Native disk storage | Virtio-blk reaches a bounded block-I/O milestone before filesystem/installation work. | Block capability/backend contract, flush/error semantics and later disk-format selection; retain the user/ownership checkpoint before durable home policy. |

The [application port candidates](application-ports.md) include longer-term
DevilutionX and C AbyssEngine/Diablo II investigations. The
[scheme-provider notes](userspace-scheme-providers.md) record SQLite views,
database sessions and the editor worksheet idea. Their URI examples are future
interactions, not supported shell syntax or a settled ABI.

[Hosted toolchains and language runtimes](toolchains-and-runtimes.md) park
binutils/P1F investigation, the GCC-versus-LLVM choice, C++ userspace, Go cross
compilation and a later hosted Go toolchain. Rust, Tailscale and Ladybird are
future directions with their own scope decisions.

SDL2/GrafX2 remains a later graphical alternative. A desktop/compositor remains
a separate [graphics direction](desktop-graphics.md), and users/authority is a cross-cutting
[design checkpoint](users-and-authority.md), not something a port should define
implicitly.

## Agreed boundaries

- Init performs setup and hands off to the shell. Supervision/restart policy
  waits for the first web-server milestone. Start with a shebang shell script,
  fail on script errors and use an explicit session launch; `exec` comes later.
- Userspace owns libc, libpyxis, libterm, startup and applications. Pyxis owns
  public ABI headers and elf2pxe, and assembles the SDK, kernel and boot image.
- Export headers, build runtime libraries, assemble the SDK, then build apps and
  ports. Initially pin the new userspace/ports repositories as submodules.
- The owner handles repository creation, dispatch integration and compiler
  container publication. Ordinary builds consume the prebuilt compiler and
  evolving SDK; they do not rebuild GCC/binutils.
- Init mounts optional `host://` before launching the shell and passes the
  selected directory grant to the session.
- No container, workflow, repository or submodule changes are part of this draft.

## User and permission design checkpoint

Multiple users with restricted permissions are a requirement. The
[users and authority notes](users-and-authority.md) identify decisions to make
before persistent home storage, writable shared mounts and cross-user services
make ownership assumptions expensive to change. This is a design checkpoint,
not a requirement to implement accounts before the current init work.

## Shell follow-ups

The fresh-line prompt and current working-path display are implemented. Their
behavior and limits are documented in [the shell reference](../shell.md) and
[terminal reference](../terminal.md).

## Completing a milestone

Rewrite the completed milestone document around the implemented behavior and
useful interface/usage guidance, then move it from `docs/wip` to `docs` and update
links. Remove the planning history and completed checklist; the original remains
in Git history. Carry forward relevant deferred work into another WIP or
technical-debt document. Do not retain a duplicate archive of the old plan.

## Existing context

- [Shell, filesystem and application runtime](../first-shell.md).
- [Earlier development candidates](development-paths.md).
- [Filesystem direction](../vfs.md) and [space direction](../spaces.md).
