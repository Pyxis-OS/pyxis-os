# RAM filesystem mutation and Doom saves

Agreed milestone: round out everyday mutations on the existing capability-based
RAM filesystem, then enable Doom save/load. This does not introduce persistent
storage, a filesystem server, a mount framework or POSIX kernel interfaces.

## Focused PRs

- [x] Removal: independent directory REMOVE authority, file/empty-directory
  removal, safe enumeration, native path helpers, libc remove and rm/rmdir.
- [ ] Atomic file rename/replacement: native calls, path helpers, libc rename
  and file-only mv. Failure preserves both entries. Support moves between RAM
  directories, with explicit replace/no-replace policy; never silently fall
  back to copy-and-delete across filesystems.
- [ ] Doom save/load: save menus, quicksave/quickload and loading after process
  restart. Write/close a temporary save and atomically replace the previous
  save; preserve the old save on failure and report the error honestly.

Each PR updates this checklist and the implemented interface documentation.
Validate with ordinary builds, manual QEMU use and debugger inspection.

## Agreed contracts

Removal needs REMOVE on the parent, not READ/WRITE on the child. Existing file
handles survive removal. Empty directories can be detached while open, but
cannot acquire new children afterward. Nonempty removal fails. Enumeration
retains its generation-based CHANGED outcome with safe name lifetimes.

Rename needs REMOVE on the source parent and CREATE on the destination parent;
replacing an existing destination also requires destination REMOVE. Replacement
keeps existing destination handles attached to the old object. A rename to the
same existing entry is a successful no-op. No missing-destination interval is
observable during replacement. The initrd stays immutable.

Directory moves are deferred: they need cycle prevention, lock ordering and an
explicit decision about retained working-directory chains and `..`. No hard
links, recursive deletion, automatic parent creation or cross-filesystem moves.

Doom saves go under `home://doom/saves/<iwad-name>/`, separating shareware and
retail saves. They survive process exit, not reboot. Configuration persistence
is separate. Adapt upstream's remove-then-rename sequence to atomic replacement.

## Following milestones

1. TTY horizontal tabs, fixing indentation when cat prints files saved in Kilo.
2. Wall-clock time: decide UTC initialization and adjustment semantics, keep
   monotonic deadlines independent, and enable TCC's deferred time/date features.
3. [PCI/VirtIO and the first host mount](virtio-fs.md).

The [desktop and graphics notes](desktop-graphics.md) remain future design
context, not additional work for these PRs. On completion, replace this worklist
with concise implemented documentation and retain open limitations in technical
debt rather than archiving a duplicate planning document.
