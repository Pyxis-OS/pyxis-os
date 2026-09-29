# Persistent storage and native terminal applications

Discussion agenda for 2026-09-29, following the completed
[HTTPS milestone](../https.md). Record the next decisions and select one bounded
implementation track; this document does not authorize implementation or settle
the interfaces below.

## 1. Discuss persistent disk storage

The active [pool/filesystem working draft](persistent-storage.md) records the
discussion and explicitly agreed choices; other mechanisms remain proposals.

Writable `host://` already provides host-backed persistence. The
[block-storage foundation](../block-storage.md) is complete. The
[initial format and read-only core](../filesystem-readonly.md) is complete;
writable recovery, native mounts and installation remain proposed. Use the existing
[filesystem direction](../vfs.md) and
[storage notes](later-os-directions.md#persistent-storage-and-installation).

- The first milestone delivered kernel-internal block I/O and GPT discovery.
  Mounting a filesystem, installation and system updates remain separate work.
- The storage discussion selected a custom COW pool/filesystem with a shared
  core for Caelum and Linux FUSE. The
  [format contract](../../fs/docs/format.md) now defines the initial disk layouts
  and read-only host tools; writable recovery algorithms remain open.
- Identify required file metadata and identity, replacement/open-handle behavior,
  flush and durability boundaries, and expected behavior after interrupted writes.
- The [users and authority checkpoint](users-and-authority.md) supplies the
  implemented ownership/grant model. Resolve writable bootstrap admission and
  native integration before introducing shared-home assumptions. Native authority
  rules should drive the design; Unix IDs and permission bits are not defaults.

Use the working draft to scope a later milestone and its policy decisions before
implementation.

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
Use the report when selecting later native OS work; do not import fork/epoll/signals
or add successful stubs just to satisfy a
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

The agreed first slice is [native remote terminal sessions](remote-terminal.md),
with a text-based agent/developer client and independent session lifetime.
The later progression is a minimal multiplexer, then the single-panel navigator
and its cross-navigator operations. Split that direction
into bounded milestones before implementation; do not bundle the whole chain.

## Selected next slice

The [BSP request milestone](../bsp-service-requests.md) is complete; the next slice
is [native remote terminal sessions](remote-terminal.md). Its agreed
contracts and focused task list replace the earlier open choice for this slice.
Persistent writable storage, the toolchain transition, multiplexer and navigator
remain parked directions. Discuss each task's remaining behavior, authority and
lifetime details before implementation.
