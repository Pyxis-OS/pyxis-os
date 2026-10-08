# Sleep wake baseline

Measured on 2026-10-08 before implementing the
[deadline wake proposal](../../../kernel/timekeeping.md).
Both workloads used Pyxis `9acf597fec897119256a0c0044785771b19f9132` with
unchanged kernel source and pinned dependencies:

| Input | Revision |
| --- | --- |
| userspace | `df78002764768b05b2843248d1e1f4cf6091d695` |
| ports | `a3b154b9d627d8ee389bfefce35a87346adf5a0b` |
| fs | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |
| SDL2 | `5d249570393f7a37e037abf22cd6012a4cc56a71`, 2.32.10 |
| quakegeneric | `13052102577c629650cf07a46151a4b6e1b19c3c` |

The published LLVM 23.1.3 / `49e2c1a` builder completed ordinary
`make -j16 image`. Default effective Kconfig includes xHCI and
`CONFIG_HPET_MAINTENANCE_TICKS=120`, with ordinary `-O2 -g3` kernel flags.
The standalone SDL measurement consumer was linked against this image's
exported SDK and SDL2 archive using the SDK make fragment; it changes no library
or kernel. Its source is [sdl-delay.c](sdl-delay.c), retained as the requested
baseline workload rather than a normal application, self-test or CI target.

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `8d29f5330e46009c33e9dff370ea3a0b4f1238b6be56009362c43295cdf53538` |
| Measurement ISO | `1bfad92298859acc5b530faca7d387bf0d6c64f7c87697b01adc37f70331a45a` |
| Measurement initrd | `7de899d4f50579a605d815830f4a970bed6a8f4f02d14837ad6cc0321b95de8c` |
| SDL workload source | `e8b97947a66c6d14594b0dbccc82356ed6b994d60cd4404d1f74a70580a970bb` |
| SDL workload P1F | `cd12a8fc67ac9d8c581f2a7d522d7ee36382036d1a37dee5ec2f0f4b19ae23d4` |
| Ordinary Quake P1F | `a99f6b4e02a4d3ff70909490fc141b7e28bf303d6998147ac6c04e29b4b2f4d8` |
| Effective Kconfig | `ac12acc93c3fcbbdff1ace10d95b8f3cdf883e1c09d21ce413ba09df8324a98b` |

The measurement initrd differs from the ordinary image only by adding the
standalone executable `sleep-baseline.pxe`. It was rebuilt with the existing
sorted GNU cpio/newc assembly flags, then assembled with the ordinary ISO flags.
Keep this initrd and the same executable/library bytes for the after run;
replace only the kernel. Local artifacts live in
`<worktree>/build/`.

## Guest configuration

Stock QEMU 10.2.2 on the development host, nested KVM, q35, four CPUs (four cores,
one thread each), 8 GiB, `-cpu max`, standard VGA boot framebuffer 1280x800,
content area 1280x768. All four ordinary spaces and networking were ready.
Normal scheduler placement/migration remained enabled. No other task-owned VM
or build ran during sampling. The NIC/RNG use modern VirtIO; the read-only
CD-ROM uses VirtIO SCSI to avoid the host's known AHCI CD-ROM crash.

```sh
cp /usr/share/OVMF/OVMF_VARS.fd build/sleep-baseline-vars.fd
qemu-system-x86_64 -name 'Pyxis sleep baseline' \
  -machine q35 -accel kvm -cpu max -rtc base=utc \
  -smp cpus=4,sockets=1,cores=4,threads=1 -m 8G \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=build/sleep-baseline-vars.fd \
  -display none -serial mon:stdio \
  -drive if=none,id=sleep_cd,format=raw,readonly=on,media=cdrom,file=build/sleep-baseline.iso \
  -device virtio-scsi-pci,id=sleep_scsi,disable-legacy=on \
  -device scsi-cd,bus=sleep_scsi.0,drive=sleep_cd,bootindex=1 \
  -device virtio-rng-pci \
  -device virtio-net-pci,netdev=sleep_net,disable-legacy=on \
  -netdev user,id=sleep_net,hostfwd=tcp:127.0.0.1:2347-10.0.2.15:2323 \
  -gdb tcp:127.0.0.1:1239
```

