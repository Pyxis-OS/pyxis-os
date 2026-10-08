# Terminal applications: multiplexer and navigator

Status: agreed sequence of separate milestones (2026-09-29, reordered by the
owner on 2026-10-07); the display milestone, its first step, is complete, and
[space layers](../userland/space-layers.md) place graphical programs above the
terminal. **The multiplexer is assigned to Codex 1 (2026-10-08), starting with
a proposal** that settles its remaining behavior, authority and lifetime
decisions. Nothing here authorizes code before that proposal is accepted.

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
2. **A terminal multiplexer** with binary-space-partitioned panes and independent
   terminal sessions. Shells, vi, Kilo, less, Links and `log -f` are its first
   consumers. Terminal scrollback needs retained text per terminal and fits here.
3. **A single-panel file navigator** in an ordinary terminal or a pane: browse
   directory capabilities and schemes, select entries, and launch an editor or
   viewer. Redraw correctly when terminal dimensions change. The navigator and
   file browser are one application. Settle exact operations and any rendering
   dependency before implementation.
4. Operations between navigators through explicit endpoints, scoped discovery and
   delegated capabilities. Settle operation ownership and failure rules from
   concrete interactions rather than bundling them into the first navigator.

The owner moved the multiplexer ahead of the navigator on 2026-10-07: its
consumers already exist and it is useful at once, and the navigator can then be
designed to live in a pane. Graphical programs do not run in panes; with
[space layers](../userland/space-layers.md) they sit on the layer above the multiplexer.

## Multiplexer decisions

Accepted by the owner on 2026-10-07:

1. **Drawing.** The multiplexer interprets each pane's output itself, as a
   userspace terminal emulator limited to the escapes the Pyxis terminal supports,
   and draws the panes into its own terminal. The kernel does not hand out
   per-pane screen regions.
2. **Resizing.** [Terminal sessions](../userland/terminal-sessions.md) gain a
   resize operation with the same "size changed" event as local consoles; today
   a session's dimensions are fixed at creation.
3. **Authority.** A per-space setting grants the terminal-session creation
   service to the multiplexer, as `launch = true` grants the child launcher.
   Today only trusted init receives it.

A possible first slice is two panes with a fixed prefix key, running shells and
the existing editors.

Navigator work can use RAM, writable `host://` and native npfs mounts.
[Native remote terminal sessions](../userland/remote-terminal.md),
[terminal sessions](../userland/terminal-sessions.md) and the
[BSP request milestone](../kernel/bsp-service-requests.md) supply the session
lifetime and request-ownership foundations.
