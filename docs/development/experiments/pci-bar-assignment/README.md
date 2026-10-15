# PCI BAR assignment qualification

## Sources and baseline, 2026-10-15

Accepted scope #897, three owner defaults. Baseline `23fbe4d9` contains docs
only over main `e81af00f`; captured before merging implementation. Matching
CI2739 bundles, df87791 compiler, userland `9c8b325d9875`, ports `f75c3a1c7606`,
fs `22eaf447a98b`, lwIP `a1aadb91a503`. No compiler rebuild or public ABI change.
Local raw logs/images: PCI worktree `build/task1-baseline` and
`build/assignment-after4`; LPSS worktree `build/task2-pci-unassigned1`.

Ordinary builds pass. Source review corrected legacy-PCI extended-space gating,
active ancestor forwarding, consumer resource envelopes, missing optional `_CRS`,
actual LPSS register-size bounds and uncertain-write admission. Only selected
claimed BARs receive probe/program writes; all foreign sizing is read-only.
These reviews do not qualify hardware failure paths.

## Matched ordinary controls

Nested KVM Q35, CPU max, 4 CPUs/2 GiB, OVMF, standard VGA, VirtIO rng/net,
xHCI hub/keyboard. Same stock image config and source pins in A/B. No own
build/GDB overlaps timed commands; other host/CI load is uncontrolled. B is
`b6754c76`. Each IPC command is a fresh Remote connection to localhost24497:

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe call --size 4096 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 4096 --messages 256 --rounds 5
lua -e 'local a={"bin://echo.pxe","-n"}; local r=pyxis.run; local n=pyxis.monotonic_ns; assert(r(a)==0); for i=1,10 do local t=n(); assert(r(a)==0); print(i,n()-t) end'
```

All samples verify; final status0 with complete Remote drain. Milliseconds,
median(range), five IPC/ten launch samples:

| Workload | Baseline | Assignment capture |
| --- | --- | --- |
| CALL64 completion | 103.359(87.275–115.569) | 138.674(106.328–216.496) |
| CALL4096 completion | 102.274(84.506–113.371) | 130.821(110.122–175.239) |
| SEND64 completion | 14.125(12.751–17.525) | 21.238(14.671–38.425) |
| SEND64 admission | 1.194(1.184–1.274) | 1.737(1.336–3.203) |
| SEND4096 completion | 12.516(11.770–28.169) | 25.471(14.660–45.880) |
| SEND4096 admission | 1.267(1.246–2.570) | 1.965(1.339–4.309) |
| Echo launch through exit | 9.710(4.867–15.166) | 9.547(5.685–21.379) |

Retain the higher B IPC measurements; this small nested pair does not establish
native cost or cause. There is no new steady-state IPC path; resource capture
runs once, and assignment runs only on a native owner request. Neither that
inspection nor overlapping ranges proves a timing regression absent. Whole-boot,
native capture latency and assignment cost remain unmeasured.

## QEMU/GDB boundary

Four CPUs: ACPI ready; owned PCI resource capture READY with1root/4windows/
2reservations, complete firmware map, no allocation records. Ordinary spaces
and Remote work. A separate1CPU LPSS integration boot (`00980766`, includes
same allocator) stops at `pci_discover` using QEMU's GDB. Firmware has configured
an empty root port, cold-created with `mem-reserve=16M,pref64-reserve=16M`.
An existing QEMU e1000e is then added behind it using the monitor, before the
kernel's one-time scan:

```text
device_add e1000e,id=barforeign,bus=barport,addr=0,romfile=
```

The configured bridge windows remain unchanged. QEMU reports unassigned memory
BAR0/1/3, sizes128/128/16KiB, before and after boot. Kernel inventory sees
`8086:10d3` on bus1. Capture is READY, inventory count10, Remote log/exit status0;
GDB confirms LPSS absent and no allocation records. This is a genuine foreign
unassigned device, not an LPSS emulator or positive assignment. No new test/probe
program, injected call, test infrastructure or USB gadget.

## Native gate and latest integration

Vela is the first requesting native consumer: #894 uses the checked D3hot
fence with decode/BME kept off, prepares its native window early, then assigns
only after the exact live companion/root capture. It verifies actual BAR0 size
before MEM enable/MMIO, resets LPSS with internal DMA held off and reads one
HID descriptor. Native assignment/readback/descriptor qualification is pending.
The prior owner run at7003c117 passed D0 but stopped at BAR0/base0; its evidence
remains in the touchpad experiment record.

Before the final packet, the stack rebased onto main9155b226, adopting its
userland15e943c1 pin and matching CI2776 component bundles; other pins unchanged.
The timing table above keeps its original matched pins. The packet will include
exact revisions/hashes and a human RUNSHEETS checklist: normal menu entry,
boot:// tools without quotes, saved home:// output, normal power-off and
stick-deploy restore. No limine.conf changes, attended blanking option or sync
workaround. No native result is claimed here.
