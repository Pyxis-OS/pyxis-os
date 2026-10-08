# QEMU HD Audio playback evidence

Captured 2026-10-08. See the [result report](../../audio-investigation.md) and
[milestone with accepted defaults](../../../wip/hda-playback.md). All HDA code is private probe
code on unmerged branches; this record accompanies a documentation-only PR.

## Revisions and matching inputs

| Image | Clean measured revision | ELF SHA-256 | ISO SHA-256 |
| --- | --- | --- | --- |
| Baseline, no HDA consumer | `a500daf` (main `67e14be` plus investigation brief) | `ad5f38d30bb01fe8efe75289399366c52155c19ca06a553cef8dbea2946c351c` | `06dbbf50ee0cf6e9acc34dad9cd68785b7bbc0efac7a9685f89a206714458c1d` |
| Initial stop at payload | [`a7c3488`](https://git.internal/PyxisOS/pyxis-os/commit/a7c34886dab23a7dc87716cd53184924950a3455) | `efb92a59a580e2742887a209861fee530c7428c88401942b1c4b002186b908f2` | `05846681693117fb06b925fe238c52b8a2a1d02ae95bb2d697f355bef697bdf4` |
| Final bounded silence tail | [`f04cf5a`](https://git.internal/PyxisOS/pyxis-os/commit/f04cf5a3bb97ec92e11af0889327d2a84c3ae737) | `6b6594e88d8aa32cbdbb5d4baa7f23b4a438b6826a852dafc42da3319d92814a` | `6ff8af7ecdb36a25a43c1a6519e231c068ac633601030e55ad613faf628ef4ce` |

Integrated branch: `probe/audio-playback`. Controller preparation originates
from `40cde96` on `probe/audio-controller`; codec enumeration from `3e53307` on
`probe/audio-codec`. Integration only adds `kernel/audio/*.c` to the source list
and prepare/start hooks in `kernel/init.c`. The later stop change belongs only
to the probe branch. None is proposed for production merge.

Ordinary `make -j16 image PREBUILT="sdk userspace ports"` inside the existing
LLVM 23.1.3/49e2c1a builder passed at baseline and both probe revisions. Final
local build had no warnings. A local host kconfiglib/genconfig Python import was
mounted into the builder; no compiler container rebuild or package source change.
The bundle verifier checked pinned SDK/userland/ports payloads; only the kernel
changed. The complete initrd SHA-256 was identical in all three images:
`cb7b47c9e3585407f92c936ea5f6e5d5d07bb314ad4d9287770bc745be264d69`.

The retained [baseline kernel](baseline-kernel.txt), [probe kernel](probe-kernel.txt),
[effective configuration](kernel.config), [SDK](bundle-sdk.txt),
[userland](bundle-userspace.txt) and [ports](bundle-ports.txt) records give
compiler/configuration/ABI/pin provenance. SDK/userland/ports bundles originate
from clean `6c7ced2`, with unchanged inputs verified against this main:
userspace `df780027`, ports `2a5c30f4`, fs `b427df29`, lwIP `a1aadb91`.
No CI result for a documentation head is evidence that probe code ran in CI.

## Guest configuration and commands

Stock Fedora QEMU 10.2.2 (`qemu-10.2.2-1.fc44`), Q35, four cores, one thread per
core, `-cpu max`, 8 GiB RAM, nested KVM. This environment is not the ThinkPad.
No host audio server, physical audio device or PCI/USB passthrough was used.
The WAV backend selects 48 kHz, two channels, signed 16-bit PCM. Default codec
properties are `mixer=on,use-timer=on`. Duplex's ADC backend warning is expected
with this output-only WAV backend; no capture path was exercised.

OVMF code SHA-256: `904bfa3e0d966372b43b804c4fe323ae63751566687c2bfdf52ca947f47eb13a`.
Fresh OVMF variables SHA-256: `6ed987af3a3c155be71665f510eae3e007eda9b8b94afd59d45e91c4a11565cc`.
QEMU executable SHA-256: `27cd395848940fc6482256d85096fc64bc4fe3f3e909824d51c202f8314cd9e9`.

The final output-only run, entered manually from its worktree:

```sh
cp /usr/share/OVMF/OVMF_VARS.fd build/audio-vars-drain.fd
qemu-system-x86_64 -name audio-probe-output-drain \
  -machine q35 -accel kvm -cpu max -smp 4 -m 8G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=build/audio-vars-drain.fd \
  -display none -serial file:build/audio-output-drain-serial.txt -monitor stdio \
  -gdb tcp:127.0.0.1:1243 \
  -drive if=none,id=audio_cd,format=raw,media=cdrom,readonly=on,file=build/pyxis.iso \
  -device virtio-scsi-pci,id=audio_scsi,disable-legacy=on \
  -device scsi-cd,bus=audio_scsi.0,drive=audio_cd,bootindex=1 \
  -object rng-random,id=audio_rng,filename=/dev/urandom \
  -device virtio-rng-pci,rng=audio_rng,disable-legacy=on \
  -device virtio-net-pci,netdev=audio_net,disable-legacy=on \
  -netdev user,id=audio_net,hostfwd=tcp:127.0.0.1:2351-10.0.2.15:2323 \
  -audiodev wav,id=audio,path=build/audio-output-drain.wav,out.frequency=48000,out.channels=2,out.format=s16 \
  -device intel-hda,id=hda -device hda-output,bus=hda.0,audiodev=audio
```

Duplex changes only the codec to `hda-duplex`, guest name and fresh variable/
capture filenames. Initial runs use the initial image and `audio-output`/
`audio-duplex` capture names. Baseline uses the baseline image, `hda-output`,
`audio-baseline` filenames, `-serial mon:stdio` and no GDB listener. All guests
were stopped cleanly through the QEMU monitor; WAV headers were inspected after
exit. Serial evidence retains only HDA lines; no boot-output automation was added.

Read-only debugger observations used the matching ELF:

```sh
gdb -q -batch build/caelum.elf \
  -ex 'target remote 127.0.0.1:1243' -ex 'p probe' \
  -ex 'p selected_route.length' -ex 'p selected_route.nodes' -ex detach
```

The output-only final capture instead prints `probe.failed`, `probe.shutdown`,
`probe.commands` and route length. No inferior calls or state edits were used.
Retained transcripts: [initial output](audio-output-gdb.txt),
[final output](audio-output-drain-gdb.txt), [final duplex](audio-duplex-drain-gdb.txt).

The existing remote client ran `lspci -n`, then five `echo audio-check-N` commands,
then `exit`. The [baseline](audio-baseline-remote.txt),
[initial output](audio-output-remote.txt) and
[final duplex](audio-duplex-drain-remote.txt) transcripts decode its machine
records and retain completion statuses. All six workload commands returned 0.
They ran after the probe finished: responsiveness evidence only, no concurrent
refill test or numerical round-trip benchmark.

## Raw observations and sample inspection

Serial records: [initial output](audio-output-serial.txt),
[initial duplex](audio-duplex-serial.txt), [final output](audio-output-drain-serial.txt),
[final duplex](audio-duplex-drain-serial.txt). Ring counts, all codec responses,
route controls, elapsed HPET/WALCLK, stream positions/status and shutdown
readbacks are retained. The report interprets their boundaries.

| Run | HPET playback ns | WALCLK ticks | Stop LPIB bytes | Polls | BCIS observations | WAV frames | Tone frames present |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Initial output | 974647670 | 23392845 | 192088 | 173 | 5 | 46288 | 46288 |
| Initial duplex | 974967240 | 23399232 | 192596 | 171 | 5 | 46751 | 46751 |
| Final output | 1143474160 | 27443286 | 224956 | 201 | 6 | 54501 | 48000 |
| Final duplex | 1144048680 | 27457208 | 225112 | 204 | 6 | 54484 | 48000 |

WAV was inspected with Python's standard `wave`/`struct`: two channels, two-byte
samples, 48,000 Hz, uncompressed. For every frame index `i < 48000`, `p=i%48`:
`x=-8192+floor(p*32768/48)` when `p<24`, otherwise
`x=24576-floor(p*32768/48)`. Both samples must equal `x`; all later frames must
be zero. **Every captured sample matched** this expectation in all four files;
the initial captures were exact prefixes, the final captures contained the
whole tone plus silence. Peaks were −8192/+8192 and positive-going crossings
were 48 frames apart (1 kHz). Identical channels do not test channel ordering.

The [final output WAV](audio-output-drain.wav) is retained for inspection.
Other local WAVs are identified by hashes; their metrics and serial captures
are retained here. No WAV generated from the formula replaces captured output.

| WAV | Bytes | SHA-256 |
| --- | ---: | --- |
| Baseline (zero frames) | 44 | `4872b61c768dff943f9e021453d816f06e35adc8edd88ef183301f03e31b94a5` |
| Initial output | 185196 | `dbc982b4a1d6831a153c4ac7d94c4be7c38f8125ec9cf0d3a05df8cbfecaec3b` |
| Initial duplex | 187048 | `7e7505aa8480b24e16e80828b9049b03ea56cff51a34cd747d8cebc33c7efb30` |
| Final output | 218048 | `088bfe54593e5339e07c5cf2c1c58ee20aafb8fc4c83014a992fcee5d4f413c0` |
| Final duplex | 217980 | `e8740b33ee0cfe93163739e97733a15ff13d76ee607c1df486eea66a084e4f29` |

QEMU's codec has an 8192-byte post-DMA timer buffer, separate from the output
voice. Traversing one 32768-byte silent period avoided observed truncation;
this is a measured probe workaround, not a portable drain guarantee. LPIB,
WALCLK and WAV duration are not interchangeable latency/consumption promises.
Native playback, IRQ delivery, sustained refill/mixing, independent channels,
ring wrap, command faults and failure recovery remain unmeasured. All task-owned
processes stopped; no native device was accessed.


## Native codec inventory supplied after review

Claude read Fedora's ThinkPad codec through read-only commands and supplied
[thinkpad-alc257-codec.txt](thinkpad-alc257-codec.txt) on 2026-10-08. It is retained
unchanged (9,587 bytes), SHA-256
`3c1799d0ec3d75c96a4a3cdf6a032728bb59eae1100b1c2e22c636bff18509d8`.
The [review of #549](https://git.internal/PyxisOS/pyxis-os/pulls/549) associates
it with AMD analog controller `07:00.6`, `1022:15e3`; the file identifies
ALC257 `0x10ec0257`, subsystem `0x17aa5081`. See the
[report's native handoff](../../audio-investigation.md#native-handoff) for pin,
DAC, amplifier and EAPD observations and the limits of captured Fedora state.
The dump supplies no Fedora kernel version, exact capture command or jack-state
record. This is native Linux inventory, not native Pyxis playback evidence.
No physical-host access or new QEMU run occurred for this documentation update.
