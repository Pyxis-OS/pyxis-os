# Mapped graphics buffers

Run `mandelbrot` from the shell in an application space. It draws a
pixel-resolution Mandelbrot set progressively. Hold the arrow keys to pan,
`=`/`+` to zoom in around the centre, and `-` to zoom out. Escape releases the
keyboard and display sessions and returns to the shell. Super+Left/Right still
switches spaces. Losing focus clears held controls and pauses rendering until
that space is selected again; held keys need a fresh press after switching back.

The application needs named `display` (DRAW), `keyboard` (INPUT), and `clock`
(READ and SLEEP) grants. Movement uses monotonic elapsed time, with at most
250 ms of catch-up per update. While moving, frames are paced to at most 30 per
second; expensive views may render more slowly. Idle and unfocused states block
on input. The renderer checks events every eight rows so Escape and focus changes do not
need to wait for a complete frame. Zoom is bounded to widths of 1e-12 through 16
in the complex plane. The normal image and SDK contain the application and the
native display, keyboard and clock helpers.

## Authority and ownership

Each space owns a display object. Boot grants init a named `display` capability
with DRAW rights; the shell forwards it to foreground children and session
successors when present. A shell without this optional resource remains usable.
The display is restricted to processes in its owning space. Acquiring graphics
requires this capability, not a space index or a global framebuffer address.

One process may acquire graphics in a space at a time. Another acquisition,
including a repeated call by the owner, returns BUSY. PRESENT and RELEASE are
permitted only to the acquiring process. Copying a capability delegates the
ability to acquire a future session; it does not transfer the current session
or map its pixels into the recipient. Closing a handle does not end the session.
The owner can use another DRAW handle to the same display to release it, or exit.

## Mapping and presentation

[The ABI](../../include/abi/display.h) has three header-only synchronous requests:

- ACQUIRE allocates zeroed RAM and maps it writable and non-executable into the
  caller. Its reply contains the address, mapped size, width, height, pitch and
  red/green/blue shifts. Acquisition leaves the TTY selected until PRESENT.
- PRESENT selects that buffer for the existing periodic presentation task.
  It returns after selection, not after scanout or a complete frame copy.
- RELEASE removes the user mapping and selects the TTY again. It returns no
  reply payload; the old pointer must no longer be used.

Pixels are 32-bit words with three 8-bit channels at the returned shifts. Pitch
is bytes between row starts. The layout matches the current display; dimensions
exclude the kernel-owned navigation bar and remain fixed. Mapped size includes
page padding, which is zeroed along with the pixels. Applications draw only
within width/height and use pitch rather than assuming tightly packed rows.
The mapping is distinct from private-memory allocations and cannot be released
through the memory service.

After PRESENT, the kernel reads the same backing pages that the application
writes. Further changes may appear without another request. This single-buffer
contract permits tearing; PRESENT neither freezes pixels nor promises vblank,
atomic frames or completion notification. There is still a copy to the boot
framebuffer. There is no VirtIO GPU driver, double buffering or userspace
compositor in this milestone.

The TTY keeps its own framebuffer and continues accepting output while graphics
is selected. Its cursor is not composited over graphics. Releasing graphics or
exiting restores the TTY on the next presentation. Graphics ownership is
independent of [keyboard capture](../devices/keyboard.md): Mandelbrot acquires both sessions,
and captured input is withheld from the console stream.

## Mapping and teardown invariants

All display state and backing allocation are BSP-owned, with interrupts disabled
while mutating them. All three operations, including PRESENT, use typed requests
on the common BSP FIFO. The service catalog requires the scheduler to park the
requesting task outside its private root and stack, with entry/current-task state
cleared, before publication. The BSP executor performs the operation and clears
its process/display loans before completion and wakeup; resumption reloads CR3.
The display capability keeps the object alive during the request. Failed
acquisition unwinds partial mappings and backing without claiming the display.

VM owns the kernel allocation; user mappings borrow its physical frames. The
session holds one buffer reference. Presentation acquires another with interrupts
disabled, then copies with interrupts enabled. If release or process cleanup runs
while that copy is preempted, it detaches the session and removes the user aliases
but retains backing until presentation drops the last reference. A presenter
borrows no process pointer. It may finish one old frame before the next tick
shows the restored TTY or newly selected graphics.

Normal exit and fatal user-fault cleanup both release an owned session before
VM destruction, including if the process closed every display handle. Child
completion is published after process cleanup; a final preempted presentation
may temporarily retain only pixel backing. No application mapping survives exit.

## Current boundaries

One buffer, one owner and one user mapping per space; fixed dimensions and native
32-bit pixel layout. Cross-space presentation, shared application mappings,
resize, dirty rectangles, frame completion and graphics-specific resource quotas
remain separate work. The single-CPU development fallback can use the same
display protocol, while its TTY still shares kernel logs.
