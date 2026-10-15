# Vela HID-I2C snapshot: task 1

## Source and build

Pre-code baseline: main `9d5fd73889563686bcf2255c6f2bdae5aeb4631d`, successful
CI 2699/run 2792 image and matching component bundles. Kernel SHA-256:
`35b5d95c55f5eb10d95fd913ee1dc7ad05510ad52f1d466818cbe0bfd0e1ef6f`.
Implementation: `ae141ce4`, on the accepted #888 plan; no submodule changes.
Pins: userland `20a98189550e`, ports `318c5c39221e`, fs `22eaf447a98b`,
lwIP `a1aadb91a503`. Builder `pyxis-llvm23.1.3-df87791`; no compiler rebuild.

Ordinary `make -j16 image PREBUILT='sdk userspace ports'` passed. The unchanged
SDK/userland/ports are verified bundles from that same run, not rebuilt inputs.
Kernel uses stock configuration, debug compiled and info console. Raw local
build/QEMU/GDB/JSONL records: task worktree `build/task1-{baseline,after4,after1}`.
No probe, synthetic firmware, injected call or new test infrastructure.

## QEMU and controls, 2026-10-15

Nested KVM Q35, `-cpu max`, 2 GiB, OVMF, standard VGA, VirtIO-net/rng, xHCI
USB2 hub/keyboard. Remote localhost 24488 → guest 2323, stock live spaces.
Baseline four-CPU boot captured before code; matched four-CPU implementation
boot, then one-CPU implementation boot for worker/input inspection. Separate
fresh firmware variables. No build/GDB overlapped timed workloads; host load
was uncontrolled. This is one A/B pair, not a native performance result.

Use `build/tools/pyxis-remote --machine --no-shell-echo 127.0.0.1 24488`.
Each IPC command uses a fresh connection:

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe call --size 4096 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 4096 --messages 256 --rounds 5
lua -e 'local a={"bin://echo.pxe","-n"}; local r=pyxis.run; local n=pyxis.monotonic_ns; assert(r(a)==0); for i=1,10 do local t=n(); assert(r(a)==0); print(i,n()-t) end'
```

Every sample verified, final status 0 and complete Remote drain. Completion
times below are milliseconds, median (range); SEND admission is separate.

| Workload | Baseline | Snapshot |
| --- | --- | --- |
| CALL64 | 90.103 (86.525–94.059) | 88.735 (80.047–95.461) |
| CALL4096 | 90.940 (85.962–106.642) | 82.308 (80.011–100.700) |
| SEND64 completion | 15.939 (12.891–18.378) | 20.482 (12.692–26.680) |
| SEND64 admission | 1.221 (1.193–1.333) | 2.067 (1.236–2.524) |
| SEND4096 completion | 15.459 (11.786–19.437) | 14.060 (12.285–16.427) |
| SEND4096 admission | 1.286 (1.251–1.440) | 1.474 (1.318–1.606) |
| Echo through exit | 8.833 (5.153–9.133) | 8.362 (2.927–9.142) |

These small nested samples vary materially, including higher SEND64 medians;
they establish no throughput conclusion. Source inspection: only the one-shot
ACPI startup path calls discovery; no IPC, scheduler or ongoing input path
changes. Both boots reach Caelum ready and all spaces. The existing namespace
timer reports 2544/2450 µs before discovery; whole-boot wall time was not timed.

Read-only GDB (`set may-call-functions off`) confirms discovery runs directly
from `acpi_worker`, before its readiness flag. One CPU: PENDING → ABSENT,
zero published endpoint, then readiness true and Remote echo/exit succeeds.
Four CPUs: ABSENT, zero endpoint, readiness true. The one-shot pass measured
52.75/58.02 µs on 1/4 CPUs; not native CPU/latency evidence. No absence log.
QMP Enter reaches USB `usb_hid_collect` → `input_keyboard_snapshot`; after
removing the emulated USB keyboard it reaches PS/2 `keyboard_receive` through
IRQ 33. The Development prompt accepts Enter. All own QEMU/GDB jobs stopped.

## Native gate

Pending owner deployment on vela, with `display.idle_blank=0`. Collect
`log -l debug | grep -i -e 'HID-I2C' -e 'ACPI: error' -e 'ACPI: warning'`.
Success is one endpoint summary with live controller path, address/speed,
descriptor register and GPIO/IRQ route; refusal must retain its named reason.
Boot, pool and existing input must remain usable. No touch or GPIO operation
is implemented, and QEMU does not qualify positive/malformed vela resources.
Task 3 moving-finger rate/idle CPU and task 4 filter latency remain unmeasured.
