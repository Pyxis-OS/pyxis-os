# ThinkPad keyboard bring-up diagnostics

Status: owner assigned a separate follow-up to [HPET PR #341](https://git.internal/PyxisOS/pyxis-os/pulls/341)
on 2026-10-03. Branch `bringup/keyboard-diagnostics` starts from main `c185e13`,
which merged its corrected, signed head `7035cd2`. The follow-up integrates main
`132aef1` to resolve the milestone-index conflict after #342. Current main's
pins are `fs` `810d2af6`, ports `a50ae5cc`, lwIP `a1aadb91` and userspace
`53b6860f`; this task adds no dependency or compiler-container change.

## Evidence and scope

The owner reached native userspace on all 12 ThinkPad T14 Gen 1 AMD CPUs.
Subsequent review comments relay a PXE boot with a blinking cursor until the
first keypress, after which blinking stops, and an approximately 1.5-second
pause after the keyboard IRQ-route message. These observations have not been
repeated by an agent. Neither observation establishes the failed command or
proves that Caelum received a keyboard IRQ. A setup timeout and a later stall
remain hypotheses to distinguish.

The owner's Fedora FADT flag is `0x0013`; its 8042 bit is set. The proposed
clear-FADT-bit probing workaround is withdrawn.

The [next owner result on #341](https://git.internal/PyxisOS/pyxis-os/pulls/341#issuecomment-3349)
shows the whole info boot log retained in the Caelum tab and reports:

```text
keyboard: PS/2 initialization failed at read scan set (status 0x14, last reply 0xfa); input unavailable
```

Code inspection establishes that both the set-2 selection (`F0 02`) and query
(`F0 00`) reached their ACKs; no set-ID byte followed. The owner approved a
short optional ID wait, retaining failure for a wrong ID or controller error.
That scoped fallback is now implemented; translation and command ACK policy
remain unchanged. The relayed native log also records 12 CPUs, 39 PCI functions
on 8 buses, and `HPET 32-bit counter, software-extended, period=69841278 fs`.
These are owner observations, not a repeated agent-native run. The precise
booted revision and multi-wrap clock behavior remain unqualified.

## Assigned inventory

- [x] Report the PS/2 initialization step that failed, the last observed
  controller status and the scan-set reply when available. Distinguish
  disable/drain, config read/write, keyboard enable, scanning disable,
  scan-set selection/query, IRQ enable and scanning enable. Preserve the
  existing commands, retry limits and failure cleanup.
- [x] Move PCI function, BAR and owned-BAR inventory details to `ktrace`.
  Keep the existing function/bus summary, discovery breadcrumbs and incomplete
  inventory or ownership warnings at info. Capability and bridge inventory
  details belong with their function's trace output. Per-space initialization
  and ordinary process exits already moved to trace in #341.
- [x] Retain the early kernel log in a fixed static buffer without heap use.
  Keep the beginning when full, count dropped bytes, and replay a truncation
  notice after the retained prefix. Replay once into the initialized Caelum
  TTY under the existing log lock before enabling live TTY output. Stop
  capturing after this handoff. Serial stays live without duplicate replay;
  existing early-console and panic ownership remain intact. A 32 KiB buffer
  is an implementation choice, not an architectural minimum.
- [x] Accept an absent scan-set ID after a short wait when set-2 selection and
  query commands were ACKed. Reject a wrong ID or controller error. Drain
  pending query output while scanning is disabled before enabling scanning.
  The 20 ms duration is an implementation choice; see the
  [keyboard contract](../devices/keyboard.md).
- [x] Validate ordinary info/trace builds, interactive QEMU boots and debugger
  inspection of capture/replay state. Check 12 CPUs and a 1920x1080 framebuffer
  where supported. Record exact revisions, configuration and limits, then ask
  the owner to capture the next native keyboard result from the Caelum tab.
- [ ] Record native letters, digits, modifiers and extended-key behavior after
  the optional-query change. If keypress still freezes the system, discuss a
  separately scoped stall diagnostic from the new evidence.

Replay retains bytes, not terminal scrollback: later output can still scroll
the retained text off screen. Userspace log access would require a separate
capability decision and is outside this task.

## Deferred proposals and remaining evidence

Visible heartbeat and AP-watchdog/NMI stall capture are review proposals, not
assigned implementation. An NMI diagnostic would need an explicit scope and
panic-display policy; current post-handoff panics are serial-only. Do not add
either diagnostic, a translated set-1 decoder, USB firmware handoff or
TSC work speculatively.

The failure message and retained boot log identified the missing query ID.
The next native boot must establish that setup and input work with the fallback.
A Shift-only native keypress is a useful owner observation because
it produces no text; it may narrow the failure before console text routing.
The roughly 15-minute native HPET clock check remains pending until input works.

## Delivery state

The three diagnostic changes are implemented in [PR #343](https://git.internal/PyxisOS/pyxis-os/pulls/343),
based on main `c185e13`. #341 is merged; this PR targets main. Its code was
built with GCC 16.2.0 and pinned Kconfiglib 14.1.0 using:

```sh
make -j16 image PREBUILT='sdk userspace ports' LOG_LEVEL=info \
  PYTHON=build/hpet-config-venv/bin/python3 \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis-
```

The SDK/userspace/ports inputs are unchanged verified bundles. Repeat with
`LOG_LEVEL=trace` to retain detailed inventory. Both ordinary builds passed
without warnings or undefined symbols. Both interactive QEMU 10.2.2 Q35/KVM
boots used 12 CPUs, 256 MiB, `-cpu max`, the Fedora OVMF pair and
`CONFIG_XHCI=n`. The host is the Ryzen 5 PRO 4650U ThinkPad running Fedora,
with host KVM rather than nested virtualization.

The info boot used `make debug` with VirtIO network/entropy and a 1280x800
framebuffer. GDB stopped at `log_set_tty`: 3,774 retained bytes, zero dropped,
initialized TTY, IF clear and replay not yet performed. On return, replay was
complete, the byte counts unchanged, the selected TTY correct, IF still clear
and the lock released. Counts remained unchanged after userspace started.
The Caelum screenshot showed pre-attachment lines after presentation handoff;
serial showed each boot line once. At this resolution with verbose VirtIO
startup logs, the earliest lines had already scrolled off, as expected without
scrollback.

Manual QEMU monitor `sendkey shift` and `sendkey a` produced the raw set-2 bytes
`12 f0 12 1c f0 1c`, all consumed. No input loss was recorded; both held-key
states were released and the BSP timer advanced from 6,028 to 9,852 deliveries.
This confirms the virtual input path continues to work; it does not reproduce
or diagnose the native freeze.

The trace boot used the same CPU/memory/firmware configuration, with no NIC or
VirtIO RNG and `-vga none -device VGA,xres=1920,yres=1080`. Its actual framebuffer
was 1920x1080. It reached preemptive userspace and retained individual PCI/BAR/
capability, space and exit messages. GDB observed 2,722 retained bytes, zero
dropped, a single replay and unchanged counts after startup. The different
retained length reflects the different devices, not a matched performance
comparison. Debugger stops separate these checks from timing measurements.

Those pre-commit info/trace runs exercised the source committed as `1849129`.
The info image was rebuilt at that code revision and booted again with the
1920x1080/no-NIC/no-RNG configuration:

```sh
cp /usr/share/edk2/ovmf/OVMF_VARS.fd build/keyboard-diagnostics-final-vars.fd
qemu-system-x86_64 -machine q35 -accel kvm -cpu max -rtc base=utc \
  -smp cpus=12,sockets=1,cores=12,threads=1 -m 256M \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=build/keyboard-diagnostics-final-vars.fd \
  -display none -serial mon:stdio -vga none -device VGA,xres=1920,yres=1080 \
  -nic none -cdrom build/pyxis.iso -boot d -no-reboot -no-shutdown
```

The final Caelum screenshot showed the retained header, clock, keyboard and
PCI summary still visible after userspace and presentation started. No per-function
or BAR inventory lines were present at info. The absent entropy-source message
remained visible; it is outside this keyboard/logging scope. Final unforced
info artifact SHA-256 values at `1849129` are:

```text
caelum.elf  ccdee99bac9113983dd1efc9c7b6d75a77a168b1e63fb86d16b4abf9860bf8a9
pyxis.iso   e2987dae31c708af7c1cd6762320d99b17272ec099ab47d5498e3692eeddc04e
```

Local logs use `build/keyboard-diagnostics-*`. Buffer exhaustion and PS/2 failure
diagnostics were reviewed by code inspection, not triggered in QEMU; no fault
injection, diagnostic hooks or new tests were added. The command/retry sequence
and panic routing were independently reviewed without a concrete finding. All
validation QEMU/GDB jobs are stopped; the final delivery uses info logging.
The subsequent native result above identifies the query timeout. The original
keypress-triggered freeze remains unexplained, and native input validation after
the fallback remains pending.

### Optional-query follow-up validation

The source committed as `0217e95` was built after integrating main `132aef1`.
Ordinary `make -j16 image` rebuilt the SDK, userspace and ports at main's pins;
it passed with vendored port warnings. The subsequent kernel/image rebuild
using those verified bundles passed without kernel warnings or undefined
symbols. Logs are `build/keyboard-optional-query-build.log` and
`build/keyboard-optional-query-kernel-build.log`.

An initial KVM/GDB run with a hardware breakpoint installed before firmware
boot stalled before any Caelum output. QEMU segfaulted when closed. That run
did not validate the query path. The repeat ordinary boot, with GDB attached
only after startup, passed on the same host with 12 CPUs, 256 MiB, Q35/KVM,
1920x1080 VGA, OVMF, no NIC/RNG and xHCI disabled. It reported scan set 2 ready
and reached preemptive userspace; QEMU returns its ID normally, so the native
missing-ID fallback was not exercised. No fault injection or test hooks were
used to simulate that condition.

Manual monitor commands `sendkey meta_l-right`, `sendkey a`, `sendkey 1`,
`sendkey shift-a` and `sendkey up` selected Development, displayed `a1A`, and
produced the extended Up-key sequence. GDB found all 27 raw bytes consumed,
zero queued bytes, no input loss and released Shift/Up states. The BSP timer
advanced from 8,756 to 15,689 deliveries. The late-reply drain and tri-state
error/absence distinction were inspected; a read-only review identified and
resolved a logging delay between drain and scanning enable. The fallback
message now follows successful setup, so draining is immediately before the
enable-scanning command. All QEMU/GDB jobs were stopped.

Local records are `build/keyboard-optional-query-normal-boot.log`,
`build/keyboard-optional-query-normal-gdb.log` and the Development screenshot
at `build/keyboard-optional-query-development.png`. These checks qualify the
normal virtual input path, not the native fallback or the original freeze.
