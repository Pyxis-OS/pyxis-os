# QEMU HD Audio playback evidence

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Captured 2026-10-08. See the [result report](../../audio-investigation.md) and the
[milestone with accepted defaults](../../../wip/hda-playback.md). All HDA code is private probe code on unmerged
branches; this record accompanies a documentation-only PR.

## Revisions and matching inputs

| Image | Clean measured revision |
| --- | --- |
| Baseline, no HDA consumer | `a500daf` (main `67e14be` plus investigation brief) |
| Initial stop at payload | [`a7c3488`](https://git.internal/PyxisOS/pyxis-os/commit/a7c34886dab23a7dc87716cd53184924950a3455) |
| Final bounded silence tail | [`f04cf5a`](https://git.internal/PyxisOS/pyxis-os/commit/f04cf5a3bb97ec92e11af0889327d2a84c3ae737) |

Integrated branch `probe/audio-playback`: controller preparation from `40cde96` on `probe/audio-controller` and codec
enumeration from `3e53307` on `probe/audio-codec`; integration only adds `kernel/audio/*.c` to the source list and
prepare/start hooks in `kernel/init.c`, and the later stop change belongs only to the probe branch. None is proposed for
production merge. Ordinary `make -j16 image PREBUILT="sdk userspace ports"` in the LLVM 23.1.3/`49e2c1a` builder passed at
baseline and both probe revisions (the final local build without warnings; a host kconfiglib/genconfig import was mounted
into the builder, with no container rebuild or package source change). The bundle verifier checked the pinned
SDK/userland/ports payloads and only the kernel changed: the complete initrd was identical in all three images. The
SDK/userland/ports bundles come from clean `6c7ced2`, with inputs verified against this main: userspace `df780027`, ports
`2a5c30f4`, fs `b427df29`, lwIP `a1aadb91`. No CI result for a documentation head is evidence that probe code ran in CI.

## Guest configuration and commands

Stock Fedora QEMU 10.2.2 (`qemu-10.2.2-1.fc44`), Q35, four cores with one thread each, `-cpu max`, 8 GiB, nested KVM; not
the ThinkPad, and with no host audio server, physical audio device or passthrough. The WAV backend selects 48 kHz, two
channels, signed 16-bit PCM, default codec properties `mixer=on,use-timer=on`; duplex's ADC backend warning is expected
with this output-only backend and no capture path was exercised. The final output-only run, entered manually:

```sh
qemu-system-x86_64 -machine q35 -accel kvm -cpu max -smp 4 -m 8G \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd -drive if=pflash,format=raw,file=FRESH_VARS \
  -display none -serial file:SERIAL -monitor stdio -gdb tcp:127.0.0.1:1243 \
  -drive if=none,id=audio_cd,format=raw,media=cdrom,readonly=on,file=pyxis.iso \
  -device virtio-scsi-pci,id=audio_scsi,disable-legacy=on -device scsi-cd,bus=audio_scsi.0,drive=audio_cd,bootindex=1 \
  -object rng-random,id=audio_rng,filename=/dev/urandom -device virtio-rng-pci,rng=audio_rng,disable-legacy=on \
  -device virtio-net-pci,netdev=audio_net,disable-legacy=on \
  -netdev user,id=audio_net,hostfwd=tcp:127.0.0.1:2351-10.0.2.15:2323 \
  -audiodev wav,id=audio,path=OUT.wav,out.frequency=48000,out.channels=2,out.format=s16 \
  -device intel-hda,id=hda -device hda-output,bus=hda.0,audiodev=audio
```

Duplex changes only the codec to `hda-duplex`, the guest name and the variable and capture file names; the baseline uses
the baseline image, `hda-output`, `-serial mon:stdio` and no GDB listener. All guests were stopped cleanly through the
monitor and WAV headers inspected after exit; serial evidence kept only HDA lines and no boot-output automation was added.
Read-only GDB on the matching ELF (`target remote 127.0.0.1:1243`, `p probe`, `p selected_route.length`,
`p selected_route.nodes`, detach; the final output-only capture prints `probe.failed`, `probe.shutdown`,
`probe.commands` and route length) made no inferior calls or state edits. The remote client ran `lspci -n`, five
`echo audio-check-N` commands and `exit` after the probe finished (baseline, initial output and final duplex runs): all six
returned 0, as responsiveness evidence only, with no concurrent refill test or round-trip benchmark.

## Raw observations and sample inspection

The serial records held ring counts, all codec responses, route controls, elapsed HPET/WALCLK, stream positions and status
and shutdown readbacks; the report interprets their boundaries.

| Run | HPET playback ns | WALCLK ticks | Stop LPIB bytes | Polls | BCIS observations | WAV frames | Tone frames present |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Initial output | 974647670 | 23392845 | 192088 | 173 | 5 | 46288 | 46288 |
| Initial duplex | 974967240 | 23399232 | 192596 | 171 | 5 | 46751 | 46751 |
| Final output | 1143474160 | 27443286 | 224956 | 201 | 6 | 54501 | 48000 |
| Final duplex | 1144048680 | 27457208 | 225112 | 204 | 6 | 54484 | 48000 |

WAV file sizes were 44 bytes (baseline, zero frames), 185196 (initial output), 187048 (initial duplex), 218048 (final
output) and 217980 (final duplex). They were inspected with Python's `wave`/`struct`: two channels, two-byte samples,
48,000 Hz, uncompressed. For every frame index `i < 48000` and `p=i%48`: `x=-8192+floor(p*32768/48)` when `p<24`,
otherwise `x=24576-floor(p*32768/48)`; both samples must equal `x` and all later frames must be zero. **Every captured
sample matched** in all four files: the initial captures were exact prefixes and the final captures held the whole tone
plus silence. Peaks were −8192/+8192 and positive-going crossings were 48 frames apart (1 kHz). Identical channels do not
test channel ordering, and no WAV generated from the formula replaces captured output.

QEMU's codec has an 8192-byte post-DMA timer buffer separate from the output voice; traversing one 32768-byte silent period
avoided observed truncation, a measured probe workaround and not a portable drain guarantee. LPIB, WALCLK and WAV duration
are not interchangeable latency or consumption promises. Native playback, IRQ delivery, sustained refill and mixing,
independent channels, ring wrap, command faults and failure recovery remain unmeasured; no native device was accessed.

## Native codec inventory supplied after review

Claude read Fedora's ThinkPad codec through read-only commands and supplied
[thinkpad-alc257-codec.txt](thinkpad-alc257-codec.txt) on 2026-10-08. It is kept unchanged (9,587 bytes, SHA-256
`3c1799d0ec3d75c96a4a3cdf6a032728bb59eae1100b1c2e22c636bff18509d8`). The
[review of #549](https://git.internal/PyxisOS/pyxis-os/pulls/549) associates it with AMD analog controller `07:00.6`,
`1022:15e3`; the file identifies ALC257 `0x10ec0257`, subsystem `0x17aa5081`. See the
[report's native handoff](../../audio-investigation.md#native-handoff) for pin, DAC, amplifier and EAPD observations and
the limits of captured Fedora state. The dump supplies no Fedora kernel version, exact capture command or jack-state
record; it is native Linux inventory, not native Pyxis playback evidence.
