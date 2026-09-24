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

A custom linker remains a possible later investigation alongside application
bundles/images. TCC already emits native P1F executables; a new linker or custom
object/archive format is not required for the working development loop.

## Lua follow-ups

When a second Lua configuration consumer appears, extract a small C library
from the session evaluator. Share source loading, the restricted Lua environment,
protected evaluation, diagnostics and state cleanup. Keep settings schemas,
defaults, validation and application policy with each consumer; do not introduce
a generic schema framework before there is a concrete need.

Further interpreter work includes a module search policy for pure-Lua `require`,
stdin scripts, and consuming the existing script capability for shebang launches.
Broader io/os, debug and full math libraries remain separate slices. The earlier
io/os audit identified pushback, temporary files, stream-buffer control, process
CPU time and calendar formatting/conversion as missing runtime pieces. Do not
substitute wall time for CPU time or add successful stubs for missing operations.
Dynamic modules, live configuration reload and per-user/space settings policy
also remain deferred. Existing initrd/RAM filesystems suffice for Lua scripts;
these tasks do not depend on virtio-fs.

## Networking and website hosting

The [initial networking milestone](initial-networking.md) covers loopback,
virtio-net, manually configured IPv4 and ping. DHCP follows through the same
configuration operations once UDP is available. TCP, DNS and server resource
contracts still need separate scopes. Hosting the Pyxis landing page remains a
release goal; revisit init supervision and restart policies when defining that
web-server milestone. Virtio-blk remains the next intended VirtIO storage driver.

[Userspace URI scheme providers](userspace-scheme-providers.md) are a separate
future consumer: scoped kernel routing to userspace HTTP/HTTPS services, with
readable results usable by `fopen` and `cat`. Unknown-length HTTP bodies are
initially rejected; a bounded LRU response cache and remote compiler includes
are later ideas. This does not add HTTP or TLS to the kernel.

## Multiple users and restricted permissions

Multi-user support is a requirement, with an earlier
[identity and authority design checkpoint](users-and-authority.md). Keep that
checkpoint ahead of persistent ownership and broader sharing decisions; do not
leave it as account UI to bolt on after those interfaces are fixed.

## Persistent storage and installation

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