Raw OVMF code SHA-256:
`904bfa3e0d966372b43b804c4fe323ae63751566687c2bfdf52ca947f47eb13a`;
fresh variables template:
`6ed987af3a3c155be71665f510eae3e007eda9b8b94afd59d45e91c4a11565cc`;
QEMU executable:
`27cd395848940fc6482256d85096fc64bc4fe3f3e909824d51c202f8314cd9e9`.

## SDL_Delay(16), unprofiled

The program creates a 640x480 streaming ARGB texture and software renderer,
scales it to logical 640x480, redraws/presents a changing color pattern, then
calls the real `SDL_Delay(16)`. It warms ten frames and records the next 300.
Performance-counter units are 1 GHz nanoseconds. It reports work, whole frame
and sleep intervals; summary output occurs after measurement. Event pumping
precedes each measured frame interval.

In Development, five invocations were typed individually through ordinary
keyboard input, with stdout redirected to the shared RAM home:

```text
sleep-baseline > s1
sleep-baseline > s2
sleep-baseline > s3
sleep-baseline > s4
sleep-baseline > s5
```

Remote's ordinary `cat home://sN` read each result after its command finished.
No debugger was attached during these runs. Values below are the reported
integer nanoseconds, before rounding for the prose conclusion.

| Run | Mean work ns | Mean frame ns | Mean sleep ns | Minimum sleep ns | Maximum sleep ns |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 940275 | 24991163 | 24050888 | 21242390 | 32217720 |
| 2 | 986121 | 25785071 | 24798949 | 21703590 | 32356390 |
| 3 | 1002962 | 25565202 | 24562239 | 21386190 | 32317460 |
| 4 | 1117933 | 25619329 | 24501395 | 20092450 | 32333110 |
| 5 | 953108 | 25281606 | 24328498 | 22121410 | 32259600 |

All five runs recorded zero sleeps shorter than 16 ms. Median per-run mean
frame time was **25.565202 ms**, range **24.991163–25.785071 ms**. Median mean
work was **0.986121 ms**; median mean sleep was **24.501395 ms**, range
**24.050888–24.798949 ms**. This reproduces the pacing problem on fresh main;
the intended sleep plus this measured work would be about 17 ms. The sleep
interval includes syscall/scheduling overhead and can exceed the delay of one
8.33 ms BSP tick; code inspection found the additional AP wake-tick path.

## Quake's ordinary 72 Hz cap, debugger-assisted

After all SDL runs, Development ran `quake +map e1m1` with the packaged shareware
data, ordinary 320x240 renderer/scaling, no input and `cls.timedemo=false`.
This measures the capped loop, not `timedemo` throughput. A debugger ELF linked
from the same objects was verified entry/loadable-byte identical to the packaged
P1F before use. No game or timer state was substituted.

Manual GDB used the matching kernel ELF, `add-symbol-file` for Quake and
`set may-call-functions off`. At `Host_Frame` entry, read `host_framecount` and
the HPET counter at `0xfffffe80402020f0`; period was inspected as 10 ns. Disable
the hardware breakpoint while the guest runs, interrupt after a manual interval,
enable it and continue to the next real Quake frame entry. Read both values
again and calculate completed frames per elapsed guest time. The breakpoint
was active only at sampling boundaries. The
[raw transcript](quake-baseline-gdb.txt) records the actual reads and checks.
Worktree paths in the transcript use `<worktree>` placeholders; numerical
readings are unchanged.

| Window | Completed frames | HPET ticks, 10 ns | Elapsed s | Frames/s |
| --- | ---: | ---: | ---: | ---: |
| 1 | 564 | 1147222217 | 11.47222217 | 49.162228 |
| 2 | 837 | 1625814470 | 16.25814470 | 51.481889 |
| 3 | 4425 | 9182532530 | 91.82532530 | 48.189320 |
| 4 | 525 | 1066580771 | 10.66580771 | 49.222714 |
| 5 | 1680 | 3405634209 | 34.05634209 | 49.330019 |

Median observed capped rate was **49.222714 FPS**, range
**48.189320–51.481889 FPS**. These are differently sized, manually sampled
windows of the same running map. They include scheduler/display/host variation
and debugger boundary effects; they are not native performance or a pure sleep
cost. After runs should repeat this workload/method and preserve the distinction
from the unprofiled SDL results. Native ThinkPad delay/cap behavior remains
unmeasured. All task-owned QEMU, debugger, client and build jobs were stopped.
