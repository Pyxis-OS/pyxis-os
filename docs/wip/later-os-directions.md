# Later OS directions

Status: parked ideas, not an implementation milestone or worklist. Promote one
to a focused milestone when its prerequisites and intended result are clear.
See the [planning index](boot-sdk-ports.md) for the current sequence.

Parked elsewhere: [capability-valued environment variables](capability-environment.md)
and [subsystems per space](subsystems-per-space.md).

Owner direction (2026-10-11): extend negotiated remote resize to plain interactive sessions after the mux-only slice.

## Package manager

Parked: a package manager has multiple dependencies and will replace [manual toolchain revision publication](../development/hosted-tools.md#manual-immutable-publication); see the related [source-build direction](source-builds.md). No work is assigned.

Selecting existing build artifacts, so a developer can consume successful CI work
without finding and extracting each [bundle](../development/build-bundles.md), is
part of this direction (owner's call, 2026-10-14) rather than a separate task. The
rules worth keeping:

- Begin with explicit artifact selection and a local cache. Decide the Forgejo
  run and artifact selector, authentication source, retention and missing-artifact
  fallback first, never embedding repository tokens in bundles or updating source
  checkouts as a side effect of `make run`.
- Match exact input fingerprints per component: source inputs, compiler and build
  settings and SDK dependency identity. Never trust a "clean checkout", which can
  be behind upstream, and never weaken a dependency check to avoid work.
- Local edits rebuild the affected components and their dependents; unrelated
  bundles stay reusable.
- Explain every decision: which components were downloaded, reused or built, and
  why a candidate was rejected. Document offline behavior.

This is build-product reuse, not a cross-repository dispatch system, which remains
a separate owner-managed integration.

## Execution lifecycle

The implemented [init handoff](../userland/init.md) launches a successor and exits. Real
process replacement (`exec`) still needs its own resource and failure contract.
Supervision and restart policies belong to [Asterism](spaces.md#asterism);
whichever comes first, supervision or a web server, sets the first contract.
This does not commit to Unix PID 1 semantics.

## Direct framebuffer panic context

Proposal from [PR #468 review](https://git.internal/PyxisOS/pyxis-os/pulls/468),
awaiting an owner decision: show the last lines of the
[retained kernel log ring](../interfaces/kernel-log.md) above a panic message
on a direct framebuffer. Revisit it with
[direct panic output](../kernel/early-console.md#panic-ownership).

## Backend interfaces and scoped service dependencies

Agreed direction to revisit later, not an implementation task; it adds no
prerequisites to the completed [native filesystem writer](../devices/filesystem-native-adapter.md)
or [runtime SMP](../kernel/smp.md) milestones.

Borrow explicit dependency wiring and scoped resolution from inversion of control.
Kernel filesystem wrappers still select backends through explicit backing kinds;
a later implementation interface could reduce that repeated dispatch. Keep these
responsibilities distinct:

- Capabilities identify held resources and granted authority.
- Protocols define operations, message layouts and result semantics.
- Backend interfaces connect those operations to concrete implementations.
- Namespaces and scoped resolvers select resources and return bounded grants.

Prefer direct dependencies for required services: a filesystem adapter receives
its block device; an HTTP provider receives networking, clock and trust resources.
Pass a resolver only when dynamic discovery is needed. The resolver itself must be
a capability with bounded scope and delegation rights, never an ambient
machine-wide service locator. Different spaces may resolve the same name to
different providers, and supporting a protocol does not grant authority to invoke
it. Init is the natural place to start providers, bind names and delegate
dependencies. Replacing a binding affects future resolution; existing handles keep
their identity and lifetime; revocation and replacement of held authority need
separate semantics. This preserves the [namespace contract](../interfaces/namespaces.md).

Inside the kernel, consider small typed operation tables and explicit constructor
arguments where multiple implementations already justify them (file and directory
backends; network devices need packet-oriented contracts of their own). Userspace
providers implement message protocols, not kernel function tables. Define
buffer/object ownership, partial progress, completion, cancellation, sleeping and
executor constraints before extracting an interface, and leave room for
asynchronous completion without implementing it speculatively. Avoid a universal
object framework, inheritance hierarchy, reflection, automatic dependency graphs
or interface discovery. This note schedules no refactor and introduces no APIs.

## Completion-based asynchronous operations

Owner direction, 2026-10-10: a planned complement to readiness.
[Pipes](../interfaces/pipes.md#readiness), terminals and TCP use readiness:
wait until an object is ready, then make a nonblocking try transfer. The
complement is completion: submit an operation, keep running, and collect its
single result later. The owner's design discussion put this on
[endpoint](../interfaces/endpoints.md) SEND and RECEIVE: an operation is a message
to a kernel object or provider, and a receiver collects one completion per
submitted operation, including short progress, failure or cancellation. It is the
io_uring and IOCP family, expressed through capabilities. Expected consumers are
graphical event loops (input, frame presentation, compositor messages and provider
replies together), bulk file I/O and several outstanding provider requests.

Settle these before any implementation:

- whether request and reply buffers are copied at submission or borrowed until
  completion, and what happens to borrowed memory on cancel or exit;
- that every submission has exactly one terminal outcome, including cancel,
  close and process exit, with no completion dropped;
- limits on outstanding operations per process and per receiver;
- ordering between completions and readiness on the same object.

The smaller step useful to both models, [endpoint receiver readiness](../interfaces/endpoints.md#receiver-readiness)
with `wait_many`, is implemented and qualified with
[matched CALL/SEND runs](../development/experiments/endpoint-readiness/README.md).
**Owner exception to the planning freeze, accepted 2026-10-12:**
[prepared HTTP requests](native-http-requests.md#task-3-accepted-facets-and-lifetime)
need a bounded native completion prerequisite now: TCP CONNECT, DNS UDP
SEND/RECEIVE and native FILE/DIRECTORY staging, one pending operation per instance,
one owned receiver per correlated result, reserved admission before submission,
and cancel/close/exit ownership drain. Its
[accepted submission/cancel/collection contract](../interfaces/native-completion.md)
is accepted and its bounded interface delivered. The broader completion model remains
parked and unassigned; this permits no general async rewrite or extra per-operation
capability API. [BSP service requests](../kernel/bsp-service-requests.md) remain
synchronous outside the separate native completion admission route.

## Additional ports

The [application and library port candidates](application-ports.md) record the
queue; this is not an instruction to port the whole list. Neovim remains a later
editor goal. GrafX2 is another candidate, alongside the
[desktop and graphics direction](desktop-graphics.md); that draft does not
authorize implementation.

[retawq](https://retawq.sourceforge.net/) is a future text-browser candidate,
starting with HTTP and leaving TLS optional. Terminal support needs a concrete
choice: port a curses library, or evaluate its built-in `bicurses` backend
(`--set-tg=bicurses`, terminfo-based, without color or mouse; also optional TLS and
`--set-threading=0`, see the [build options](https://retawq.sourceforge.net/docu/ctconfig.html))
before committing to ncurses. These are leads for a compile probe, not a confirmed
path. Audit terminal, libc and event/I/O requirements, keep HTTP/TLS in userspace,
and do not start the port or change milestone order on this note.

A custom linker remains a possible later investigation alongside application
bundles/images. TCC already emits native P1F executables; a new linker or custom
object/archive format is not required for the working development loop.

## Toolchains and language runtimes

[Hosted toolchains and language runtimes](toolchains-and-runtimes.md) records
the chosen LLVM/Clang transition and hosted compiler direction, C++ userspace, a Go
cross compiler and later hosted Go tools. Rust is a separate possible direction.
Tailscale has a concrete future [homelab administration target](toolchains-and-runtimes.md#homelab-administration-over-tailscale);
Ladybird is a wilder, much later candidate. These
are parked alongside the ports, not extra tasks in the libc milestone.

## Lua follow-ups

The shared [C configuration helper](../userland/lua.md#embedding-and-session-configuration)
serves session and network settings. Further consumers keep their own schemas,
defaults and application policy; a generic schema framework is not needed.

Build scripting for [in-Pyxis development](in-pyxis-development.md#tooling)
and the implemented behavior are in the [runtime reference](../userland/lua.md).
Remaining interpreter work, each a separate slice: stdin scripts and consuming the
existing script capability for shebang launches; `file:setvbuf`; `os.clock`
(process CPU time); debug and the full math library. Dynamic modules, live
configuration reload and per-user/space settings policy also remain deferred.
None depends on virtio-fs.

Owner direction, 2026-10-13: allow bundle interpreters in shebangs (for example
`#!bin://shell.pxb`), so `home://post.pxs` directly receives declared interpreter
grants; Lua shares the gap. Today interpreters must be native executables.

## Networking and applications

The priority is making Pyxis useful for simple daily tasks; website hosting is an
eventual application, not the main project target. [Outbound TCP](../devices/tcp.md),
[UDP tools](../devices/networking.md#udp-tools), [DHCP](../devices/dhcp.md) and
[DNS with hostname ping](../userland/dns.md) are implemented, and
[lwIP](../devices/lwip.md) owns the TCP engine. Keep the
[user/authority checkpoint](users-and-authority.md) ahead of remotely accessible
services; server resource contracts need their own scope. Hosting the Pyxis
landing page remains an eventual application; revisit init supervision and restart
policies when defining that web-server milestone.

[Userspace URI scheme providers](userspace-scheme-providers.md) are a separate
future consumer: scoped kernel routing to userspace HTTP/HTTPS services, with
readable results usable by `fopen` and `cat`. A bounded LRU response cache and
remote compiler includes are later ideas, and the provider direction includes
[read-only SQLite views and query results](userspace-scheme-providers.md#sqlite-views-and-query-results).
Structured database sessions and an editor-based query worksheet are later
experiments. This adds no HTTP or TLS to the kernel.

## Device ownership and network domains

Future spaces could have exclusive devices or explicitly share the services
provided by a device. For example, one NIC could serve two or three workload
spaces while another serves a single space containing an isolated web-service
environment. This is a future direction, not an extension of the current driver
milestone or a commitment to particular objects/APIs.

Keep device ownership, service state and access grants distinct. One driver owns
a device's registers, queues, interrupts and reset. A proposed network domain
would own its interfaces, addresses, routes, connections, loopback and resource
budgets; spaces receive separate communication and configuration capabilities.
Several spaces could intentionally share one domain. An isolated domain should
have its own loopback, and dedicated hardware alone must not allow its workload
to exhaust shared kernel resources.

Sharing one physical NIC between isolated domains would need virtual interfaces
and explicit packet routing/filtering. Direct device programming by an untrusted
space would additionally need hardware DMA isolation. CPU placement and moving
work off the BSP are separate decisions from these ownership boundaries. Device
assignment/revocation, accounting, domain lifetime and sharing policy remain open.
Do not introduce placeholder structures or restructure current drivers for this.

## Multiple users and restricted permissions

Multi-user support is a requirement, with an earlier
[identity and authority design checkpoint](users-and-authority.md). Keep that
checkpoint ahead of persistent ownership and broader sharing decisions; do not
leave it as account UI to bolt on after those interfaces are fixed.

[Credentials and biometric unlock](credentials-and-biometrics.md) parks fingerprint
login and a Pyxis-wide credential store. The master key is sealed in the TPM under
a measured-boot policy, a password or PIN is the root factor, and biometrics only
gate use after a root-factor unlock. It follows local users, USB and a TPM driver.

## Developer tools

Owner ideas, 2026-10-07; none is scheduled.

- **Capability inspector.** A key combination opens a full-screen view of the
  focused program's capabilities: kinds, rights and names. Reading another
  program's capabilities is powerful, so the authority would itself be a grant,
  enabled for debugging through menuconfig. The kernel cannot start a program on
  a key press; a trusted helper in each space could receive the combination and
  show the view, possibly on its own [layer](../userland/space-layers.md).
- **top.** Live CPU use per CPU and per process, which would show the
  [SMP work](../kernel/smp.md) directly. It needs a new read-only kernel query
  for per-CPU busy and idle time and per-process CPU time, and a decision on
  which processes a caller may see.

- **Network debugger task 4.** Mutation and execution control (writes, software
  breakpoints, stepping, running break-in, detach and kill restoration), unassigned;
  see [decisions and unimplemented task 4](../kernel/network-debugger.md#decisions-and-unimplemented-task-4).

## Full-screen space

Owner idea, 2026-10-12; not scheduled. A key combination hides the
[space bar](../userland/init.md#space-bar) for the current space. It reappears
while the pointer is pushed past the top edge and hides again when the pointer
leaves. The motivation is recovering at least two text rows in the mux and
Neovim workflow.

## Test-run screen and idle blanking

Owner idea, 2026-10-12; not scheduled. During automated or remote test runs, blank
the display (black, or backlight or panel off if the display path supports it),
mainly to prevent OLED burn-in on the Steam Deck from a static shell and space bar
shown for hours, and to cut power and heat on the ThinkPad. There is no continuous
animation, because compositing during benchmarks adds measurement noise. An
optional marker is a very dark grey Pyxis logo moved rarely (about once a minute,
colour may change per move), so its redraw cost stays negligible. Any key
restores the real screen. The same mechanism could later become the general idle
screen saver or blanker.

## Audio

The [analog HDA engine](../devices/hda.md), [PCM sessions](../interfaces/audio.md)
and [master/per-space volume](../userland/audio-volume.md) are complete; the
[remaining limits](../technical-debt.md#hd-audio-volume-control) include media-key
input and the later native regression.

[SDL2 audio callbacks](../development/sdl2.md#audio) and the
[Chocolate Quake sound bundle](../userland/chocolate-quake.md) are delivered;
Chocolate Doom and EDuke32 sound, music and [SDL latency tuning](../technical-debt.md#sdl-audio-latency)
remain. Conversion and resampling belong in userspace. Recording, HDMI/DP, USB/dock
and Bluetooth audio remain later directions.

## Bluetooth

Accepted direction (owner, 2026-10-08): use the MX Master 3S on Pyxis through the
ThinkPad AX200 USB function (`8087:0029`). The
[completed investigation](../devices/ax200-bluetooth.md) reached a first
LE scan on warm-host passthrough. The [mouse proposal](bluetooth-mouse.md) records
kernel USB/Intel/HCI ownership and a trusted userspace upper stack, Secure
Connections enrollment, and cold initialization/durable bonds/native pointer
use required for closure. HID over GATT feeds the [system pointer](../interfaces/pointer.md#input-source-coordination)
with kernel-owned positions. Tasks 1 to 3 are complete; tasks 4 to 8 and later
policy rounds remain open and need explicit assignment.

## Wi-Fi on the ThinkPad and the Steam Deck

Owner idea, 2026-10-12; not scheduled. One shared 802.11 core (WPA2/WPA3
supplicant, connection management, 802.11-to-Ethernet frames) plus a small driver
per chip. Both targets look SoftMAC-class: the ThinkPad's Intel AX200 (iwlwifi-class,
[firmware already loaded for Bluetooth](../devices/ax200-bluetooth.md)) and the
Deck OLED's Qualcomm QCA2066 (ath11k-class). The chip identification and the
SoftMAC classification are from memory and need confirming against the inventories.

## Beyond x86: aarch64

Owner idea, 2026-10-13; not scheduled. QEMU's aarch64 `virt` machine would come
first (edk2 UEFI, GIC, PL011, virtio; LLVM and Limine support aarch64), then an
Apple M1 MacBook Air as hardware, probably 2027. It has no native UEFI, so it would
boot through Asahi's m1n1 and U-Boot, with Apple's AIC and DART; the Asahi docs
are the reference. The owner's second candidate is a Lenovo Chromebook Duet
(MediaTek MT8183, mainline Linux device tree, USI stylus); its firmware is coreboot
plus depthcharge with no UEFI, so it would need U-Boot as a UEFI layer or a
depthcharge payload path. The groundwork is the rule keeping hardware details in `arch/`
and Limine in its adapter. Review lens for current work: no x86-isms outside
`arch/`, and no ABI or format (P1F, syscalls) that bakes in x86.

## Clock source

TSC with extended-HPET fallback is [implemented](../kernel/timekeeping.md#tsc-selection)
([measurements](../development/experiments/tsc-clock/README.md)). KVM pvclock,
runtime source switching, suspend and resume and VM migration remain separate
scopes.

## Power and ACPI

Follow-ups to the completed [ACPI](../kernel/acpi.md) milestone. None is agreed.

- **Notifications instead of polling.** Battery and AC `Notify` calls from the
  embedded controller's query methods would refresh the reading at once, and
  `Notify(0x81)` would reread a battery's full capacity and cycle count, which
  are read only when it appears today. They reach only the trace log now.
- **Battery-aware Update.** The installer warns, or refuses, on battery below a
  threshold without AC, as firmware updaters do; the ThinkPad switched itself
  off on low battery just before a planned Update on 2026-10-07. It could also
  hold off power operations while it writes.
- **Power overlay.** Bounded output-lock waits in the presenter, so a stuck writer
  drops frames instead of stopping input (it changes ordinary presentation and
  needs a decision; see [power overlay limits](../technical-debt.md#power-overlay-limits)),
  a warning that RAM volumes are lost on shutdown from a live image, and other
  system-overlay entries such as a lock screen or space switch, only with users
  and sessions.
- **Low battery.** The widget changes style at a low level, and later a clean
  power-off runs automatically at a critical level, before the firmware cuts
  power.
- **More widgets.** A way for userspace services to publish short bounded widget
  text in the space bar. Pyxis's resolutions leave room for several; if there
  are ever too many, mouse support could show the less important ones on click
  (owner, 2026-10-07).
- **Later ACPI uses.** Lid and AC-adapter events, thermal zones, sleep, and
  control-method power buttons.
- **CPU frequency control.** Pyxis leaves P-states to the firmware. On battery
  the ThinkPad (Ryzen 5 PRO 4650U) drops to about 400 fps in Quake
  `timedemo demo1`, against 660–690 on AC, and console drawing slows. ACPI
  `_PSS`/`_PCT` or AMD CPPC could let Pyxis choose a policy. Nobody has
  measured whether OS control recovers any of it on battery.

## Persistent storage and installation

[npfs](../devices/filesystem-readonly.md) holds installed systems, the
[native installer](../userland/installer.md) installs and updates them, and
[Linux mounts](../development/npfs-linux-mount.md) read them on the host.
[Writable virtio-fs](../devices/virtio-fs.md) still serves port development.
Keep three choices separate: Pyxis file/directory capability requests, a backend
operation interface, and the disk format. A FUSE-inspired backend need not force
Linux FUSE's complete wire ABI, Unix permissions or path semantics on applications.

Ideas recorded when npfs was planned, none of them agreed:

- **A live-system install flow (owner preference).** Boot a usable live system,
  inspect disks and the network, then enter the installer, as Linux live images
  do, instead of rebooting into the separate "Install Pyxis" entry. This likely
  needs the new-space flow, and first a safe way to retire a mounted pool: today
  mounted pools stay retained until reboot, which prevents raw installation in
  the same boot.
- **Choosing inits at install time.** An interactive step that chooses which
  inits start which spaces.
- **Filesystem overlays,** for example a volume overlaid on the boot archive.
- **Writing from Linux.** The host mount is read-only; writing is a later step.
- **Machine settings that survive updates (owner, 2026-10-09).** Today the boot
  configuration, `boot://config/installed.lua`, is part of the boot archive, so
  changing a per-machine value means rebuilding the archive, and an update
  replaces it. The [machine settings proposal](machine-settings.md) develops a
  small record (hostname and similar) kept separate from the archive and carried
  across updates and reinstalls; its task 1 is implemented.
- **USB write throughput.** Extracting 280 MiB to the toolchain stick took 333 s
  (about 0.9 MB/s) in the native Clang session of 2026-10-13, far below what a USB
  stick sustains. It is the write-side counterpart of
  [bulk read aggregation](../devices/usb-storage.md#bulk-read-aggregation): writes are
  likely 4 KiB-sized, serialized BOT commands, now also without a device cache flush.
  It needs investigation and is on the owner's shortlist; nothing is assigned.
- **NVMe.** The [NVMe driver](../devices/nvme.md) is merged (#710) and read-only;
  native writes await qualification. With it, the Fedora disk would appear
  under Read the room as a foreign disk, which the owner accepted. The
  [coexistence proposal](nvme-coexistence.md) develops the driver, FAT32 and a
  non-destructive installer mode beside Fedora.

## Presentation timing for programs

Unassigned. Timing would reach programs through a native display observation and
wait: a geometry generation, a frame sequence, a monotonic timestamp and period, and
whether the source is software cadence, command completion or verified hardware
timing. Buffer retirement and display timing are different events, and there is no
SDL vsync claim without supported timing and no timer cap presented as vsync. Quake
could pace to display timing rather than its unrelated 72 Hz sleep, keeping timedemo
uncapped. Pending frames stay bounded, and nothing queues without limit, waits under
output locks or with IF=0, or changes hidden-space, focus or input semantics. One
1920x1080x4 frame is 7.91 MiB, about 498 MB/s of destination writes at 60 Hz before
source reads; damage-only copies need damage tracking for navigation, cursor, caret
and every source, which is later work. Qualify before and after on the same ThinkPad
mode, power and workload (composition and copy P50/P95/max, missed intervals,
application cadence, input latency); camera clips or owner observation, not FPS or
screenshots, decide visible tearing. See [presentation timing](../kernel/display.md#presentation-timing).

## Neovim follow-ups

Neovim is delivered ([reference](../userland/neovim.md)); each follow-up needs its own
proposal and the owner's go.

- **Swap and backup recovery** need a lease policy: the swap creator PID in file bytes
  must not become control authority, and `flock` is not a basic-editor prerequisite.
  Change detection by polling needs truthful metadata and timers.
- **Jobs and `system()`** consume the [native command core](../userland/native-jobs.md);
  descendant jobs need [nested execution groups](nested-execution-groups.md), and
  stopping one needs a cooperative-versus-forced policy over held capabilities, not
  numeric-PID killing.
- **Terminal input reporting** (xterm mouse modes 1000/1002/1006 and focus `?1004`) in
  TTY tabs, mux panes and the remote client, enabling `vim.opt.mouse` and
  `FocusGained`/`FocusLost`; the owner wants optional save on focus loss
  (`autocmd FocusLost * silent! wall`). Neither reporting path exists.
- **Richer `pyxis` profile:** advertise indexed and RGB colours and the text styles the
  terminals already render, then `termguicolors`, with explicit highlight groups. A
  Neovim Unicode profile is separate.
- **Netrw** and native scheme handling in runtime path helpers, including the
  `:checkhealth` frontend; **`:terminal`** (terminal sessions, child attachment, raw
  and cooked responsibilities, resize, hangup, flow control, with native launch
  replacing `forkpty`).
- **Threads and the libuv pool** need the [native threads](../interfaces/threads.md)
  consumer adapter; separate worker processes cannot replace them.
- **Dynamic Tree-sitter parsers, C Lua modules and LuaJIT** need relocatable code
  loading with permissions and executable-memory transitions; the seven bundled
  grammars are already static.
- **LSP and networking:** stdio language servers need jobs first, and TCP LSP needs
  listen and accept. clangd follows [native Clang](../development/hosted-tools.md).
  Compiler diagnostics as Neovim diagnostics, before clangd, is the owner's preference
  over jump-to-definition. Universal Ctags is parked (2026-10-10).
- **Scalable terminal fonts** (far-future, 2026-10-10): TTF, OTF or WOFF chosen by
  implementation ease, for example an stb_truetype-style rasterizer with a glyph
  cache. No font source, dependency or contract exists.

Long-term acceptance target (owner, 2026-10-10): the real `chronium/nvim-configs`
(lazy.nvim, Primeagen-style) should mostly work. The first set is `set.lua`, remaps,
undotree, a copied-in harpoon and tokyonight once the richer profile lands, with
explicit errors for unavailable features. Known gaps: lazy.nvim, fugitive and plugin
updates need [Git](git-on-pyxis.md) and jobs; `<leader>pv` needs netrw; `"+y` needs a
clipboard provider wired to the [typed clipboard](../interfaces/clipboard.md);
`:TSUpdate` is replaced by statically bundled parsers; Mason stays out because it
downloads Linux binaries; telescope needs jobs; tmux-sessionizer is replaced by the
mux. Replace the config's `os.getenv("HOME")` undo directory with
`vim.fn.stdpath("state") .. "/undo"` (`home://.local/state/nvim`), since stock launch
does not set `HOME`.

## Clipboard typed objects and converters

Proposed, not assigned (accepted direction in the [clipboard reference](../interfaces/clipboard.md#direction)).
An item would hold a MIME representation (canonical lower-case `type/subtype`, at most
127 bytes, no parameters) beside the mandatory UTF-8 text form, as bounded bytes or
as a read-only FILE capability. A label is descriptive, never proof of content or
authority, and a FILE handle's number cannot be pasted in text. Pasting an image whose
text form is `home://pictures/photo.png` into a terminal inserts only that string,
with no lookup, mount or authority; an image editor receives the object or an
attenuated READ grant to its frozen snapshot, never a source directory, WRITE,
launcher or namespace grant. Mutable or provider-backed data is copied into
store-owned immutable backing before publication (READ and MIME do not prove
immutability), a read of a concurrently modified file is refused, and only complete
validated staging becomes the current item. Proposed bounds, to be measured: 8 MiB per
item, 64 MiB retained globally and 16 MiB per space, 128 retained versions globally
and four per receiving space, charging logical bytes once globally and per retaining
space, never revoking an admitted snapshot.

Converters would be trusted, packaged handlers registered by trusted startup, exact
MIME matches preferred, one conversion step per request, found through no pasted
path, environment variable or untrusted namespace. A trusted coordinator gives each
only a READ input snapshot, private memory, a bounded result channel and the clock;
no store grant, destination input, roots, network, terminal or general launch. The
coordinator holds termination in an execution group, validates type and length,
applies a five-second deadline and owns the output; converters cannot replace the
current clipboard, a failed conversion preserves it, and a graphics request never
falls back to text. Hostile plugins, supervision and conversion caches are separate
work, and retained-result accounting must be settled before any converter code.

## Gallery

Owner request (queued, 2026-10-13), waiting on plain stdio HTTP submission
([parked 5c](native-http-requests.md#parked-tasks-5b-and-5c)): a clean Pyxis QEMU
screendump with Neovim on the left editing a short C version of
[`ci-jobs-server.py`](../../tools/demo/ci-jobs-server.py)'s existing
[gallery](../gallery/README.md) workflow and the shell on the right running one
`clang demo.c -o demo.pxe` and its output. It uses plain stdio (`fopen(uri, "w+")`,
`fputs`, `fflush`, `fgets`), posts a job, reads its reply, polls progress and lists
jobs. The owner coordinates readiness.

## References

- [Filesystem direction](vfs.md) and [space direction](spaces.md).
