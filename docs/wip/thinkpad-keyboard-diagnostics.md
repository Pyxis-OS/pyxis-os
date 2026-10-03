# ThinkPad keyboard bring-up diagnostics

Status: owner assigned a separate follow-up to [HPET PR #341](https://git.internal/PyxisOS/pyxis-os/pulls/341)
on 2026-10-03. Branch `bringup/keyboard-diagnostics` starts from main `c185e13`,
which merged its corrected, signed head `7035cd2`. Dependencies use current
main's pins (`fs` `810d2af6`, ports `2f9d55d3`, lwIP `a1aadb91` and userspace
`0c4743de`); no dependency or compiler-container change is needed.

## Evidence and scope

The owner reached native userspace on all 12 ThinkPad T14 Gen 1 AMD CPUs.
Subsequent review comments relay a PXE boot with a blinking cursor until the
first keypress, after which blinking stops, and an approximately 1.5-second
pause after the keyboard IRQ-route message. These observations have not been
repeated by an agent. Neither observation establishes the failed command or
proves that Caelum received a keyboard IRQ. A setup timeout and a later stall
remain hypotheses to distinguish.

The owner's Fedora FADT flag is `0x0013`; its 8042 bit is set. The proposed
clear-FADT-bit probing workaround is withdrawn. No PS/2 protocol change is
assigned before the failure is identified.

## Assigned inventory

- [ ] Report the PS/2 initialization step that failed, the last observed
  controller status and the scan-set reply when available. Distinguish
  disable/drain, config read/write, keyboard enable, scanning disable,
  scan-set selection/query, IRQ enable and scanning enable. Preserve the
  existing commands, retry limits and failure cleanup.
- [ ] Move PCI function, BAR and owned-BAR inventory details to `ktrace`.
  Keep the existing function/bus summary, discovery breadcrumbs and incomplete
  inventory or ownership warnings at info. Capability and bridge inventory
  details belong with their function's trace output. Per-space initialization
  and ordinary process exits already moved to trace in #341.
- [ ] Retain the early kernel log in a fixed static buffer without heap use.
  Keep the beginning when full, count dropped bytes, and replay a truncation
  notice after the retained prefix. Replay once into the initialized Caelum
  TTY under the existing log lock before enabling live TTY output. Stop
  capturing after this handoff. Serial stays live without duplicate replay;
  existing early-console and panic ownership remain intact. A 32 KiB buffer
  is an implementation choice, not an architectural minimum.
- [ ] Validate ordinary info/trace builds, interactive QEMU boots and debugger
  inspection of capture/replay state. Check 12 CPUs and a 1920x1080 framebuffer
  where supported. Record exact revisions, configuration and limits, then ask
  the owner to capture the next native keyboard result from the Caelum tab.

Replay retains bytes, not terminal scrollback: later output can still scroll
the retained text off screen. Userspace log access would require a separate
capability decision and is outside this task.

## Deferred proposals and remaining evidence

Visible heartbeat and AP-watchdog/NMI stall capture are review proposals, not
assigned implementation. An NMI diagnostic would need an explicit scope and
panic-display policy; current post-handoff panics are serial-only. Do not add
either diagnostic, a translation/scancode workaround, USB firmware handoff or
TSC work speculatively.

The new failure message and retained boot log should reveal whether setup
completes. A Shift-only native keypress is a useful owner observation because
it produces no text; it may narrow the failure before console text routing.
The roughly 15-minute native HPET clock check remains pending until input works.

## Delivery state

Inventory recorded; implementation and validation pending. #341 is merged;
this PR targets main. No QEMU/debugger jobs are active.
