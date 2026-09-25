# Later OS directions

Status: parked ideas, not an implementation milestone or worklist. Promote one
to a focused milestone when its prerequisites and intended result are clear.
See the [planning index](boot-sdk-ports.md) for the current sequence.

## Execution lifecycle

The implemented [init handoff](../init.md) launches a successor and exits. Real
process replacement (`exec`) still needs its own resource and failure contract.
Init supervision and restart policies remain deferred to the first web-server
milestone; this does not commit to Unix PID 1 semantics.

## Additional ports

Kilo and TCC provide the [edit/build/run workflow](../edit-build-run.md).
[Guest Lua](../lua.md) now supplies scripts, a REPL and session configuration. The [Doom port](../doom.md) provides initial
gameplay and demo playback. Other candidates are SQLite, a CHIP-8 interpreter,
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

## Lua follow-ups

The shared [C configuration helper](../lua.md#embedding-and-session-configuration)
now serves session and network settings. Further consumers should keep their own
schemas, defaults and application policy; a generic schema framework is not needed.

Further interpreter work includes a module search policy for pure-Lua `require`,
stdin scripts, and consuming the existing script capability for shebang launches.
Broader io/os, debug and full math libraries remain separate slices. The earlier
io/os audit identified pushback, temporary files, stream-buffer control, process
CPU time and calendar formatting/conversion as missing runtime pieces. Do not
substitute wall time for CPU time or add successful stubs for missing operations.
Dynamic modules, live configuration reload and per-user/space settings policy
also remain deferred. Existing initrd/RAM filesystems suffice for Lua scripts;
these tasks do not depend on virtio-fs.

## Networking and applications

[Outbound TCP](../tcp.md) is implemented, including native tcp and ttcp tools.
The priority is making Pyxis useful for simple daily tasks; website hosting is
an eventual application, not the main project target.

The completed [initial networking milestone](../networking.md) provides loopback,
virtio-net, manually configured IPv4 and ping. Native
[UDP endpoints and tools](../networking.md#udp-tools) now support bounded loopback
and host exchanges. DHCP follows through the same
configuration operations once broadcast handling and lease deadlines are
available. [DNS queries and hostname ping](../dns.md) are complete. The
[lwIP integration](../lwip.md) owns the TCP engine; keep the
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

## Persistent storage and installation

[Writable virtio-fs](writable-virtio-fs.md) is the next selected milestone after
outbound TCP and per-CPU init. It will let the Kilo/TCC workflow keep source and
executables across boots without first choosing a disk filesystem. Trusted init
selects access grants over one host-service identity; the
[users/authority checkpoint](users-and-authority.md) records what that prototype
boundary leaves open. Space titles follow in a separate small PR.

Keep three choices separate: Pyxis file/directory capability requests, a backend
operation interface, and the disk format. A FUSE-inspired backend need not force
Linux FUSE's complete wire ABI, Unix permissions or path semantics on applications.
If a custom disk format is selected, a freestanding format implementation could
be shared by a Caelum adapter and a Linux FUSE adapter. The host and kernel sides
would supply their own I/O/allocation glue. The native filesystem need not run in
userspace to enable host mounting.

Defer existing-versus-custom disk format selection and installer design until
there are block I/O and concrete persistence requirements. Virtio-fs can support
port development while those decisions remain open.

## References

- [Broader development candidates](development-paths.md).
- [Filesystem direction](../vfs.md) and [space direction](../spaces.md).
