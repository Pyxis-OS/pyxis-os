# Persistent storage and native terminal applications

Discussion agenda for 2026-09-29, following the completed
[HTTPS milestone](../https.md). Record the next decisions and select one bounded
implementation track; this document does not authorize implementation or settle
the interfaces below.

## 1. Discuss persistent disk storage

Writable `host://` already provides host-backed persistence. Discuss the next
step toward storage owned by Pyxis, beginning with virtio-blk before filesystem
and installation work. Use the existing [filesystem direction](../vfs.md) and
[storage notes](later-os-directions.md#persistent-storage-and-installation).

- Choose a first useful persistence result and its block-device scope. Separate
  basic block I/O from mounting a filesystem, installation and system updates.
- Discuss an existing filesystem versus a custom format, including host-side
  inspection and recovery tools. No format is selected yet.
- Identify required file metadata and identity, replacement/open-handle behavior,
  flush and durability boundaries, and expected behavior after interrupted writes.
- Resolve the [users and authority checkpoint](users-and-authority.md) before
  encoding persistent ownership or shared-home assumptions. Native authority
  rules should drive the design; Unix IDs and permission bits are not defaults.

The result should be a concrete first milestone plus explicitly deferred policy,
not a combined block driver, filesystem, installer and account-system project.

## 2. Investigate Neovim and libuv requirements

Perform a bounded, pinned source/build investigation against the current SDK.
Use these consumers to identify useful missing OS capabilities, rather than
collecting application workarounds until an editor happens to start.

- Trace concrete requirements to upstream operations and distinguish basic
  open/edit/save from plugins, subprocesses, embedded terminals and language servers.
- Inventory existing Pyxis equivalents, semantic gaps and demonstrated libc or
  dependency gaps. Check the required Lua implementation/version; the current Lua
  port and a future PDCurses port do not establish Neovim compatibility.
- Examine multiple threads in one process: shared address space and handle table,
  synchronization, thread-local state, thread exit and process-wide cleanup.
- Examine asynchronous I/O and events: operation submission, completion, waiting
  on several resources, deadlines, cancellation, buffer ownership and backpressure.
  Existing SEND/RECEIVE is a foundation, not a complete asynchronous I/O contract.
- Relate filesystem requirements to the preceding metadata/persistence discussion,
  and terminal/process requirements to the terminal-session direction below.

Describe native Pyxis contracts for the missing behavior, with a libuv platform
backend or libc adapter where appropriate. A requirement such as waiting for
input, a timer or child completion does not prescribe epoll, signals or fork.
Do not add successful stubs, blocking substitutes that freeze the event loop, or
invasive application rewrites to conceal missing functionality. A compile result
alone does not establish working semantics.

Deliver an evidence-based gap list, unresolved decisions, and ordered, focused
milestones with independently useful consumers. This investigation is not a
commitment to finish Neovim before pursuing another application.

## 3. Native terminal sessions, multiplexer and navigator

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

After the storage discussion and Neovim/libuv investigation, select one next
implementation milestone: native disk storage or the first terminal-session
slice, with any demonstrated prerequisites made explicit. Keep the other track
parked. Starting implementation is optional; unresolved behavior, authority and
lifetime decisions should be discussed first.
