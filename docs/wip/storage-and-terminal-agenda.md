# Persistent storage and native terminal applications

Discussion agenda for 2026-09-29, following the completed
[HTTPS milestone](../https.md). Record the next decisions and select one bounded
implementation track; this document does not authorize implementation or settle
the interfaces below.

## 1. Discuss persistent disk storage

The active [pool/filesystem working draft](persistent-storage.md) records the
discussion and explicitly agreed choices; other mechanisms remain proposals.

Writable `host://` already provides host-backed persistence. Discuss the next
step toward storage owned by Pyxis, beginning with virtio-blk before filesystem
and installation work. Use the existing [filesystem direction](../vfs.md) and
[storage notes](later-os-directions.md#persistent-storage-and-installation).

- Choose a first useful persistence result and its block-device scope. Separate
  basic block I/O from mounting a filesystem, installation and system updates.
- The storage discussion selected a custom COW pool/filesystem with a shared
  core for Caelum and Linux FUSE. Exact disk layouts and recovery algorithms
  remain open; see the working draft rather than reopening the selected direction.
- Identify required file metadata and identity, replacement/open-handle behavior,
  flush and durability boundaries, and expected behavior after interrupted writes.
- Resolve the [users and authority checkpoint](users-and-authority.md) before
  encoding persistent ownership or shared-home assumptions. Native authority
  rules should drive the design; Unix IDs and permission bits are not defaults.

The result should be a concrete first milestone plus explicitly deferred policy,
not a combined block driver, filesystem, installer and account-system project.

## 2. Investigate Neovim and libuv requirements

The initial [Neovim/libuv investigation](neovim-libuv.md) is complete: pinned
source inspection, SDK header provenance checks and two header-only compiler
probes. No full build, target link or runtime compatibility was demonstrated.

Even the selected Neovim's basic TUI launches an editor subprocess and exchanges
RPC over pipes. The report separates native event waiting, shared-process threads,
metadata/identity and terminal-session gaps from libc/libuv adapters and dependency
work. It also distinguishes basic editing from jobs, embedded terminals, native
modules and language-server executables. Existing Lua and possible PDCurses ports
do not establish Neovim compatibility.

Six bounded follow-up milestones are proposals, not additional active tracks.
Block storage remains selected. Use the report when selecting later native OS
work; do not import fork/epoll/signals or add successful stubs just to satisfy a
particular upstream backend. Dependency build/link closure and runtime behavior
remain to be investigated during a future port milestone.

## 3. Investigate LLVM/Clang transition and hosting

LLVM/Clang is now the chosen toolchain direction, with Clang the first large
hosted C compiler target. The [toolchain notes](toolchains-and-runtimes.md)
separate host-side cross-toolchain migration, native C++/OS prerequisites, and
running Clang inside Pyxis. Hosting does not require bootstrapping LLVM in the
guest as its first result; Rust remains a separate runtime/OS integration effort.

Add a bounded, pinned LLVM requirements probe alongside Neovim/libuv. Identify
the needed compiler tools and runtime libraries, distinguish host build tools
from guest dependencies, and trace missing filesystem, process, threading and
synchronization behavior to native Pyxis contracts. Use common requirements to
inform milestone selection; neither consumer should define the kernel by its
current Unix implementation. No toolchain/container change is part of this agenda.

## 4. Native terminal sessions, multiplexer and navigator

The preferred direction replaces a multi-panel file-manager application with
independent applications inside a terminal multiplexer:

- A native terminal-session contract owns input/output, terminal state, dimensions
  and attachment/lifetime behavior. Define resize, focus, exit and failure handling.
  Future PTY compatibility may adapt to this contract; it does not define it.
- A tmux-like userspace multiplexer owns windows, binary-space-partitioned splits,
  pane focus and layout. Here BSP means the layout tree, not the bootstrap CPU.
  Independent applications use separate sessions rather than drawing into the
  same terminal. This remains distinct from spaces and a graphical compositor.
- A navigator is a single-panel application with its own location, history and
  selection. PDCurses is a possible rendering/input dependency, to be investigated
  through that consumer rather than assumed necessary for the multiplexer.
- Independent navigators expose endpoints for intentional interaction. Proposed
  file operations exchange capabilities for selections and destination directories,
  rather than treating path strings as authority. Discovery and delegation should
  be scoped explicitly; sharing a layout grants no automatic access to other panes.

The endpoint shapes and operation ownership remain open. In particular, distinguish
atomic rename from cross-backend copy-and-remove, and decide who performs a copy,
owns its progress and handles cancellation/failure. Do not design those policies
implicitly inside the multiplexer. Shells and editors should remain ordinary
applications alongside navigators.

Candidate progression: terminal sessions, then a minimal multiplexer, then the
single-panel navigator and its cross-navigator operations. Split that direction
into bounded milestones before implementation; do not bundle the whole chain.

## End-of-discussion decision

After the storage discussion and bounded Neovim/libuv and LLVM investigations,
select one next implementation milestone. Native disk storage and the first
terminal-session slice remain candidates; the toolchain transition or a shared
prerequisite can be selected explicitly instead. Keep the other tracks parked.
Starting implementation is optional; unresolved behavior, authority and lifetime
decisions should be discussed first.
