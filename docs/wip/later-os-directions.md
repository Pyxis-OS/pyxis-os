# Later OS directions

Status: parked ideas, not an implementation milestone or worklist. Promote one
to a focused milestone when its prerequisites and intended result are clear.
See the [planning index](boot-sdk-ports.md) for the current sequence.

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

Agreed direction to revisit later, not an implementation task. The
[native filesystem writer](../devices/filesystem-native-adapter.md) is complete;
the [runtime SMP and independent spaces milestone](../kernel/smp.md) completed
on 2026-10-06. This idea adds no prerequisites or tasks to either.

Borrow explicit dependency wiring and scoped resolution from inversion of control
and dependency injection. Launchers already supply capabilities, and FILE and
DIRECTORY already define message contracts. HTTP/HTTPS providers implement FILE
in userspace. Kernel filesystem wrappers still select backends through explicit
backing kinds; a later implementation interface could reduce that repeated dispatch.

Keep these responsibilities distinct:

- Capabilities identify held resources and granted authority.
- Protocols define operations, message layouts and result semantics.
- Backend interfaces connect those operations to concrete implementations.
- Namespaces and scoped resolvers select resources and return bounded grants.

Prefer direct dependencies for required services: a filesystem adapter receives
its block device; an HTTP provider receives networking, clock and trust resources.
Pass a resolver when dynamic discovery is actually needed. The resolver itself
must be a capability with bounded scope and delegation rights, never an ambient
machine-wide service locator. Different spaces may resolve the same name to
different providers. Supporting a protocol does not grant authority to invoke it.

Init is a natural place to start providers, choose implementations, bind names and
delegate dependencies to the session. Replacing a binding affects future resolution;
existing handles retain their established identity and lifetime. Revocation and
replacement of held authority require separate explicit semantics. This preserves
the existing [namespace contract](../interfaces/namespaces.md).

Inside the kernel, consider small typed operation tables and explicit constructor
arguments where multiple implementations already justify them. File and directory
backends are candidates; network devices need packet-oriented contracts of their
own. Userspace providers implement message protocols, not kernel function tables.
Define buffer/object ownership, partial progress, completion, cancellation, sleeping
and executor constraints before extracting an interface. Preserve worker ownership
and leave room for asynchronous completion without implementing it speculatively.

Avoid a universal object framework, inheritance hierarchy, reflection, automatic
dependency graphs or interface discovery machinery. Revisit concrete filesystem
backend duplication or additional network drivers when that work is selected;
the present note does not schedule a refactor or introduce APIs.

## Additional ports

Kilo and TCC provide the [edit/build/run workflow](../development/edit-build-run.md).
[Guest Lua](../userland/lua.md) now supplies scripts, a REPL and session configuration. The [Doom port](../userland/doom.md) provides initial
gameplay and demo playback. The [application and library port candidates](application-ports.md)
record SQLite, zlib/libpng, SDL2, PDCurses, Mbed TLS, text utilities, awk, jq,
Quake, DevilutionX, the C AbyssEngine investigation, a CHIP-8 interpreter,
Frotz and NetHack. This is not an instruction to port the whole list. Neovim
remains a later editor goal.

GrafX2 is another candidate, alongside the [desktop and graphics direction](desktop-graphics.md).
That draft records the global menu/dock/desktop ideas, software-rendering option
and eventual compositor prerequisites; it does not authorize implementation.

