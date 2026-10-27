# BSP display/thread VM admission

2026-10-27. The port audit's exact ImGui executable
`884d3c458e25decd8c646ea54ef92238820a26dedb779507ceb545ca58eff6d0` and Caddy
`4ac6bb82acfc16bf0f658d672ec6499f544e72e303e40096b8d28fe0e86a53ba` are used
unchanged. No port retry or resource-policy change.

## Cause and correction

The audit's four-vCPU GDB capture stops at `display_request_execute`'s failed
`vm_kernel_owner_try`, before display ownership or frame allocation. That failure
means another actor holds the shared kernel owner, not an existing display owner.
Shared retirement may retain that owner over a scheduler yield, so another BSP
worker can encounter it even with one CPU. Private-process drainage does not
also admit the shared kernel owner. Thread creation has the same immediate-try
omission; its other BUSY paths remain distinct and unchanged.

On contention, the two BSP execution entries now use the existing one-second,
parking owner admission before effects, then restore IF=0 and run their existing
validation/publication paths. The drained private mutator and request remain
retained; no short lock or mapping borrow spans the wait. The owner is released
before notifying the caller. Same-actor nested ownership is unchanged. An actual
admission timeout still returns pre-effect BUSY; it is not silently retried.
This can delay the BSP request/VM worker by the existing admission budget.

## Checks and evidence limits

Disposable nested-KVM Q35, four vCPUs/4 GiB, VirtIO display/network/RNG, OVMF,
diskless live archive; one-vCPU baseline also inspected. Kernel baseline is the
audit's exact `b1fd5a32` ELF, SHA-256
`6e0c2832105c6ad712cee7a36030f020e6c44cf309d0c799585ada9366ac5429`.
Fixed kernel is `bf74e0b7`, based on main `faec2f54fcb0`, compiler `8a4d001d`. The retained live launcher binaries
were built at userland `333b094d`; these checks do not claim a rebuilt audit
userland or newly linked application.

Fresh baseline runs can render ImGui and start Caddy: this is an intermittent
admission failure, not a permanent display/transport refusal. Allocation/retirement
pressure using the existing allocbench also did not reproduce BUSY in those
fresh runs; captured entry owners were free. The audit's failed-try trace is the
positive evidence for the reported ImGui refusal. The exact failing Caddy
admission site has not been independently captured, so it is not conflated with
other possible thread/private-VM BUSY causes.

The fixed four-vCPU run renders the unchanged ImGui demo, exits it with Escape,
and serves `Pyxis cleanup Caddy` over HTTP from the unchanged Caddy bundle. The
fixture explicitly binds `10.0.2.15` (wildcard listen is outside the existing
native profile), and the request supplies the matching Host header. This corrects
the host test fixture, not the port. Kernel build and an independent source review
pass. No native, performance, timeout or contended-wake runtime claim is added.

Sources, hashes, screenshots, GDB and command receipts remain under
`/shared/present/vm-busy/`; the original failure capture is
`/shared/present/port-patch-audit/cleanup/2026-10-27/imgui-display-busy-gdb.txt`.
Own QEMU/debugger/Remote processes stop after each capture. Other agents' guests
and private game data are untouched.
