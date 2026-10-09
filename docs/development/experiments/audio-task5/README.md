# Native audio task 5: baseline and qualification

Task 5 implements the [accepted AMD/ALC257 plan](../../../wip/hda-native.md).
The owner supplied the PCI/MSI inventory; native playback is not yet measured.
All figures below are QEMU observations. Keep raw records locally under build;
this report retains revisions, configuration, commands, hashes and ranges.

## Baseline before source changes (2026-10-09)

Baseline source `6f16e39c0211c6a11cdbcdd991be23171edde951` is merged main
`c2407b60` plus accepted plan documentation. It pins userland `68cff906`, ports
`a642f073`, filesystem `b427df29` and lwIP `a1aadb91`. Ordinary source
`make -j16 image` passed using LLVM 23.1.3/`49e2c1a`; no compiler rebuild.
Info logging, XHCI enabled, flush interval 30 s, HPET maintenance 120 ticks.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `9bd2655ba6c46047260caf8811b80844f5d74d16eaa1c3d5974aa39b0ca1bf06` |
| ISO | `7ab40ae5308c1006b8cf15c0311fdc31f2bc2edf93cc66ecf4d7970c4dc1fc8c` |
| Initrd | `980c58a7c4ed55a534f1aa0008d08f0218e5251c573b433c0dc99a51273c2b23` |

QEMU 10.2.2 Q35, nested KVM, `-cpu max -smp 4 -m 8G`, OVMF with fresh variables,
standard VGA 1280×800, VirtIO SCSI/RNG/network, `intel-hda` + `hda-output` into
48 kHz stereo S16 WAV. Default three-shell configuration; static Caelum selected.
No playback ran. The worker was available and untimed parked, with rings/stream
stopped, zero IRQ/refill work, BME off and retained DMA. WAV remained empty.

Two debugger-free sequential 15 s host-accounting windows used Linux /proc thread
CPU ticks and actual monotonic duration, with CLK_TCK=100. Linux utime includes
guest_time; percentages below mean one host CPU, not native utilization.

| Baseline window | Elapsed s | BSP guest % | BSP host-thread total % | Whole QEMU % |
| --- | ---: | ---: | ---: | ---: |
| 1 | 15.000412 | 4.67 | 9.00 | 17.47 |
| 2 | 15.000382 | 4.73 | 8.47 | 17.47 |

Eight manual hardware-breakpoint/finish observations of `space_present` used the
matching ELF, no inferior calls and 10 ns HPET ticks: **median 1.10241 ms**, range
**0.92007–1.55604 ms**. Tick samples: 155604, 92007, 92259, 102136, 142442,
111150, 121249, 109332. No timing instrumentation was added to the kernel.

The [existing task 2 launch command](../audio-task2/README.md#revisions-and-configuration)
provides the matching Q35/device/OVMF options; substitute these artifacts and
recorded initrd rather than a nine-space workload image. Presenter measurement
follows the [manual timing method](../../screenshot-qualification.md#matched-presenter-cost).
No task-owned build/debugger/other guest ran during CPU windows; an unrelated
SDL/QEMU/debugger session was present on the shared host. Repeated windows and
elapsed presenter ranges include nested-host variation and do not prove native
performance or a stable change. Owned baseline QEMU/GDB were stopped afterward.

## Implementation and remaining checks

Controller identification/coherence and stopped-start codec selection are being
implemented on `audio/native-amd`. Native FIFO/commit guard and D3+BME handoff
choices were presented to the owner before applying them; their decisions must
be recorded with implementation. Public ABI, eight-session capacity, DMA/session
queues, write limits, live-jack deferral and reboot-only fault recovery stay as
accepted. Repeat matched parked QEMU observations and ordinary playback checks
after integration. Actual AMD cold-init/speaker/headphone and one/eight-session
results belong to the owner's native batch, not a QEMU inference.
