# Terminal applications: navigator and multiplexer

Status: agreed sequence of separate milestones (2026-09-29); the display
milestone, its first step, is complete. Each later milestone still needs its
behavior, authority and lifetime decisions settled before implementation. Nothing
here authorizes code.

## Direction

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

## Sequence

1. Complete: [display drivers and live resizing](../kernel/display.md), which
   present the software framebuffer and propagate changed terminal dimensions,
   without 3D acceleration or a compositor.
2. **A single-panel file navigator**, the next step here, in an ordinary terminal: browse
   directory capabilities and schemes, select entries, and launch an editor or
   viewer. Redraw correctly when terminal dimensions change. The navigator and
   file browser are one application. Settle exact operations and any rendering
   dependency before implementation.
3. A terminal multiplexer with binary-space-partitioned panes and independent
   terminal sessions. Pane changes use the same terminal resize contract; the
   navigator, shells and editors are its first consumers.
   Terminal scrollback needs retained text per
   terminal and fits here.
4. Operations between navigators through explicit endpoints, scoped discovery and
   delegated capabilities. Settle operation ownership and failure rules from
   concrete interactions rather than bundling them into the first navigator.

Navigator work can use RAM, writable `host://` and native npfs mounts.
[Native remote terminal sessions](../userland/remote-terminal.md),
[terminal sessions](../userland/terminal-sessions.md) and the
[BSP request milestone](../kernel/bsp-service-requests.md) supply the session
lifetime and request-ownership foundations.
