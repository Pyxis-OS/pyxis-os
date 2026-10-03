# Development milestone index

Status: working discussion after the first-shell milestone. These documents
separate the next concrete results from parked ideas; they do not authorize
implementation. The initial scope decisions are recorded; resolve each
milestone's remaining interface details before starting its code work.

## Suggested focus order

1. [Init and primitive scripts](../userland/init.md) — complete: selected native or
   shebang init and explicit handoff to an interactive shell.
2. [SDK and repository separation](../development/sdk-and-repositories.md) — complete:
   exported SDK, prebuilt Pyxis compiler and pinned userspace submodule with
   the integrated build preserved.
3. [Port recipes and Kilo](../development/ports.md) — complete: pinned host Lua recipes,
   SDK-based Kilo build and ordinary boot-archive integration. The
   [edit/build/run workflow](../development/edit-build-run.md) with TCC is also complete.
4. [Filesystem mutations and Doom saves](../interfaces/filesystem-mutations.md) — complete.
5. TTY horizontal tabs — complete: eight-column stops, clamped at the right edge,
   without erasing cells or wrapping. See [terminal controls](../userland/terminal.md#tty-output-controls).
6. [UTC wall-clock and calendar conversion](../kernel/wall-clock.md) — complete:
   ISO date display, independent monotonic deadlines and TCC time features.
7. [Boot archive assembly](../development/boot-archive.md) — complete: install trees, Lua
   manifest and [independent build bundles](../development/build-bundles.md).
   [Automatic artifact selection](build-artifact-reuse.md) remains follow-up work.
8. Complete: [zoneinfo-backed local time](../userland/timezones.md), including the full
   pinned database, UTC for absent/empty `TZ`, named zones and `date -u`.
   Locale and reverse conversion remain deferred.
9. Complete: [guest Lua](../userland/lua.md), including scripts, REPL and default
   [session configuration](../userland/session-configuration.md) for timezone and tab width.
10. Complete: [PCI and VirtIO host filesystem access](../devices/virtio-fs.md).
    Init opens the opt-in export and delegates `host://` through the
    session launcher to the shell and children. Existing `ls`/`cat` use native
    directory/file capabilities; archive-only boot remains the default without
    a device/socket.
11. Complete: [initial networking](../devices/networking.md), with loopback and virtio-net,
    manual IPv4 configuration and native ping. DHCP follows later through the
    same configuration interface; TCP and website hosting remain separate.
12. Complete: [userspace UDP datagrams](../devices/networking.md#udp-tools), with explicit
    address binding, endpoint capabilities, bounded queues and loopback/host
    client-server use.
    DHCP and TCP follow as separate milestones.

13. Complete: [hardware-backed randomness](../devices/randomness.md), using VirtIO entropy
    and a bounded native READ capability.
14. Complete: [DNS queries and hostname ping](../userland/dns.md), using a shared
    userspace client, route-aware UDP opening and a configured default resolver
    at `1.1.1.1`. Numeric ping remains independent of DNS and randomness.

15. Complete: [outbound TCP streams](../devices/tcp.md), native connection capabilities,
    a request/response client and a transmit-only ttcp tool. Listening and application protocols follow separately.
16. Complete: [per-CPU trusted init scripts](../userland/init.md), with development,
    read-only and idle selections driven by Make/Limine configuration.
17. Complete: [writable virtio-fs](../devices/virtio-fs.md), with persistent host-backed
    source and executables, and different grants in two spaces.
18. Complete: [space titles](../userland/init.md#space-titles), with a caller-space
    capability and `title` shell builtin. Labels survive init exit; fixed tab
    widths and navigation are unchanged.

19. Complete: [allocation benchmarks and memory profiling](../development/allocation-profiling.md),
    with native heap/growth/page workloads and opt-in caller-scoped BSP timing.
    The measured follow-up now notifies the BSP promptly after private-memory
    publication, reducing queue delay without changing allocation policy.

20. Complete: [standard streams, redirection and pipelines](../userland/shell-streams.md),
    with dedicated capability grants, bounded native pipes, all-or-none batch
    preparation, foreground shell pipelines and exact bounded `head` consumption.
21. Complete: [libc portability](../userland/libc-portability.md), with descriptor ownership
    shared with stdio, public open/read/write/close, and packaged sbase cksum and
    restricted tee. The documentation handoff is complete; accepted compatibility
    limits and their revisit points are recorded in technical debt.

22. Complete: [userspace services and HTTP snapshots](../interfaces/userspace-services.md),
    with bounded call/send/receive, deadlines, exported objects, scoped namespaces
    and ordinary file consumers using immutable text and HTTP snapshots. The
    documentation handoff is complete; accepted limits remain in technical debt.

23. Complete: [I/O and IPC performance baselines](../development/io-ipc-baselines.md),
    with verified file, pipe, endpoint and HTTP workloads, separate completion
    boundaries, and a recorded nested-KVM baseline. Owner-host results remain
    unavailable; capacity and attribution follow-ups are documented.

24. Complete: [I/O reliability and attribution](../development/io-reliability-attribution.md),
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
The [edit/build/run workflow](../development/edit-build-run.md) now supports writing C in
Pyxis, compiling it there and running the native P1F result. Guest Lua now has a
concrete configuration consumer in the [session launcher](../userland/session-configuration.md).
Clock/calendar functions and host Lua build tools
remain independent of that port.

The [Doom port](../userland/doom.md) uses the mapped display, keyboard sessions and
monotonic clock for single-player gameplay and demo playback. Images include
shareware data; local retail WADs and demos are optional overrides. PCI/VirtIO
is not a prerequisite.

VirtIO driver order is agreed: virtio-fs, then virtio-net, then virtio-blk.
An opt-in [host-backed development overlay](host-development-overlay.md)
remains postponed: programs can already run from `host://`, so it is not needed
for the persistent development loop or as a replacement for the boot archive.

## Current focus and later candidates

The [native read-only filesystem milestone](../devices/native-readonly-filesystem.md)
is complete: init-selected GPT partition/volume mounting, policy-approved objects,
explicit root delegation, ordinary reads and executable loading, scoped filesystem
information and Fastfetch Disk. Combined validation covers source-content reads,
retained rights, repeated operations, final cleanup and unchanged media.
The agreed storage continuation is [writable core and recovery](writable-filesystem-core.md),
with [native persistent volumes](native-persistent-volumes.md) as its later
integration. The first task
settles bounded publication/reclamation, live handles, admission and maintained
host contract tests with a per-filesystem-PR CI merge gate. Tests and the bounded
failure adapter land with their behavior in pyxis-fs. The namespace profile, tighter
deletion reservation and Unity setup are accepted; real-host post-error recovery
qualification is deferred. Tasks 1–6 are complete, including public file/namespace
mutation, retained-handle lifetime, funded orphan cleanup and their maintained
tests. Task 7 has shared extended campaigns and the populated 4 GiB recovery
workload, with a focused contiguous-volume-selection correction for the observed
fragmentation limit. Whole-map write costs and remaining qualification still
block milestone closure and writable deployment. Task 7 now requires
RAM-only validation/baselines, incremental allocation-map design with revised
admission and cleanup bounds, implementation/recovery tests, and repeated write-cost
acceptance measurements. Large disk-backed runs are suspended; larger profiles
need a bounded RAM-only execution plan. The placement fix remains useful but does
not resolve this blocker. The first corrective step adds the scoped
2 GiB RAM / 4 GiB job launcher and a small Pyxis/ext4/Btrfs baseline; this VM is
the agreed safety boundary. Two 40-case matrices and the maintained suites pass;
the initial target is now Btrfs-comparable submitted-write costs on representative
matched workloads, with ext4 retained as a comparison and longer-term goal.
The delivered narrow final-orphan
cleanup reduces compiler-history totals by 15.70–18.79% through maintenance.
The merged [bounded retirement-debt correction](filesystem-retirement-debt.md)
(filesystem #20 / Pyxis #312) carries reclamation across individually durable
publications and lowers matched compiler totals by a further 53.19–58.95%.
All eight short Pyxis cases beat recorded Btrfs totals; sustained writes, larger
populations/fragmentation, namespace churn, pressure and recovery qualification
remain open. The [incremental-map proposal](filesystem-write-efficiency.md#incremental-allocation-map-proposal)
records the accepted topology-preserving intermediate path and explicit funded
bulk/global-closure limitation. The first implementation is merged; the assigned
focused planning follow-up adds indexed retirement membership and phase-separated
diagnostics under the same budgets. The subsequently assigned
[bounded sustained follow-up](../../fs/docs/sustained-map-measurements.md)
completes 54 independently verified cases on source maps up to J=216, with all
setup, preparation, windows and final maintenance included. Broad local closure
dominates append/overwrite metadata traffic; the largest such histories exceed
Btrfs totals. Compiler writes are non-monotonic with population while instrumented
planning time grows. Native member-cgroup peaks are not complete RAM high-water
measurements; independent scratch/trace bounds and conservative estimates remain.
The subsequently assigned [occupancy-aware neighbour repair](../../fs/docs/neighbour-repair-measurements.md)
compares complete expanded runs under the same funding/termination bounds.
Matched results are mixed; this selector does not settle broad closure or planning
costs. After #327 merged, the owner accepted and assigned the
[bounded leaf-overflow split](filesystem-overflow-split.md): one extra leaf under
an existing parent, distinct retired/emitted counts, renewed self-accounting and
one seed restoration before ordinary repair/bulk. Its
[implementation and matched record](../../fs/docs/overflow-split-measurements.md)
retain existing admission/reserve/memory/runner policy. General structural editing
and further optimisations require separate assignment.
On 2026-10-02 the owner superseded this writable track with a
[simple native filesystem](native-filesystem.md): ext2-class structures, native
to Caelum, with a format-only shared library. The pyxis-fs writer's measured
workload time reached 13–80 ms per logical operation in RAM, growing with
population. Native read-only
mounts keep working until the replacement lands.
Broader population/pressure and recovery qualification, deployment criteria and
task 7 remain open. See
the linked milestone for the corrective task list.
FUSE and installation remain separate. These
plans do not start implementation or displace explicitly assigned libc/remote work.

The [libc input read-ahead milestone](../userland/stdio.md#input-read-ahead) is
complete: buffered stdio fetches file and pipe input in BUFSIZ blocks while
consoles stay exact, fgets no longer mistakes an earlier error for a failed
final line, and the recorded uniq workload fell from 20.4 s to 0.04 s. Output
buffering, `setvbuf` and `ungetc` remain outside it.

The [I/O reliability and attribution report](../development/io-reliability-attribution.md)
closes the performance milestone. The former IPC/HTTP failures are resolved,
and HOST publication now notifies the BSP. Profiling perturbation and remaining
measurement coverage are recorded in [technical debt](../technical-debt.md).

The [verified HTTPS snapshots milestone](../userland/https.md) is complete. Mbed TLS
4.1.1 / TF-PSA-Crypto 1.1.1 supplies client TLS through native userland hooks;
separate providers publish verified snapshots using packaged public roots and
optional instance-specific augmentation. Implemented behavior, configuration and
limits live in the subsystem docs, with the
[fetch/inspect/compile/run workflow](../development/edit-build-run.md#fetch-source-over-https)
beside the local development loop. SSH/libssh remains deferred. The
[block-storage foundation](../devices/block-storage.md) is also complete. The
[initial filesystem format and read-only core](../devices/filesystem-readonly.md) is
complete; writable core/native integration scopes are linked above, while FUSE
remains a later proposal.

The [task state and BSP service requests milestone](../kernel/bsp-service-requests.md)
is complete. Subsystem requests are separate from scheduling while retaining
BSP allocation and VM ownership, with FIFO service, prompt notification and
scheduling opportunities between operations. Request completion is separate from waiting;
user tasks have reusable request storage and separate persistent profiling.
Public asynchronous I/O and process threads remain outside the implementation.

The [native remote terminal implementation](../userland/remote-terminal.md) provides
TCP listeners, readiness waits, independent terminals and contained execution
lifetime for text-based agent/developer work. Its host client supports interactive
Kilo and machine-readable command completion; framebuffer screenshots remain for
graphical work. See [terminal sessions](../userland/terminal-sessions.md) and
[execution groups](../interfaces/execution-groups.md) for the underlying contracts.
Authentication, a multiplexer and process threads remain separate work.
[Foreground interruption](../userland/foreground-interruption.md) is
implemented: Ctrl+C terminates a running foreground command or pipeline through
process-level termination, shell-armed interrupt events and a minimal
application passthrough for programs such as Kilo.

The parked [VirtIO GPU presentation direction](desktop-graphics.md#virtio-gpu-presentation-and-display-resizing)
groups software framebuffer presentation, live display resizing and terminal
geometry notifications into one future milestone. It requires no 3D acceleration and does not
block a multiplexer on a fixed-size display.
The agreed [visible-work sequence](storage-and-terminal-agenda.md#4-native-terminal-sessions-multiplexer-and-navigator)
after filesystem core and spaces/SMP is that display milestone, a resize-aware
single-panel file navigator, a terminal multiplexer, then cross-navigator operations.
These are separate bounded milestones; the navigator first runs in an ordinary
terminal and later becomes a multiplexer consumer.

The broader [discussion agenda](storage-and-terminal-agenda.md) covers persistent
disk storage, bounded Neovim/libuv and LLVM requirements investigations, and
native terminal sessions leading to a BSP multiplexer and independent navigators.
The initial [Neovim/libuv investigation](neovim-libuv.md) is complete, with pinned source
evidence and header probes; its proposed native event, threading, metadata and
terminal milestones remain deferred. No Neovim build or runtime compatibility
is claimed. The LLVM investigation remains separate future work.

After writable filesystem core completion, the agreed
[runtime SMP milestone](scheduling-and-threads.md) separates spaces and boot sessions
from CPU topology, schedules existing single-task processes across eligible CPUs
including the BSP, and brings private-memory operations onto the caller's CPU.
Trusted init requests affinity within a launcher-supplied ceiling; initial
per-task fairness and a scrolling space bar are explicit. Capture matched
before/after performance records; selected services remain serial initially.
Declarative YAML init is a separate userspace follow-up, not an SMP prerequisite.
User threads and off-BSP service-worker placement follow separately. Native writable
mount integration remains a storage follow-up, not a prerequisite for this SMP work.
The [backend interfaces and scoped dependency direction](later-os-directions.md#backend-interfaces-and-scoped-service-dependencies)
is parked for later; it does not change the filesystem-core-then-spaces/SMP order.

The [persistent storage design](persistent-storage.md) records the agreed custom
COW pool, volume guarantees, migration strategy, durability and compatibility
contracts. The [block-storage foundation](../devices/block-storage.md) is complete:
[configurable split queues](../devices/virtio-queues.md) serve filesystem, entropy and
virtio-blk; bounded ticketed reads/writes and ordered flushes support
[GPT discovery](../devices/gpt.md). Final validation combined those devices on the merged
implementation. Device/transport failure remains terminal until reboot. The
[initial format and read-only shared core](../devices/filesystem-readonly.md) is complete.
PyxisOS/pyxis-fs owns the pinned freestanding core and Linux host tools built by
`make fs-tools`. They create populated images, reopen and extract them, evaluate
bounded acquisition and check both retained states. The
[format contract](../../fs/docs/format.md), [core interfaces](../../fs/docs/core.md)
and [measured host validation](../../fs/docs/host-tools.md#validation) are durable
references. The [native read-only mount milestone](../devices/native-readonly-filesystem.md) is also
complete. The [writable core](writable-filesystem-core.md) and
[native persistence](native-persistent-volumes.md) follow as separate milestones;
FUSE is not a prerequisite.
BSP request separation is complete.

The [USB installation proposal](usb-installation.md) records a replaceable SanDisk
target and QEMU-first stages: boot Limine/kernel/archive from a FAT32 EFI partition,
add xHCI/Bulk-Only read-only access to a separate Pyxis pool, then integrate native
persistence and validate the ThinkPad. Real-device passthrough is an intermediate
check, not validation of the laptop controller. Boot-image Phase A now provides
an opt-in [raw image and emulated USB launch](../development/usb-image.md);
Phase B.1 now defines explicit backend/unique-disk selection, controller resource
preparation, bounded requests and failure ownership. Phase B.2 adds
[controller ownership, rings, MSI-X and root-port slot reservations](../devices/usb-xhci.md).
Phase B.3 adds [checked enumeration and control requests](../devices/usb-enumeration.md).
The later inspection-first slice discovers controllers independently and publishes
root-device observations and [USB 2 hub descendants](../devices/usb-hubs.md)
without selecting/configuring a storage transport. BOT/SCSI reads and native block integration
remain pending; writable work is unassigned. Reusable controller/USB/class/block
boundaries are required, without
speculative driver frameworks. This does not reorder filesystem core, spaces/SMP
or the visible-work sequence.

The [ThinkPad KVM and invariant-TSC investigation](thinkpad-kvm-tsc.md) records
a successful four-vCPU KVM boot, an owner-observed native HPET panic, the
native/guest timer differences and the **2026-10-03 owner decision: implement
software-extended 32-bit HPET first** to continue native bring-up. Its
[implementation handoff](thinkpad-kvm-tsc.md#accepted-direction-and-implementation-handoff)
records the sampling/concurrency constraints and validation scope. TSC with
extended-HPET fallback is the accepted future direction, deferred from this
first task. The clock now supports software extension and a menuconfig-editable
BSP maintenance interval, default 120 timer ticks. Its accepted support limit
requires less than one advancing-counter wrap between incorporated samples;
the owner subsequently reached native userspace on all 12 ThinkPad CPUs.
The [native continuation](thinkpad-kvm-tsc.md#native-bring-up-continuation)
records the native 32-bit/software-extended clock-path log and next keyboard
blocker; the native multi-wrap check remains pending.

The assigned [ThinkPad keyboard diagnostics](thinkpad-keyboard-diagnostics.md)
follow separately: name PS/2 setup failures, reduce PCI inventory log noise and
replay early logs into the Caelum tab. The native result identified an absent
scan-set query ID; the owner approved a short optional-ID wait after ACKed set-2
selection, with native input qualification still pending.

The separate [hardware inspection](../devices/hardware-inspection.md) provides native
`lspci`/`lsusb` consumers of read-only inventory and pinned plain-text PCI/USB name
databases. USB inventory covers root devices and USB 2 hub descendants; unsupported hub
branches report partial inventory; these tools do not expand boot-image
Phase A. The [everyday pipeline performance target](../development/io-reliability-attribution.md#everyday-pipeline-performance-target)
records their use as text-tool input without assigning a port or benchmark campaign.

The ThinkPad's [next bring-up steps](thinkpad-next-steps.md) record the owner's
order after the native shell: the assigned CPU entropy task first (virtio-rng
when present, RDSEED with RDRAND fallback otherwise, ChaCha20 later), then
Ethernet through VFIO passthrough to QEMU and a driver.

| Path | First concrete completion point | Decisions and supporting work |
| --- | --- | --- |
| SDL2 and graphical applications | A native software-rendered SDL2 backend supports a selected GrafX2 edit/save workflow. | Probe the pinned application first; settle input/presentation and image-library needs. zlib/libpng are useful shared candidates. Compositor and GPU support stay separate. |
| SQLite | A native SQLite library/CLI creates, queries and reopens a database with an explicitly supported persistence/access contract. | File identity, locking, journaling and sync need discussion; an in-memory slice can come first. Scheme views follow the port and provider infrastructure. |
| Terminal applications | PDCurses over native terminal facilities supports one selected application. | Probe its actual terminal/input/libc requirements; NetHack, Frotz and retawq remain candidates with different frontends. |
| Quake | A selected software-rendered port runs single-player or a demo. | Host/target compile probe, libc, display, input and timing gaps. Audio and multiplayer can follow; no GPU prerequisite. |
| Native disk storage | The [block-storage foundation](../devices/block-storage.md), [read-only core](../devices/filesystem-readonly.md) and [native mounts](../devices/native-readonly-filesystem.md) are complete. | [Writable core/recovery](writable-filesystem-core.md), then [native persistence](native-persistent-volumes.md); FUSE remains later. |

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

The [initial Go runtime investigation](go-runtime.md) records a pinned source
audit and host hello-world probes. ELF-to-P1F conversion already works for the
two Linux probe images; native startup, runtime threading/TLS, synchronization
and VM semantics remain unresolved. No Go target or guest execution is claimed.

[Fastfetch](../userland/fastfetch.md) is packaged in the normal image. Its native
information adapters, existing libc prerequisites and local/remote integration
acceptance are complete. The port preserves upstream formatting and error
behavior; its implemented reference records observation and display limits.

[Uniq](../userland/uniq.md) is packaged from the existing sbase pin, adding only
libc `getline` and `isblank`, and was validated through the remote shell against
the host build of the same upstream revision. It does not authorize a full
utility suite. Its large-input cost was removed by libc input read-ahead;
[console line input](../technical-debt.md#console-line-input) remains per byte.

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
The [credentials and biometric unlock direction](credentials-and-biometrics.md)
is parked after local users; it adds no tasks to current milestones.

## Shell follow-ups

The fresh-line prompt and current working-path display are implemented. Their
behavior and limits are documented in [the shell reference](../userland/shell.md) and
[terminal reference](../userland/terminal.md).

## Completing a milestone

Rewrite the completed milestone document around the implemented behavior and
useful interface/usage guidance, then move it from `docs/wip` to `docs` and update
links. Remove the planning history and completed checklist; the original remains
in Git history. Carry forward relevant deferred work into another WIP or
technical-debt document. Do not retain a duplicate archive of the old plan.

## Existing context

- [Shell, filesystem and application runtime](../userland/first-shell.md).
- [Earlier development candidates](development-paths.md).
- [Filesystem direction](vfs.md) and [space direction](spaces.md).
