# Development milestone index

Status: working discussion after the first-shell milestone. These documents
separate the next concrete results from parked ideas; they do not authorize
implementation. The initial scope decisions are recorded; resolve each
milestone's remaining interface details before starting its code work.

## Suggested focus order

1. [Selected init and primitive scripts](init-and-scripts.md): boot a native or
   shebang init, perform setup and hand off to a working interactive shell.
2. [SDK and repository separation](sdk-and-repositories.md): export the runtime
   contract, keep the compiler prebuilt, and move userspace into its own pinned
   repository while preserving the integrated build.
3. [Port recipes and one first port](ports-and-first-port.md): consume that SDK
   through a small host Lua runner. Prefer Kilo as the first guest port after
   its requirements audit, then investigate TCC for the edit/build/run loop.
4. [PCI, VirtIO and a host filesystem mount](virtio-fs.md): expose host files
   through an init-managed virtio-fs mount and native filesystem capabilities.

This focus order is proposed, not a commitment to work on all four together.
Ports depend on the SDK; init does not need the repository split. PCI/VirtIO
infrastructure can be developed independently, while its final mount setup uses
init. Each milestone should become several focused PRs where needed.

[Later directions](later-os-directions.md) park the remaining ports, networking,
website hosting, block storage, filesystem-format choices and an installer.
The later [edit/build/run milestone](edit-build-run.md) gives Kilo and TCC a
concrete goal: write C in Pyxis, compile it there and run the resulting program.
It includes a bounded investigation of native TCC PXE output. Guest Lua is
deferred until useful, notably for system-wide configuration; its
[staged worklist](lua-port.md) is retained. Host Lua recipes do not depend on it.

VirtIO driver order is agreed: virtio-fs, then virtio-net, then virtio-blk.

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
- The first host mount is read-only `host://`, mounted by init before launching
  the shell and passed to the session as a directory capability.
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

- [Completed first-shell milestone](first-shell.md).
- [Earlier development candidates](development-paths.md).
- [Filesystem direction](../vfs.md) and [space direction](../spaces.md).
