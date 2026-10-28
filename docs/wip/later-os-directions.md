# Later OS directions

Status: parked ideas, not an implementation milestone or worklist. Promote one
to a focused milestone when its prerequisites and intended result are clear.
See the [planning index](boot-sdk-ports.md) for the current sequence.

Parked elsewhere: [capability-valued environment variables](capabilities-and-providers.md)
and [subsystems per space](subsystems-per-space.md).

## Package manager

The native `pkg` client, data/program/development packages and installed command
views are delivered ([formats](../interfaces/packages.md)). The
[ABI/core/updater milestone](pkg-upgrade.md) records the core archive, online
Install and signed `pkg upgrade`: QEMU upgrade/new/previous/rescue boots are
qualified; the native ThinkPad upgrade check remains open. Dynamic linking and
the [source-build direction](source-builds.md) remain future work.
[Manual toolchain publication](../development/hosted-tools.md#manual-immutable-publication)
remains a recovery workflow.

Selecting existing build artifacts, so a developer can consume successful CI work
without finding and extracting each [bundle](../development/build-bundles.md), is
part of this direction (owner's call, 2026-10-14) rather than a separate task. CI
has uploaded no artifacts since 2026-10-20 (chronium), so this needs a published
source first. The rules worth keeping:

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

Upgrading the system through the package manager, without booting the installer
(owner idea, 2026-10-14), is implemented by `pkg upgrade` with the qualification
limits above. The installer retains [Update](../userland/system-updates.md).

Historical program-store direction (owner, 2026-10-17), before
[package upgrades](pkg-upgrade.md) removed the interim revision directories:
each program version is written once into a store (`bin/store/NAME-HASH/`), a
generation maps stable names to store entries, one atomic step switches the
current generation, and cleanup removes only store entries no kept generation
uses. Updates then write only changed programs and keep rollback. Whether a
generation is a directory of symbolic links or a manifest that boot init turns
into the read-only `bin://` view is open; the owner leans towards links. User
programs stay outside it, in a plain user command directory (see
[everyday gaps](everyday-gaps.md)), so installing one never needs a tool.
The implemented store/set and command-view contracts are now described by
[package formats](../interfaces/packages.md); the choices above preserve the
earlier proposal rather than reopen those decisions.

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
separate semantics. This preserves the [environment contract](../interfaces/environment.md).

Inside the kernel, consider small typed operation tables and explicit constructor
arguments where multiple implementations already justify them (file and directory
backends; network devices need packet-oriented contracts of their own). Userspace
providers implement message protocols, not kernel function tables. Define
buffer/object ownership, partial progress, completion, cancellation, sleeping and
executor constraints before extracting an interface, and leave room for
asynchronous completion without implementing it speculatively. Avoid a universal
object framework, inheritance hierarchy, reflection, automatic dependency graphs
or interface discovery. This note schedules no refactor and introduces no APIs.

## Command-resolution service

Direction stated by chronium (2026-10-23); a candidate for a later milestone,
not scheduled and not an interface proposal. It has higher priority than the
[cgo candidate](go-runtime.md#cgo-for-go-programs-that-wrap-c-libraries--candidate-chronium-2026-10-23).
Today the bundle-admission, grant and ABI policy of
[command search](../interfaces/command-search.md) is code inside libc's command
selection. Go cannot call it without cgo, so `exec.LookPath` is unsupported today
and the [Go runtime](go-runtime.md) would have to duplicate the policy or link C.

The direction: move that policy behind a typed native interface served by a
restartable userland service. libc and the Go runtime then share one
implementation, without cgo and without a second copy of the policy to keep in
step. The accepted, not yet implemented `exec.LookPath` contract
([decision 3](go-runtime.md#standard-library-adapter-delivery--decided)) stays in
force until chronium changes it.

- **Userland rather than kernel**, by chronium's preference: a crashed service is
  restarted instead of taking the system down. Restart and supervision belong to
  [Asterism](spaces.md#asterism).
- **Typed interface, not a file.** It moves Pyxis toward userland providers that
  expose typed interfaces beyond files, as in
  [capabilities and providers](capabilities-and-providers.md) (protocols, not
  providers) and the
  [backend interface note](#backend-interfaces-and-scoped-service-dependencies)
  (explicit wiring, a resolver that is itself a bounded capability).
- **Authority is unchanged.** The service resolves against a held command list
  and returns what the caller's own list admits; it holds no ambient search path.
- **Cost.** A call replaces a library call. Acceptable if the overhead is
  measured and does not matter; the measurement comes before any commitment.

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
[prepared HTTP requests](../interfaces/transactions.md)
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
queue; this is not an instruction to port the whole list. Neovim is delivered
([reference](../userland/neovim.md)). GrafX2 is another candidate, alongside the
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
bundles/images. LLD already emits native P1F executables; a new linker or custom
object/archive format is not required for the working development loop.

**Cheaper Quake scaling.** On vela, `timedemo demo1` ran at 194 fps on battery
and 197 on AC (2880x1800, trace build of `2cadd127`). `QG_DrawFrame` expands
each pixel `scale` times per row in plain C, and the presenter then copies the
whole slot to the firmware framebuffer. Writing each expanded row once and
copying it for the repeats, or scaling in the presenter, would cut one pass.
Timedemo results after such a change measure different port code, so they are
not comparable with earlier figures.

## Toolchains and language runtimes

[Hosted toolchains and language runtimes](toolchains-and-runtimes.md) records
the chosen LLVM/Clang transition and hosted compiler direction, C++ userspace, a Go
cross compiler and later hosted Go tools. [Rust stage 1](rust.md) delivers
core+alloc and tiny-skia; the [std milestone](rust-std.md#qualified-delivery)
records the fork compiler, std main and read-only-file hexyl subset. The complete
std profile, Rust worker integration and native hardware qualification remain
separate gates; hosted Rust remains later work.
Tailscale has a concrete future [homelab administration target](toolchains-and-runtimes.md#homelab-administration-over-tailscale);
Ladybird is the first planned full web browser (owner, 2026-10-16), much later:
it needs hosted C++ tools, capability pipes for its multi-process design, its
graphics, font and image libraries, and a native frontend; check its current
Swift dependency before planning. Mirroring LLVM on the Forgejo instance is an
open question for later. These
are parked alongside the ports, not extra tasks in the libc milestone.

## Lua follow-ups

The shared [C configuration helper](../userland/lua.md#embedding-and-session-configuration)
serves session and network settings. Further consumers keep their own schemas,
defaults and application policy; a generic schema framework is not needed.

Build scripting for [in-Pyxis development](in-pyxis-development.md#tooling)
and the implemented behavior are in the [runtime reference](../userland/lua.md).
Remaining interpreter directions include `os.clock` (process CPU time), dynamic
modules, live configuration reload and per-user/space settings policy. Native
stdin/shebang loading and the upstream library profile are implemented; see
[Lua](../userland/lua.md) and [script authority](../userland/script-authority.md).
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

[Userspace URI scheme providers](capabilities-and-providers.md) are a separate
future consumer: scoped kernel routing to userspace HTTP/HTTPS services, with
readable results usable by `fopen` and `cat`. A bounded LRU response cache and
remote compiler includes are later ideas, and the provider direction includes
[read-only SQLite views and query results](capabilities-and-providers.md#sqlite-views-and-query-results).
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
- **Explicit binding overrides for testing (owner, 2026-10-15).** Programs get
  their clock, files and network as capabilities, so a test can hand a child a
  fake clock, an in-memory volume or a recorded HTTP provider. Automatic bindings
  such as the clock cannot be shadowed by a shell variable of the same name
  ([shell binding rule](../userland/shell.md#typed-values-and-command-sequences)); an override would be a declared
  launch profile or grant, visible like any other authority. A mocked clock
  would let one program be tested across time zones, DST transitions and year or
  leap-day boundaries without affecting any other program (owner, 2026-10-16);
  the clock and the time-zone data could be replaced independently.
- **Config override warnings (owner, 2026-10-16).** `config validate` should warn
  when a `system://config/boot.lua` space that replaces a built-in space by name
  drops a grant the current built-in declares (0.0.6's HTTP staging grant was
  the first case). Today it reports "no selected ESP view" and checks only the
  archive defaults.
- **Pyxis MCP for automated testing (owner, 2026-10-16).** First a host-side
  MCP server on horse over the existing tooling (`pyxis-remote --machine`, PXE
  staging, stick deployment, remote reboot, QEMU launch), whose tools carry the
  native-run safety rules instead of each brief. Later, perhaps, a native server
  whose tools are capabilities granted only to Remote or test spaces. Scope to be
  discussed; it is agent tooling, not a test framework.
- **Scoped build environments (parked idea, chronium, 2026-10-22): good, but not
  now; authorizes no code.**
  - *Today (decided 2026-10-22).* `sdk://` is the normal default include and
    library path, over the base SDK plus every installed `-dev` package.
    `pkg-config` supplies flags and libraries elsewhere.
  - *The idea.* A build sees an `sdk://` view composed only from the `-dev`
    packages it selected. A space's configuration lists the defaults, and the
    shell can add a package for its session. Dependencies come along
    automatically: `raylib-dev` brings `sdl2-dev`. The shell passes the composed
    view to compilers as a capability, not a path string (see the
    [process environment](../interfaces/environment.md)).
  - *Effect.* Inside the selection everything stays easy: `#include <raylib.h>`,
    `-lraylib -lSDL2`, and `pkgconf` sees only the selected `.pc` files. Outside
    it, a header or library is simply not found. That surfaces undeclared
    dependencies and makes builds reproducible.
  - *Why not now.* It adds a selection step to everyday development, which
    conflicts with Pyxis not being a difficult OS. Revisit when reproducible or
    hermetic builds become a need, for example with
    [nested execution groups](nested-execution-groups.md), native package builds
    or CI on Pyxis. Package versions and upgrades are in
    [`pkg upgrade`](pkg-upgrade.md).

## Full-screen space

Owner idea, 2026-10-12; not scheduled. A key combination hides the
[space bar](../userland/init.md#space-bar) for the current space. It reappears
while the pointer is pushed past the top edge and hides again when the pointer
leaves. The motivation is recovering at least two text rows in the mux and
Neovim workflow.

## Test-run screen and idle blanking

Owner idea, 2026-10-12. Implemented by #886: [idle blanking](../kernel/display.md#idle-blanking)
blanks after local idle time, and test packets use `display.idle_blank=0`.
Still open: backlight or panel power off where a display driver supports it,
and the optional marker, a very dark grey Pyxis logo moved about once a minute so
its redraw cost stays negligible.

## Touchpad report rate on the ThinkPad

Owner request, 2026-10-15; not scheduled. The ThinkPad's Synaptics pad already
runs in high-rate mode, and 80 packets per second is the PS/2 limit. Linux
reaches a higher rate on these pads by switching them to RMI4 over SMBus
(Synaptics InterTouch), which would need an SMBus host controller driver and an
RMI4 protocol layer feeding the same touchpad filter.

## Touchpad momentum scrolling

Owner idea, 2026-10-16; not scheduled. Inertial scrolling after a two-finger
flick, applied only after the fingers lift so live input gains no delay. Tuning
needs a small program with live-editable curves (acceleration, friction,
velocity threshold) beside an endless scrolling text, so the parameters belong
in runtime settings like `input.touchpad_speed`, not compiled constants.

## Low-end test machine

Owner plan, 2026-10-16, for a month or two later: 2–4 cores, 4–8 GB RAM, a SATA
SSD and a TPM (required for the [credentials direction](credentials-and-biometrics.md),
preferably discrete rather than firmware). Candidate: HP EliteBook 830 G5 with
an M.2 SATA SSD, whose discrete TPM is to be confirmed. The Steam Deck already
covers low display resolution.

## Audio

The [analog HDA engine](../devices/hda.md), [PCM sessions](../interfaces/audio.md)
and [master/per-space volume](../userland/audio-volume.md) are complete; the
[remaining limits](https://git.internal/PyxisOS/pyxis-os/issues/1388) include media-key
input and the later native regression.

[SDL2 audio callbacks](../development/sdl2.md#audio) and the
[Chocolate Quake sound bundle](../userland/chocolate-quake.md) are delivered;
Chocolate Doom and EDuke32 sound, music and [SDL latency tuning](https://git.internal/PyxisOS/pyxis-os/issues/1397)
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

## Wi-Fi

The owner assigned the [Wi-Fi milestone](wifi.md) on 2026-10-18.
[AX200 stages 1–4](../devices/ax200-wifi.md) deliver firmware, passive scan,
legacy 2.4 GHz open and WPA2-Personal/CCMP links, independent DHCP and verified
data with recorded qualification limits. Next are vela RTL8922AE and WPA3;
AX200 5 GHz and the Deck QCA2066 remain deferred. These results do not qualify
the RTL8922AE backend or broader radio coverage.

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
  needs a decision; see [power overlay limits](https://git.internal/PyxisOS/pyxis-os/issues/1323)),
  a warning that RAM volumes are lost on shutdown from a live image, and other
  system-overlay entries such as a lock screen or space switch, only with users
  and sessions.
- **Low battery** (owner's direction, 2026-10-17; not scheduled). On battery:
  - below 10% the battery widget gains an exclamation mark;
  - below 5% it blinks slowly, about once a second (calm, and kind to OLED);
  - at 3% a full-screen overlay counts down about 30 seconds ("Battery critical,
    shutting down, plug in to cancel"), then runs the same flush-and-power-off
    path as the [power overlay](../kernel/power-overlay.md)'s Shut down;
  - starting Pyxis below 3% shows a full-screen overlay with a large blinking
    battery icon; plugging in continues the boot, otherwise it powers off after
    about 10–15 seconds.

  AC power overrides all of it: no blinking, shutdown or boot block while
  plugged in, and plugging in cancels a countdown. With no battery or an
  unreadable one nothing happens, the opposite of the install gate, because a
  false trigger would power off a working machine. The fixed thresholds are the
  default; the firmware's own warning and low levels (`_BIF`/`_BIX`) could later
  serve as a floor.

  Owner idea, 2026-10-18; not scheduled: show estimated time-to-empty and
  time-to-full, for example in the space bar or a userspace power query. This
  is userspace math: the [ACPI worker](../kernel/acpi.md#embedded-controller-and-battery)
  already publishes per-battery `_BST`, including present rate, through the
  [`system_info` power queries](../interfaces/system-information.md#power-and-batteries).
  Estimate discharge time as remaining capacity / rate, and charge time as
  (full capacity - remaining capacity) / rate, using matching capacity/rate
  units and smoothing. A zero or unknown (`0xFFFFFFFF`) rate, or unknown
  required capacity, gives an unknown estimate.
- **More widgets.** A way for userspace services to publish short bounded widget
  text in the space bar. Pyxis's resolutions leave room for several; if there
  are ever too many, mouse support could show the less important ones on click
  (owner, 2026-10-07).
- **Later ACPI uses.** Lid and AC-adapter events, thermal zones, sleep, and
  control-method power-button routes beyond the implemented `PNP0C0C` and
  `INTC10CC` notifications ([ACPI](../kernel/acpi.md)).
- **CPU frequency control.** Pyxis leaves P-states to the firmware. On battery
  the ThinkPad (Ryzen 5 PRO 4650U) drops to about 400 fps in Quake
  `timedemo demo1`, against 660–690 on AC, and console drawing slows. ACPI
  `_PSS`/`_PCT` or AMD CPPC could let Pyxis choose a policy. Nobody has
  measured whether OS control recovers any of it on battery.

## ACPI table export

Accepted by the owner on 2026-10-15 (PR #898) and implemented. The kernel's
read-only `acpi_tables` root, its names, copies, withheld `MSDM` and the
`acpi_tables = true` grant are described in [table export](../kernel/acpi.md#table-export)
and [acpidump](../userland/acpidump.md); the uACPI reset and interrupts-off
copy limits are in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1320).

## Persistent storage and installation

[npfs](../devices/filesystem-readonly.md) holds installed systems, the
[native installer](../userland/installer.md) installs and updates them, and
[Linux mounts](../development/npfs-linux-mount.md) read them on the host.
[Writable virtio-fs](../devices/virtio-fs.md) still serves port development.
Keep three choices separate: Pyxis file/directory capability requests, a backend
operation interface, and the disk format. A FUSE-inspired backend need not force
Linux FUSE's complete wire ABI, Unix permissions or path semantics on applications.

Symbolic links (owner, 2026-10-17): an npfs link type with `ln -s` and
`readlink`, resolved in the kernel's directory walk only beneath the root that
holds the link and with the opener's own capability, never escaping to another
root or scheme. Any link text can be stored, so [Git](../userland/git.md) can keep a
repository's symlinks instead of refusing them; a target outside the root just
does not resolve. Uses: stable program names for the [program store](#package-manager) and
personal shortcuts such as `home://bin/cc`.

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
  Owner, 2026-10-15: use them to remove the `boot://` versus `system://`/`bin://`
  disparity. Installed systems keep two copies of the system programs (the boot
  archive and the pool), which makes it unclear which one runs: run sheets spell
  `boot://NAME.pxe` to get the deployed version, and script parity trusts only
  `#!boot://shell.pxe`. One overlaid view would leave a single name per program.
- **Writing from Linux.** The host mount is read-only; writing is a later step.
- **Machine settings that survive updates (owner, 2026-10-09).** At the time the
  boot configuration, `boot://config/installed.lua`, was part of the boot archive,
  so changing a per-machine value meant rebuilding the archive, and an update
  replaced it. Installed boots now read `system://config/` on the pool. The [machine settings proposal](machine-settings.md) develops a
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
  under Any disk (formerly Read the room) as a foreign disk, which the owner accepted. The
  [coexistence proposal](nvme-coexistence.md) develops the driver, FAT32 and a
  non-destructive installer mode beside Fedora.

Boot payload ideas (owner, 2026-10-15), compared with other systems' updates:

- **Keep-N cleanup of boot payload attempts.** Like Fedora's
  `installonly_limit`: keep the last few `EFI/pyxis/<revision>/<attempt>/`
  payloads and remove older owned ones, never one an entry still references.
  Coexistence task 3 records the missing cleanup as technical debt.
- **Boot counting with automatic fallback.** Like systemd-boot's boot assessment
  or ChromeOS's A/B tries: a new entry gets a few tries and is marked good only
  after a successful boot; otherwise the loader falls back to the previous one.

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
- **Detached and PTY jobs.** External jobs, `system()` and `:make` are delivered
  ([jobs](../userland/neovim.md#external-jobs-and-shell-commands)); `pty` and `detach`
  are refused. Detached jobs need a lifetime policy beyond the editor's, and stopping
  one needs a cooperative-versus-forced policy over held capabilities, not
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
- **LSP over TCP** needs listen and accept; stdio servers such as
  [clangd](../userland/clangd.md) work. Universal Ctags is parked (2026-10-10).
- **LuaLS extensions**: the [native stdio profile](../userland/lua-language-server.md)
  is delivered and qualified on vela. Formatter, native watchers and
  TCP/subprocess modes remain outside that profile; extensions need their own
  scoped task and the corresponding native interfaces.
- **Scalable terminal fonts** (far-future, 2026-10-10): TTF, OTF or WOFF chosen by
  implementation ease, for example an stb_truetype-style rasterizer with a glyph
  cache. The [userland font library](../userland/fonts.md) is planned separately;
  the terminal keeps its bitmap font until this direction is taken up.

Long-term acceptance target (owner, 2026-10-10): the real `chronium/nvim-configs`
(lazy.nvim, Primeagen-style) should mostly work. The first set is `set.lua`, remaps,
undotree, a copied-in harpoon and tokyonight once the richer profile lands, with
explicit errors for unavailable features. Known gaps: lazy.nvim, fugitive and plugin
updates need [Git](../userland/git.md) and jobs; `<leader>pv` needs netrw; `"+y` needs a
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

Owner request (queued, 2026-10-13), using the delivered
[plain stdio HTTP submission](../userland/stdio.md#http-resources): a clean Pyxis QEMU
screendump with Neovim on the left editing a short C version of
[`ci-jobs-server.py`](../../tools/demo/ci-jobs-server.py)'s existing
[gallery](../gallery/README.md) workflow and the shell on the right running one
`clang demo.c -o demo.pxe` and its output. It uses plain stdio (`fopen(uri, "w+")`,
`fputs`, `fflush`, `fgets`), posts a job, reads its reply, polls progress and lists
jobs. The owner coordinates readiness.

## References

- [Filesystem direction](vfs.md) and [space direction](spaces.md).
