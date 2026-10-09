# Native audio task 5: QEMU evidence

The [accepted AMD/ALC257 plan](../../../wip/hda-native.md) is implemented;
ThinkPad boot state, coherence and one/eight-session sound remain unqualified.
All measurements here are **QEMU**, not native results. Raw captures stay local.

## Inputs and commands

Baseline `6f16e39c` is main `c2407b60` plus plan docs; implementation `3fd540fb`
precedes subsequent main integration. Both use userland `68cff906`, ports
`a642f073`, filesystem `b427df29` and lwIP `a1aadb91`, identical initrd and
kernel configuration. Ordinary baseline `make -j16 image` and implementation
`make -j16 image PREBUILT="sdk userspace ports"` passed with LLVM 23.1.3/49e2c1a.
Info logging, XHCI enabled, flush interval 30 s, HPET maintenance 120 ticks.

| Artifact | Baseline SHA-256 | Implementation SHA-256 |
| --- | --- | --- |
| ELF | `9bd2655ba6c46047260caf8811b80844f5d74d16eaa1c3d5974aa39b0ca1bf06` | `7969a159dce3eee6a3bb5bd09b2b0e635bcce5613dc33f505b6c498a92253824` |
| ISO | `7ab40ae5308c1006b8cf15c0311fdc31f2bc2edf93cc66ecf4d7970c4dc1fc8c` | `d5426eb521c41fd52ec86bfeb7ab9370892927992b14023de07438784469fc31` |
| Common initrd | `980c58a7c4ed55a534f1aa0008d08f0218e5251c573b433c0dc99a51273c2b23` | Same |

QEMU 10.2.2 Q35, nested KVM, `-cpu max -smp 4 -m 8G`, fresh OVMF variables,
standard VGA 1280×800, VirtIO SCSI/RNG/network, `intel-hda` + `hda-output`,
48 kHz stereo S16 WAV. Default three shells, static Caelum selected, no audio.
Both engines were ready and untimed parked, 22 commands/responses, stream/rings
stopped, no IRQ/refill work. Existing QEMU 8192-byte/1 ms guards stay unchanged.

## Interleaved presenter comparison (2026-10-09)

This replaces the earlier sequential presenter comparison, which had different
host workloads. From **07:36:18–07:37:07 UTC**, manually alternate baseline,
implementation, repeated eight times. Only the sampled guest runs; its peer
stays stopped in GDB. One warmup per image precedes the retained window.
Read-only host job checks once per second found no unrelated QEMU/GDB or build
job in all 48 window observations. No remote client or source build ran.

Use the matching ELF, `set may-call-functions off`, `hbreak space_present`,
`continue`, read the 10 ns HPET counter, `finish`, then read its delta. No
sampling loop or kernel instrumentation. See the
[manual timing method](../../screenshot-qualification.md#matched-presenter-cost).

| Image | Samples | Median ms | Range ms |
| --- | ---: | ---: | ---: |
| Baseline `6f16e39c` | 8 | 1.190765 | 0.93635–3.23329 |
| Implementation `3fd540fb` | 8 | 0.983345 | 0.91350–2.52168 |

All retained samples are included. The earlier implementation slowdown did not
reproduce; broad overlapping ranges and eight pairs do not establish a speedup
or native cost equivalence. Both guests remained in Caelum with output stopped,
no pending IRQ error and no engine failure; owned jobs were stopped afterward.

## Earlier idle accounting

Historical, sequential two-window observations, **not** the interleaved run.
Each window lasts about 15.0004 s; Linux /proc ticks use CLK_TCK=100 and utime
includes guest_time. Percentages mean one host CPU. Shared-host workloads
changed between sets, so these are ranges, not an isolated audio cost estimate.

| Set | BSP guest % | BSP host-thread total % | Whole QEMU % |
| --- | ---: | ---: | ---: |
| Initial baseline | 4.67–4.73 | 8.47–9.00 | 17.47 |
| Implementation | 5.20–5.27 | 10.00–10.33 | 19.53–20.07 |
| Repeated baseline | 6.20–6.53 | 10.40–10.73 | 19.73–20.00 |

## Playback and integration

`pcm 1000 500 5` and
`pcm --pause-ms 250 --repeat 2 --gap-ms 150 1000 500 2` passed on `3fd540fb`:
432,000 PCM frames without content mismatch after removing silence. Starvation
was 2 for the uninterrupted producer and 4 per paused repeat; discontinuity 0,
READY before release. Then five starts/stops, 970 refills, max commit 502,530 ns,
no fault, all slots released and DMA parked. No debugger stopped active playback.

Ordinary source integration build passed with main `5cf60bc3`, kernel `b7148af1`,
userland `ff278aec` and ports `f2da003d`. Main's timer changes are separate from
the comparison above. Four-CPU `hda-duplex` playback (`pcm 1000 500 2`) preserved
all 96,000 PCM frames after removing silence, starvation 2/discontinuity 0,
one start/stop, max commit 341,920 ns, no fault and all slots released. Missing
QEMU ADC backend is expected; recording is unsupported. Without HDA, absence
was quiet, ACQUIRE returned UNAVAILABLE (6), and shell commands still worked.
Final source build `6c4a4aaa` also passed; disassembly changes only assertion
line numbers after factoring the same native 20 ms tuning into one constant.

Native batch instructions are in [#578](https://git.internal/PyxisOS/pyxis-os/pulls/578).
The owner froze that PR pending the native batch. D3+BME remains refused, with
boot-state ktrace; no PCI helper, live jack switching, recovery or batching.
Native eight-session speaker/headphone playback remains the closure gate.
