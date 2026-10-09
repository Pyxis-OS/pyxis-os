# Sleep wake baseline

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Measured on 2026-10-08 before implementing the
[deadline wake proposal](../../../kernel/timekeeping.md), with Pyxis
`9acf597fec897119256a0c0044785771b19f9132` (unchanged kernel source) and these pins:

| Input | Revision |
| --- | --- |
| userspace | `df78002764768b05b2843248d1e1f4cf6091d695` |
| ports | `a3b154b9d627d8ee389bfefce35a87346adf5a0b` |
| fs | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |
| SDL2 | `5d249570393f7a37e037abf22cd6012a4cc56a71`, 2.32.10 |
| quakegeneric | `13052102577c629650cf07a46151a4b6e1b19c3c` |

The published LLVM 23.1.3 / `49e2c1a` builder completed ordinary `make -j16 image`
(default Kconfig: xHCI, `CONFIG_HPET_MAINTENANCE_TICKS=120`, `-O2 -g3`). The
measurement initrd differs from the ordinary one only by the standalone SDL consumer
`sleep-baseline.pxe`, linked against the image's exported SDK and SDL2 archive; its
source is [sdl-delay.c](sdl-delay.c), kept as the requested baseline workload rather
than a normal application, self-test or CI target. The after runs ([IPI](ipi.md),
[timer](timer.md)) keep this initrd and replace only the kernel.

## Guest configuration

Stock QEMU 10.2.2 on the development host: nested KVM, q35, `-cpu max`, four CPUs
(four cores, one thread each), 8 GiB, standard VGA 1280x800 (content area 1280x768),
fresh OVMF variables, VirtIO NIC and RNG, a read-only VirtIO SCSI CD-ROM (avoiding the
host's known AHCI CD-ROM crash), serial on the monitor and a GDB port. All four ordinary
spaces and networking were ready, scheduler placement/migration was normal, and no
other task-owned VM or build ran during sampling.

## SDL_Delay(16), unprofiled

The program creates a 640x480 streaming ARGB texture and software renderer, scales it
to logical 640x480, redraws and presents a changing pattern, then calls the real
`SDL_Delay(16)`. It warms ten frames and records the next 300 (1 GHz nanosecond
counter), pumping events before each measured frame. Five invocations were typed
individually in Development with stdout redirected to the RAM home
(`sleep-baseline > s1` through `s5`) and read afterward from Remote; no debugger was
attached.

All five runs recorded zero sleeps shorter than 16 ms. Median per-run mean frame time
was **25.565202 ms** (range **24.991163–25.785071 ms**); median mean work
**0.986121 ms**; median mean sleep **24.501395 ms** (range **24.050888–24.798949 ms**);
per-run maximum sleep 32.217720–32.356390 ms. This reproduces the pacing problem on
fresh main: the intended sleep plus the measured work would be about 17 ms. The sleep
includes syscall/scheduling overhead and can exceed the delay by one 8.33 ms BSP tick;
code inspection also found the additional AP wake-tick path.

## Quake's ordinary 72 Hz cap, debugger-assisted

After the SDL runs, Development ran `quake +map e1m1` with the packaged shareware data,
ordinary 320x240 scaling, no input and `cls.timedemo=false`. This measures the capped
loop, not `timedemo` throughput. A debugger ELF linked from the same objects was
verified entry/loadable-byte identical to the packaged P1F, and no game or timer state
was substituted.

Manual GDB used the matching kernel ELF, `add-symbol-file` for Quake and
`set may-call-functions off`. At `Host_Frame` entry it read `host_framecount` and the
HPET counter (10 ns period); the hardware breakpoint was disabled while the guest ran,
then enabled to stop at the next real frame entry, and completed frames per elapsed
guest time were calculated. Five differently sized windows (525–4425 frames,
10.67–91.83 s) gave a median of **49.222714 FPS**, range **48.189320–51.481889 FPS**.
These manually sampled windows include scheduler, display, host and debugger-boundary
effects; they are not native performance or a pure sleep cost. After runs repeat this
method and keep it distinct from the unprofiled SDL results. Native ThinkPad delay/cap
behavior remains unmeasured.
