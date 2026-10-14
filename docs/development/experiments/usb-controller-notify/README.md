# Cross-CPU USB controller notification

2026-10-14. A latent main bug exposed during native qualification of #852:
an AP forwarder CUT or automatic cut on final controller-grant release calls
`bluetooth_hci_revocation_notify()`. With an AX200 host, that reaches the
BSP-only `usb_host_notify()` assertion. The disappearance change moved no task
or caller. This fix is a separate prerequisite for #852.

## Baseline and stand-in

Main baseline: `e5e995f070d026e3181deed562b7010ba907c62c`.
Fixed code: `75db872737cc172547afabf82fed4f8e008237a8`.
Builder: `git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-df87791`.
Full baseline SDK/ports/userland/kernel image build, then ordinary fixed
`make -j16 image PREBUILT="sdk ports userspace" LOG_LEVEL=trace` using those
verified unchanged bundles. Pins: userland `220b8076`, ports `efe8d877`,
fs `d84f8084`, lwIP `a1aadb91`. No compiler or public ABI change.

QEMU cannot supply AX200. The owner authorized a stand-in: a one-line **local,
unmerged** call to `bluetooth_hci_candidate(controller)` immediately before
`usb_enumerate()` in `controller_worker`. It supplies the real retained xHCI
host to the real revocation callback, without a fabricated HCI device, firmware
or successful HCI operation. Apply the same line to baseline and fixed code;
remove it from ordinary builds. Patch: task worktree
`build/hci-notify-standin.patch`, SHA256
`9db22c3018a9c1010eacd52f62ede837a1fd1f705f2e4c381797464f365c0702`.
The patch is absent from the submitted tree and must not be staged natively.

QEMU 10.2.2/Q35/nested KVM, `-cpu max`, four CPUs, 2 GiB, fresh OVMF variables,
UTC RTC, VirtIO network/RNG, `qemu-xhci`, boot hub/keyboard and a read-only
48 MiB root USB disk. Remote TCP 24085 forwards to guest 2323; GDB 1248.
Existing remote command, unchanged:

```text
session bin://counter.pxe --forwarders bin counter.pxe textfs.pxe
```

Read-only hardware breakpoint on `usb_host_notify`, with CPU identity read
from the saved kernel GS base. Baseline catches explicit CUT on CPU 1 with a
published BSP waiter and `notified=false`; continuing prints the same assertion
as the ThinkPad. This is runtime confirmation of the call-path mismatch, not
AX200 transport emulation. No injected function or target-memory write.

## Fixed results and lifetime reasoning

The same stand-in catches explicit CUT on CPU 1 without an assertion. A later
last-controller CLOSE reaches `task_wait_wake()` on CPU 1: controller waiter is
already NULL and its notification guard remains locked. Wake can publish the
BSP worker immediately, but that worker cannot clean up/reuse its wait until
it reacquires the guard after AP wake finishes. Expiry follows that same guard.
The guard is released before sleep and log output. IRQ shares it safely because
all local holders have interrupts disabled; an AP holder waits for no BSP
acknowledgment. Order is notification guard then scheduler queue, with no
reverse acquisition or controller work under the guard.

Forwarder qualification completes with status 0 and full group drain, covering
explicit CUT and controller release. Existing
`session bin://counter.pxe --queued-cut` also returns status 0, with the queued
client denied before delivery. Final GDB: controller running, not failed,
guard unlocked, a normally parked waiter, 118 interrupts/120 consumed events,
active USB HID and `forwarder operations == NULL`. Ordinary keyboard IRQ input
continues. A separate **unpatched** one-CPU fixed-image boot and forwarder run
returns status 0; host is NULL, controller remains healthy.

Other CUT notifiers source-audited: readiness and networking already guard
notification/wait handoff through wake and accept remote callers; forwarder
wait registration/removal shares the CUT guard. Endpoint cancellation runs
after that guard is dropped, with a retained receipt preserving its storage.
No second BSP-only notification mismatch found in this bounded audit.

ELF SHA256: baseline stand-in
`2136a9035a1aec9100d2bbf6843321a3ed89eeb40e316719897f55e079cdbda2`;
fixed stand-in `629d2348abc5afaf77bd090c79df5b8eb2c82e68c9dbee2b879c71dd9671d6b5`;
ordinary fixed `6deb40b10f1d6d47c479ae2b564e8e9532ca4f102015a173ff3162ee44b44f1d`.
Raw serial/GDB/JSONL and immutable ELF/ISO snapshots remain under the task
worktree's `build/notify-*`. All owned jobs stopped. No speedup or native fix
result claimed; owner verifies the ordinary build on the ThinkPad with AX200.
