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
10. [PCI, VirtIO and a host filesystem mount](virtio-fs.md): expose host files
    through an init-managed read-only virtio-fs mount and native filesystem
    capabilities. The agreed Q35/ECAM, modern PCI, split-queue and BSP-worker
    direction is split into nine focused tasks. [PCI discovery and owned
    resources](../pci.md) are complete; VirtIO feature negotiation and queue
    inspection are next.

This focus order does not commit to working on the milestones together.
Ports depend on the SDK; init does not need the repository split. PCI/VirtIO
infrastructure can be developed independently, while its final mount setup uses
init. Each milestone should become several focused PRs where needed.

[Later directions](later-os-directions.md) park the remaining ports, networking,
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
An opt-in [host-backed development overlay](virtio-fs.md#follow-up-host-backed-development-overlay)
is a follow-up experiment after the plain mount works, not part of its completion
boundary or a replacement for the default boot archive.

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

- [Shell, filesystem and application runtime](../first-shell.md).
- [Earlier development candidates](development-paths.md).
- [Filesystem direction](../vfs.md) and [space direction](../spaces.md).