[retawq](https://retawq.sourceforge.net/) is a future text-browser port candidate,
starting with HTTP and leaving TLS optional. Terminal support needs a concrete
choice: port a curses library, or evaluate its built-in `bicurses` backend before
committing to ncurses. Upstream documents `--set-tg=bicurses` (terminfo-based,
without color or mouse), optional TLS and `--set-threading=0`; see the
[build options](https://retawq.sourceforge.net/docu/ctconfig.html). These options
are leads for a compile probe, not a confirmed Pyxis port path. Audit terminal,
libc and event/I/O requirements alongside the future TCP and name-resolution
interfaces. Keep HTTP/TLS in userspace; this does not authorize starting the port
or changing the current milestone order.

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
now serves session and network settings. Further consumers should keep their own
schemas, defaults and application policy; a generic schema framework is not needed.

Build scripting for [in-Pyxis development](in-pyxis-development.md#4-lua-for-build-scripts)
now has `io`, bounded `os`, pure-Lua `require` and a native module for running
programs, listing directories and hashing files. Libc supplies pushback,
exclusive temporary files and C-locale calendar formatting. The
[runtime reference](../userland/lua.md) records implemented behavior and absent
functions.

Further interpreter work includes stdin scripts and consuming the existing
script capability for shebang launches. Stream-buffer controls, process CPU
time, reverse calendar conversion, debug and full math remain separate slices.
Dynamic modules, live configuration reload and per-user/space settings policy
also remain deferred. Existing initrd/RAM filesystems suffice for Lua scripts;
these tasks do not depend on virtio-fs.

## Networking and applications

[Outbound TCP](../devices/tcp.md) is implemented, including native tcp and ttcp tools.
The priority is making Pyxis useful for simple daily tasks; website hosting is
an eventual application, not the main project target.

The completed [initial networking milestone](../devices/networking.md) provides loopback,
virtio-net, manually configured IPv4 and ping. Native
[UDP endpoints and tools](../devices/networking.md#udp-tools) now support bounded loopback
and host exchanges. DHCP follows through the same
configuration operations once broadcast handling and lease deadlines are
available. [DNS queries and hostname ping](../userland/dns.md) are complete. The
[lwIP integration](../devices/lwip.md) owns the TCP engine; keep the
[user/authority checkpoint](users-and-authority.md) ahead of remotely accessible
services. Server resource contracts need their own scope. Hosting the Pyxis
landing page remains an eventual application; revisit init supervision and
restart policies when defining that web-server milestone. Virtio-blk remains
the next intended VirtIO storage driver.

[Userspace URI scheme providers](userspace-scheme-providers.md) are a separate
future consumer: scoped kernel routing to userspace HTTP/HTTPS services, with
readable results usable by `fopen` and `cat`. Responses may omit an upfront
length: the provider completes a bounded download before returning a sized file.
A bounded LRU response cache and remote compiler includes are later ideas.
This does not add HTTP or TLS to the kernel.

The same provider direction now includes [read-only SQLite views and query results](userspace-scheme-providers.md#sqlite-views-and-query-results).
Structured database sessions and an editor-based query worksheet are later
experiments, after the SQLite port and provider contract exist.

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

## Audio

The [analog HDA engine](../devices/hda.md) and [PCM sessions](../interfaces/audio.md)
closed 2026-10-09 with native AMD/ALC257 speaker/headphone tones and an
error-free eleven-minute eight-session silent run. No volume control exists yet;
[volume and other limits](../technical-debt.md#hd-audio-volume-control) remain debt.

SDL2 and Quake sound are separate consumer assignments. Conversion/resampling
belongs in userspace; Quake needs its real mixer and native producer adapter.
SDL2 needs real audio callback/thread execution under the
[threads direction](scheduling-and-threads.md); today its port omits audio devices
and threads. Recording, HDMI/DP, USB/dock and Bluetooth audio remain later directions.

## Bluetooth

Accepted direction (owner, 2026-10-08): use the MX Master 3S on Pyxis through the
ThinkPad AX200 USB function (`8087:0029`). The
[completed investigation](../devices/ax200-bluetooth.md) reached a first
LE scan on warm-host passthrough. The [mouse proposal](bluetooth-mouse.md) records
kernel USB/Intel/HCI ownership and a trusted userspace upper stack, Secure
Connections enrollment, and cold initialization/durable bonds/native pointer
use required for closure. HID over GATT feeds the [system pointer](../interfaces/pointer.md#input-source-coordination)
with kernel-owned positions. Later policy rounds remain open; no implementation
task or USB HID work is authorized by this direction.

## Clock source

TSC with extended-HPET fallback is
[implemented](../kernel/timekeeping.md#tsc-selection) as of 2026-10-09. KVM
pvclock, runtime source switching, suspend and resume and VM migration remain
separate scopes. The
[investigation](https://git.internal/PyxisOS/pyxis-os/src/commit/93851aebce74c71ceea93774c4d97e01bc2a60e7/docs/wip/thinkpad-kvm-tsc.md#what-caelum-must-establish-for-tsc)
records the original requirements and references, and
[cheaper timekeeping](cheaper-timekeeping.md) the plan that delivered it.

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
- **NVMe.** Installing onto the ThinkPad's internal disk needs an NVMe driver.
  With it, the Fedora disk would appear under Read the room as a foreign disk,
  which the owner accepted.

## References

- [Filesystem direction](vfs.md) and [space direction](spaces.md).
