# HD Audio playback investigation results

Investigation completed **2026-10-08**, from main `67e14be`, after #546 merged.
A private BSP worker initialized QEMU's Intel HDA controller, exchanged codec
verbs through CORB/RIRB, discovered an analog output route and played a known
PCM buffer through a BDL. The final WAV captures contain the complete known
signal followed by silence. This is emulated playback evidence; no native
ThinkPad sound, production audio interface or SDL2 audio backend is established.

The [experiment record](experiments/audio-investigation/README.md) identifies
exact revisions, build inputs, commands, captures and limitations. The
[playback milestone](../wip/hda-playback.md) records three defaults accepted by the
owner on **2026-10-08**. No implementation task is assigned. QEMU closure with
a later native batch is carried forward for owner confirmation at closure.
Probe code stays on unmerged branches and is excluded from this documentation PR.

## Investigation steps and observations

1. **References and baseline.** Read Intel's HDA specification, QEMU 10.2.2
   controller/codec source and Pyxis PCI/DMA/BSP contracts. With `intel-hda` and
   `hda-output` present, the normal kernel enumerated `8086:2668` at `00:05.0`;
   it produced a 44-byte WAV header with zero frames. The same guest commands
   completed successfully before and after the probe. This checks post-probe
   responsiveness, not concurrent playback performance or quantified latency.
2. **Controller and commands.** QEMU reports HDA 1.0, `GCAP=0x4401`, four input
   and four output descriptors, no bidirectional streams and 64-bit DMA support.
   BAR0 is 16 KiB; the probe maps only the first 4 KiB containing its registers.
   It holds the link in reset before completing the PCI claim and BAR sizing,
   then allocates coherent CORB, RIRB, BDL and PCM backing before AP startup.
   An IF-enabled BSP worker releases reset and polls with bounded waits; HDA global
   interrupt delivery and PCI INTx remain masked. Both rings select 256 entries.
   The final output-only/duplex runs correlated **32/42 commands and responses**,
   with no unsolicited replies or reported ring errors. Immediate commands were
   unnecessary. Ring wrap and unsolicited reception were not exercised.
3. **Codec and path.** QEMU output-only codec ID is `1af4:0012`; duplex is
   `1af4:0022`. Both expose audio function group 1 and output pin **3 → DAC 2**.
   Enumeration discovers this connection, PCM/rate capabilities and amplifier
   controls rather than hardcoding node numbers. The route supports 48 kHz,
   16-bit stereo PCM. It enables pin output, unmutes both DAC channels using
   the advertised gain offset 74, and checks converter format and stream tag 1.
   The emulated function group advertises no power-control capability and reads
   as D0. No EAPD operation was needed for these pins.
4. **Known PCM and stop.** The signal is 48,000 stereo frames of a 1 kHz integer
   triangle, signed 16-bit, peaks ±8192, identical channels. Output descriptor 4
   uses eight 32,768-byte BDL periods over a 262,144-byte cyclic buffer. The
   192,000-byte tone is followed by zeroed backing. Initial runs stopped when
   LPIB reached the payload extent and captured only 46,288/46,751 tone frames.
   QEMU buffers after fetching DMA, so LPIB was insufficient evidence of audible
   drain. A later probe revision traverses one additional silent period before
   stopping. The final captures match **all 48,000 tone frames byte-for-byte**,
   followed by **6,501/6,484 zero frames**. They contain no unexpected nonzero
   samples. This measured margin works in these two runs; it is not a general
   hardware drain contract.
