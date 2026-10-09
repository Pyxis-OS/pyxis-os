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

## Matched implementation observations (2026-10-09)

The compared kernel is clean revision `3fd540fb` on `audio/native-amd`, before
integrating later main changes. `make -j16 image PREBUILT="sdk userspace ports"`
passed with the existing verified bundles. Public ABI and pins/configuration
are unchanged; initrd is byte-identical to the baseline. Source inspection
confirms QEMU's existing 8192-byte/1 ms guard path is retained.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `7969a159dce3eee6a3bb5bd09b2b0e635bcce5613dc33f505b6c498a92253824` |
| ISO | `d5426eb521c41fd52ec86bfeb7ab9370892927992b14023de07438784469fc31` |

Same QEMU/device/CPU/display configuration and no playback during idle windows:

| Implementation window | Elapsed s | BSP guest % | BSP host-thread total % | Whole QEMU % |
| --- | ---: | ---: | ---: | ---: |
| 1 | 15.000412 | 5.27 | 10.33 | 20.07 |
| 2 | 15.000391 | 5.20 | 10.00 | 19.53 |

Eight separate manual presenter observations: **median 1.57997 ms**, range
**1.45517–1.82723 ms**. Tick samples: 145517, 157556, 151908, 161697,
158438, 182723, 149903, 160940. The worker was parked with zero refill/IRQ work,
22 commands/responses and stream/rings stopped. A different unrelated QEMU/GDB
workload was present on the shared host. A subsequent repeat of the unchanged
baseline image used the same current configuration and two quiet windows:

| Repeated baseline window | Elapsed s | BSP guest % | BSP host-thread total % | Whole QEMU % |
| --- | ---: | ---: | ---: | ---: |
| 1 | 15.000406 | 6.20 | 10.40 | 19.73 |
| 2 | 15.000385 | 6.53 | 10.73 | 20.00 |

Repeated baseline presenter ticks were 129693, 98128, 108836, 136130,
121815, 118981, 80131, 85203: median **1.13909 ms**, range
**0.80131–1.36130 ms**. The current idle totals overlap the implementation and
differ from the earlier baseline, demonstrating host/time variation. The
implementation's eight presenter samples remain slower than both baseline
samples; no stable presenter improvement or native cost equivalence is claimed.
The parked audio worker performs no periodic work by inspection and debugger
state. These small sequential samples do not isolate a causal presenter cost;
repeat native matched scene observations in the batch.

Manual `pcm 1000 500 5` and
`pcm --pause-ms 250 --repeat 2 --gap-ms 150 1000 500 2` completed successfully.
Before release, starvation was 2 for the uninterrupted producer and 4 per paused
repeat; discontinuity stayed zero and state stayed READY. WAV analysis found
**432,000 PCM frames with zero content mismatches** against the producer's
triangle waves after removing deliberate/underrun silence. This verifies PCM
content, not continuous audible timing or physical sound. After all producers
completed, debugger inspection showed five starts/stops, 970 refills,
max commit 502,530 ns, no failure, command/stream DMA parked and all session
slots released. No debugger halted active playback.

## Current-main integration

The ordinary source image build passed after integrating main `5cf60bc3`;
clean kernel revision `b7148af1` adds only the native FIFO trace and explanatory
comment after that integration. Userland is `ff278aec`, ports `f2da003d`,
filesystem/lwIP unchanged. The same compiler/configuration is used. Main's
timer-read optimizations and new userland/ports pins make this an integration
check, separate from the matched performance comparison above.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `9b90d73b7d1786012252db0d4227be3e835130a7190455417db75f1e230e8025` |
| ISO | `45bb4b384d0df11ba0468882dc9e1bce7ece1d4562d11bb39307356c68822ca2` |
| Initrd | `73a9be50f99d0d8e994cb86c3fed01938141c485ea612167eb9172979fe3e2d6` |

Same four-CPU/8 GiB Q35 nested KVM devices, with `hda-duplex` for playback only:
`pcm 1000 500 2` completed successfully, starvation 2, discontinuity 0, READY
before release. WAV contained all **96,000 PCM frames without content mismatch**
after removing silence. Stopped-state inspection showed one start/stop, max
commit 341,920 ns, no failure, rings/stream parked and all slots released.
QEMU's missing `adc` backend warning is expected; recording is not supported.
Booting the same image without HDA devices kept absence quiet, rejected PCM
ACQUIRE with CALL_UNAVAILABLE (status 6), and left ordinary shell commands
serviceable. All owned QEMU, debugger and remote-client jobs were stopped.

The final source build at `6c4a4aaa` also passed with verified unchanged bundles.
It factors the accepted native 20 ms value into one constant; the audio object's
compiled `.text` is byte-identical to the integration build. No guard or runtime
behavior changed after these boots.

## Native implementation and remaining checks

Controller matching/snoop constraints, ALC257 shared-DAC output selection and
the FIFO-derived 20 ms native guard are implemented. The owner accepted that
guard and keeping D3+BME refused on **2026-10-09**; no handoff helper is added.
Boot power/COMMAND/PME/NoSoftRst are recorded through ktrace, with specific
unavailable reasons. Public ABI, eight-session capacity, DMA/session queues,
write limits, live-jack deferral and reboot-only fault recovery stay accepted.
Actual AMD boot state, coherence, cold-init/speaker/headphone and one/eight-session
results belong to the [owner's native batch](../../../wip/hda-native.md#owner-run-native-batch-after-implementation-acceptance),
not a QEMU inference. Native eight-session qualification remains open.
