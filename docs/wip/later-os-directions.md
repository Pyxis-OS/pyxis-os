# Later OS directions

Status: parked ideas, not an implementation milestone or worklist. Promote one
to a focused milestone when its prerequisites and intended result are clear.
See the [planning index](boot-sdk-ports.md) for the current sequence.

## Additional ports

Prefer Kilo first, then assess TCC for the development loop below. Guest Lua
can wait until useful, notably for system-wide configuration; retain
[its staged worklist](lua-port.md). Other candidates are SQLite, Doom, a CHIP-8
interpreter, Frotz and NetHack. This is not an instruction to port the whole list.
Neovim remains a later editor goal.

Kilo and TCC now have a concrete [edit/build/run milestone](edit-build-run.md):
write a hello-world program in Kilo, compile it with TCC and run it entirely
inside Pyxis. Evaluate direct PXE output without letting compiler internals
turn the milestone into an open-ended toolchain rewrite.

## Networking and website hosting

After virtio-fs, the next intended VirtIO driver is virtio-net, then virtio-blk.
Hosting the Pyxis landing page remains a release goal. Networking, the server
and their resource contracts need their own scoped plans. Revisit init
supervision and restart policies when defining the first web-server milestone.

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
