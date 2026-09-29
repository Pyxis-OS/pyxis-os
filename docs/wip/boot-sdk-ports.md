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

## Closed HTTPS milestone and later candidates

The [I/O reliability and attribution report](../io-reliability-attribution.md)
closes the performance milestone. The former IPC/HTTP failures are resolved,
and HOST publication now notifies the BSP. Profiling perturbation and remaining
measurement coverage are recorded in [technical debt](../technical-debt.md).

The [verified HTTPS snapshots milestone](../https.md) is complete. Mbed TLS
4.1.1 / TF-PSA-Crypto 1.1.1 supplies client TLS through native userland hooks;
separate providers publish verified snapshots using packaged public roots and
optional instance-specific augmentation. Implemented behavior, configuration and
limits live in the subsystem docs, with the
[fetch/inspect/compile/run workflow](../edit-build-run.md#fetch-source-over-https)
beside the local development loop. SSH/libssh remains deferred. The
[block-storage foundation](../block-storage.md) is also complete. The
[initial filesystem format and read-only core](../filesystem-readonly.md) is
complete; later writable recovery, FUSE and native integration remain proposals.

The selected next milestone is [task state and BSP service requests](bsp-service-requests.md).
It separates subsystem requests from scheduling while retaining BSP allocation
and VM ownership, with FIFO service, prompt worker notification and scheduling
opportunities between operations. Request completion is separate from waiting;
public asynchronous I/O and process threads are outside this milestone.

After its closure, the agreed next milestone is [native remote terminal sessions](remote-terminal.md):
TCP listeners, readiness waits, independent terminals and contained execution
lifetime lead to a text-based remote shell for agent/developer work. A host client
supports interactive Kilo and machine-readable command completion; framebuffer
screenshots remain for graphical work. Authentication, a multiplexer and process
threads are separate. The milestone records remaining task-local decisions and
does not start implementation before BSP request closure.

The broader [discussion agenda](storage-and-terminal-agenda.md) covers persistent
disk storage, bounded Neovim/libuv and LLVM requirements investigations, and
native terminal sessions leading to a BSP multiplexer and independent navigators.
The initial [Neovim/libuv investigation](neovim-libuv.md) is complete, with pinned source
evidence and header probes; its proposed native event, threading, metadata and
terminal milestones remain deferred. No Neovim build or runtime compatibility
is claimed. The LLVM investigation remains separate future work.

The [scheduling and threads direction](scheduling-and-threads.md) connects future
process threading with independent space identity, task migration and execution
across CPUs, initially retaining BSP services. Its staged proposals and ownership
decisions do not start another implementation track.

The [persistent storage design](persistent-storage.md) records the agreed custom
COW pool, volume guarantees, migration strategy, durability and compatibility
contracts. The [block-storage foundation](../block-storage.md) is complete:
[configurable split queues](../virtio-queues.md) serve filesystem, entropy and
virtio-blk; bounded ticketed reads/writes and ordered flushes support
[GPT discovery](../gpt.md). Final validation combined those devices on the merged
implementation. Device/transport failure remains terminal until reboot. The
[initial format and read-only shared core](../filesystem-readonly.md) is complete.
PyxisOS/pyxis-fs owns the pinned freestanding core and Linux host tools built by
`make fs-tools`. They create populated images, reopen and extract them, evaluate
bounded acquisition and check both retained states. The
[format contract](../../fs/docs/format.md), [core interfaces](../../fs/docs/core.md)
and [measured host validation](../../fs/docs/host-tools.md#validation) are durable
references. Writable recovery, FUSE and native-persistence milestones remain
proposed; BSP request separation is selected before continuing those tracks.

| Path | First concrete completion point | Decisions and supporting work |
| --- | --- | --- |
| SDL2 and graphical applications | A native software-rendered SDL2 backend supports a selected GrafX2 edit/save workflow. | Probe the pinned application first; settle input/presentation and image-library needs. zlib/libpng are useful shared candidates. Compositor and GPU support stay separate. |
| SQLite | A native SQLite library/CLI creates, queries and reopens a database with an explicitly supported persistence/access contract. | File identity, locking, journaling and sync need discussion; an in-memory slice can come first. Scheme views follow the port and provider infrastructure. |
| Terminal applications | PDCurses over native terminal facilities supports one selected application. | Probe its actual terminal/input/libc requirements; NetHack, Frotz and retawq remain candidates with different frontends. |
| Quake | A selected software-rendered port runs single-player or a demo. | Host/target compile probe, libc, display, input and timing gaps. Audio and multiplayer can follow; no GPU prerequisite. |
| Native disk storage | The [block-storage foundation](../block-storage.md) and [initial format and read-only core](../filesystem-readonly.md), including populated image tools and whole-image checking, are complete. | Writable recovery, FUSE and native mounts remain proposals with separate policy and implementation gates. |

The [application port candidates](application-ports.md) include longer-term
DevilutionX and C AbyssEngine/Diablo II investigations. The
[scheme-provider notes](userspace-scheme-providers.md) record SQLite views,
database sessions and the editor worksheet idea. Their URI examples are future
interactions, not supported shell syntax or a settled ABI.

[Hosted toolchains and language runtimes](toolchains-and-runtimes.md) record
LLVM/Clang as the chosen direction: host-side toolchain migration, native C++
prerequisites and then Clang running inside Pyxis are separate milestones to
scope. Go cross compilation, hosted Go, Rust, Tailscale and Ladybird remain
future directions with their own decisions.

SDL2/GrafX2 remains a later graphical alternative. A desktop/compositor remains
a separate [graphics direction](desktop-graphics.md), and users/authority is a cross-cutting
[design checkpoint](users-and-authority.md), not something a port should define
implicitly.

## Agreed boundaries

- Init performs setup and hands off to the shell. General service supervision
  and restart policy remain deferred; remote terminal execution groups have the
  explicit lifetime contract in their milestone. Start with a shebang shell
  script, fail on script errors and use an explicit session launch; `exec` comes
  later.
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
[users and authority notes](users-and-authority.md) record stable principals,
policy-based acquisition, runtime capabilities and prospective permission changes.
They retain the unresolved enforcement and lifecycle decisions before persistent
ownership is implemented. External login and account UI are separate work.

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
