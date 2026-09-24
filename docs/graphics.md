# Mapped graphics buffers

Run `mandelbrot` from the shell in an application space. It draws a
pixel-resolution Mandelbrot set progressively, then waits for a terminal key.
Super+Left/Right still switches spaces. A key delivered to the application after
rendering releases graphics and restores its TTY, including the shell prompt.
The normal image and SDK contain the application and native display helpers.

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

[The ABI](../include/abi/display.h) has three header-only synchronous requests:

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
exiting restores the TTY on the next presentation. Input still uses the existing
console stream; graphics ownership does not establish general input arbitration.

## Mapping and teardown invariants

All display state and backing allocation are BSP-owned, with interrupts disabled
while mutating them. A syscall parks the requesting task outside its private root
and stack before publishing a display request. The BSP creates/removes user page
aliases, then wakes the task; resumption reloads CR3. Failed acquisition unwinds
partial mappings and backing without claiming the display.

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
resize, dirty rectangles, frame completion, graphics-specific resource quotas
and physical key events remain separate work. The single-CPU development fallback
can use the same display protocol, while its TTY still shares kernel logs.
