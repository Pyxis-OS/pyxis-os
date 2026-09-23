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
   through a small host Lua runner. Lua is the first port, divided into
   [runtime audit, execution and script/REPL milestones](lua-port.md).
4. [PCI, VirtIO and a host filesystem mount](virtio-fs.md): expose host files
   through an init-managed virtio-fs mount and native filesystem capabilities.

This focus order is proposed, not a commitment to work on all four together.
Ports depend on the SDK; init does not need the repository split. PCI/VirtIO
infrastructure can be developed independently, while its final mount setup uses
init. Each milestone should become several focused PRs where needed.

[Later directions](later-os-directions.md) park the remaining ports, networking,
website hosting, block storage, filesystem-format choices and an installer.
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

## Small independent shell follow-ups

The unconditional newline after every foreground child is annoying. Replace it
with starting the next prompt on a fresh line only when needed. Preserve child
output that ends mid-line: libterm currently clears the prompt row. The terminal
owns cursor state, so guessing from the shell's own writes is insufficient.
The specific query or terminal operation remains to be selected separately.

Show the current working path in the prompt, including its scheme, for example
`home://projects> `. Keep it consistent with successful directory changes and
the initial working directory. The display string does not replace the retained
directory capabilities used for lookup. Settle long-path display and line-editor
interaction when implementing it.

These can be focused changes; neither needs to wait for the init milestone.

## Existing context

- [Completed first-shell milestone](first-shell.md).
- [Earlier development candidates](development-paths.md).
- [Filesystem direction](../vfs.md) and [space direction](../spaces.md).