5. **Native requirements.** The checked-in ThinkPad inventory identifies analog
   controller **AMD `1022:15e3`, `07:00.6`**, separate from Renoir HDMI/DP
   `1002:1637`, the AMD audio coprocessor `1022:15e2` and dock USB audio. The codec
   is now identified by Claude's owner-supplied Fedora dump as **Realtek ALC257**,
   vendor `0x10ec0257`, subsystem `0x17aa5081`. Its analog output converters
   advertise 48 kHz/16-bit stereo. The [native handoff](#native-handoff) records
   pins, connections and controls; this is Fedora inventory/state evidence, not
   native Pyxis qualification. This agent did not access the Bluetooth host.
6. **Report and proposal.** This report retires the investigation checklist.
   The proposed production tasks separate controller ownership, codec routing,
   refill/underrun behavior, per-space authority, consumers and native closure.
   The owner accepted worker/session ownership, format and starting buffer tuning
   on 2026-10-08. Exact interface/lifetime policies still need task-specific review;
   no implementation task is assigned.

## QEMU measurements and interpretation

Stock QEMU 10.2.2, Q35, `-cpu max`, four guest CPUs, 8 GiB, nested KVM and fresh
OVMF variables. Audio goes to a 48 kHz stereo S16 WAV backend; no physical audio
output or passthrough. The kernel changed; the SDK, applications, ports and
complete initrd bytes stayed fixed across baseline and probes.

| Final run | Commands/replies | Controller ready, HPET | Playback run, HPET | Stop LPIB | Polled BCIS observations | WAV frames |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `hda-output` | 32/32 | 24.619 ms | 1143.474 ms | 224,956 bytes | 6 | 54,501 |
| `hda-duplex` (playback only) | 42/42 | 24.750 ms | 1144.049 ms | 225,112 bytes | 6 | 54,484 |

The programmed stop target is 224,768 bytes, nominally 1170.667 ms at
192,000 bytes/s. HPET elapsed and HDA's 24 MHz WALCLK elapsed agree closely;
neither is an end-to-end sound latency measurement. QEMU fetches ahead and its
voice/backend has a separate buffer. Its WAV samples, stream position and wall
clock describe different stages. The 170.667 ms probe period and 5 ms polling
interval were convenient investigation parameters, not proposed production
periods. Polled completion flags can coalesce and are not an interrupt count.

Both final runs report no FIFO/descriptor error, RUN clear and stream reset
acknowledged. QEMU retains LPIB after stream reset; it does not reset to zero in
these observations. Shutdown confirms CORB/RIRB stopped, link reset and PCI bus
mastering off. GDB observes terminal, nonfailed probe state and retained DMA.
Backing remains allocated until reboot, even after successful stop. No failed
stop, timeout recovery, device removal, repeated stream opening or DMA reuse
was qualified. Duplex reports an unavailable ADC backend because WAV is output
only; capture was neither configured nor attempted.

The output channels deliberately carry the same samples, so this check does
not prove independent left/right routing. No sustained refill, mixing, IRQ
handling, scheduling-load tolerance, underrun recovery or native latency was
measured. QEMU's simple codec and coherent DMA model cannot qualify OEM pin,
power, amplifier, cache/position or interrupt quirks.

## Consumer constraints

The pinned [SDL2 port](sdl2.md) is 2.32.10, with audio devices and threads omitted.
Its normal `OpenAudioDevice` starts `SDL_RunAudio`/`SDL_CaptureAudio` through
`SDL_CreateThreadInternal` unless the backend provides its own callback thread.
`SDL_QueueAudio` still relies on that processing path. Conversion and WAV
loading already exist; playback does not. A backend needs real callback
execution, queueing, waits and close semantics. Successful fake threads,
calling arbitrary application callbacks in kernel context or depending on
unrelated event polling are not a valid bridge. Thread/callback execution needs
its own accepted solution before the SDL2 consumer task can start.

Quake's current recipe links `snd_null`, with no sound producer or native DMA
adapter. Upstream's sound mixer and `SNDDMA_*` adapter would need a separately
reviewed consumer change. A userspace ring index is not automatically a hardware
play position. These consumer repositories were inspected without edits or pin
changes; the investigation adds no userspace or libc API.

## Native handoff

Claude supplied the read-only Fedora codec dump on **2026-10-08**, with its
association to analog controller `07:00.6`, AMD `1022:15e3`, recorded in the
[review of #549](https://git.internal/PyxisOS/pyxis-os/pulls/549). The
[full dump](experiments/audio-investigation/thinkpad-alc257-codec.txt) is retained
unchanged, SHA-256
`3c1799d0ec3d75c96a4a3cdf6a032728bb59eae1100b1c2e22c636bff18509d8`.
Codec address is 0, AFG is `0x01`, revision `0x100001`, vendor `0x10ec0257`
(**Realtek ALC257**), subsystem `0x17aa5081`. The supplied file does not record
the Fedora kernel version, card pathname, capture command or jack-presence state;
these are not inferred. No further codec-dump request is needed.

| Output | Pin / default configuration | Advertised DAC connections | Fedora controls at capture |
| --- | --- | --- | --- |
| Internal speaker | `0x14` / `0x90170110`, fixed | `0x02` | OUT, EAPD `0x2`, pin output amp unmuted |
| Right-side headphone jack | `0x21` / `0x04211020`, jack | `0x02`, `0x03` (selected) | OUT + HP, EAPD `0x2`, pin output amp muted; unsolicited tag 1 enabled |

Both output DACs `0x02` and `0x03` are stereo PCM, advertise 44.1/48 kHz and
16/20/24-bit samples, and are in D0 at capture. Thus **48 kHz S16 stereo is
advertised by the actual analog converters**, not just the AFG default (which
also lists 96/192 kHz). Their amplifier offset and maximum step are `0x57`;
current gains are `0x55`/`0x34`. The pins have mute-capable output amplifiers,
and both advertise EAPD. These captured gain/mute values are Fedora state,
not selected Pyxis defaults or a sufficient cold-init sequence.

The mic jack is pin `0x19`, default `0x04a11030`; recording stays outside scope.
Three GPIOs advertise unsolicited/wake capabilities but are disabled in the
captured state. Vendor widget `0x20` advertises 142 processing coefficients;
the file contains no coefficient initialization sequence. EAPD/power handling
and possible Lenovo-specific Realtek fixups remain bring-up work; this data does
not prove a GPIO toggle or particular vendor fixup is needed. Inspect relevant
source with licence/provenance before adapting any fixup. The QEMU probe's
line-out/headphone preference does not itself select the native fixed speaker;
production route and jack policy still need review.

Native Pyxis then needs cold-boot speaker and headphone checks, controller
address-width/position/interrupt behavior, stop/reset ownership, sustained
refill and underrun observations under normal load, and measured usable latency.
Speaker/headphone selection and jack events need policy before implementation.
Do not bind the GPU, coprocessor or USB dock as an analog fallback. Suspend/resume,
recording and HDMI/DP remain separate scopes. Native qualification remains open.
The owner's established practice is QEMU closure with native checks in a later
ThinkPad batch. The milestone carries that closure alternative forward and must
confirm it with the owner at closure, recording retained native checks as debt
if confirmed. No native completion is implied by the accepted tuning defaults.

All task-owned guests, debugger connections, clients and builds are stopped.

## Primary references

- [Intel HDA specification, revision 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf): controller/stream programming and codec verbs.
- [QEMU 10.2.2 controller](https://github.com/qemu/qemu/blob/v10.2.2/hw/audio/intel-hda.c) and [codec](https://github.com/qemu/qemu/blob/v10.2.2/hw/audio/hda-codec.c): response-status handling, DMA position, codec buffering and reset behavior.
- [QEMU audio options](https://www.qemu.org/docs/master/system/qemu-manpage.html#audio): WAV backend configuration.
- [Pinned SDL2 audio core](https://github.com/libsdl-org/SDL/blob/5d249570393f7a37e037abf22cd6012a4cc56a71/src/audio/SDL_audio.c): queued audio and callback worker creation.
- [PCI ownership](../devices/pci.md), [BSP/scheduler contracts](../kernel/smp.md), [ThinkPad inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt).
