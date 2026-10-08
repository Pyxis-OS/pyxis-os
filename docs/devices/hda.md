# Private HD Audio engine

Caelum has a private playback engine for QEMU's `8086:2668` Intel HDA controller
with `hda-output` or `hda-duplex`. It discovers a checked 48 kHz S16LE stereo
analog route. Ordinary initialization never plays audio: command DMA stops,
PCI bus mastering is disabled, and the sole BSP worker parks without polling.
Normal boot prints only the `audio: ready ...` summary; an absent controller is
quiet. Controller/codec details use `LOG_LEVEL=trace`; failure explanations
remain visible at the normal log level.
This is the first implementation stage of the [playback milestone](../wip/hda-playback.md),
not a userspace sound interface. Public sessions, software mixing and runtime
IRQ/refill remain later tasks. SDL2 and Quake sound are unchanged.

## Ownership and initialization

[Preparation](../../kernel/audio/hda.c) reserves the unique supported controller
in a complete PCI inventory. It masks HDA delivery, halts firmware streams and
command rings, and asserts link reset before completing the claim or sizing
BARs. A checked 4 KiB BAR prefix contains the used registers; QEMU's complete
BAR is 16 KiB. Coherent, physically contiguous CORB, RIRB, BDL and PCM backing
is allocated on the BSP before AP startup. CPU mappings and physical DMA
addresses are separate. Unsupported/absent hardware remains optional.

The [BSP worker](../../kernel/audio/audio.c) alone releases reset, initializes
command transport and reads the codec graph. Runtime helpers assert that worker
and IF enabled; PCI command writes briefly disable interrupts under the existing
PCI/BSP contract. No allocation occurs during commands/playback, no AP mutates
the controller, and no IRQ handler performs driver work in this stage.

Codec detection is read after the required post-reset delay, before acknowledging
STATESTS. Presence can already be latched while reset is asserted; clearing it
before the read loses QEMU's detection event. CORB/RIRB use their largest supported
size and one solicited command at a time. Memory barriers precede publication
and response consumption. Responses must match the pending codec; unexpected
progress, overrun, memory errors or deadline expiry fail the engine. An expired
command is not reused for a later reply. Unsolicited replies are counted and
ignored in this stage, without jack-event policy.

QEMU requires response-status generation and RINTFL acknowledgement to continue
processing commands with RINTCNT=1. HDA INTCTL and PCI INTx delivery remain
masked; this is bounded command polling, not an interrupt implementation.
Operations poll at 1 ms with 100 ms phase/command deadlines. Codec power polling
shares an absolute deadline, while individual verbs have the transport bound.

## Codec and output

[Codec discovery](../../kernel/audio/codec.c) reads the full candidate audio
function group before publishing a route. It checks widget types, connections,
PCM capabilities, power and amplifier metadata. Direct NIDs bound the static
graph/iterative search to 128 entries. It rejects unsupported indirect addresses,
malformed/range-overflow connections, digital/mono/processing paths and unusable
amplifier indices. No runtime allocation or recursive search stack is needed.
Initial selection prefers an analog line-out, then headphones. Native fixed
speaker/jack selection still needs its own policy and qualification.

Discovery does not activate pins, EAPD or amplifiers. Explicit activation checks
D0, selected connections, advertised amplifier gain offsets, output pin controls,
EAPD where present, converter format and stream tag. The measured QEMU route is
codec 0, AFG 1, pin 3 → DAC 2. Static PCM is copied only into a stopped stream's
backing, with four 1,920-byte BDL periods: **7,680 bytes / 40 ms**, using an
8,192-byte page-rounded allocation. There is no software refill or starvation
policy yet; a static cyclic buffer intentionally repeats until stopped.

Stream stop verifies RUN clear and reset; command stop verifies both ring DMA
bits clear. Their bus-master handoff preserves whichever DMA engine is still
active, then clears BME once both are stopped. Codec disable detaches the stream
and verifies supported amplifier mute. QEMU's two measured codec IDs ignore
pin-control writes and retain OUT=0x40; that exception is explicit and narrowly
matched. Codec disable is not a substitute for physical-stream stop.

All backing/claims remain retained until reboot. Transport or stream failure
stops admission and attempts bounded stream/ring/link/BME shutdown. Uncertain
ownership quarantines backing; there is no automatic restart or DMA reuse.
The normal initialized engine keeps its link released but all DMA off while
parked. No userspace handle, DMA mapping, drain promise or audible-position API
is exported by this stage.

## Evidence and remaining work

[Task 1 evidence](../development/experiments/audio-task1/README.md) records ordinary
builds, the production idle-state inspection, matched presenter/host idle-cost
observations and an **unmerged** static-PCM consumer. That consumer exercised
command wrap, distinct left/right signals, stop/detach, silent reopen and terminal
shutdown with four CPUs/output-only and one CPU/duplex. It is not linked into
the production branch; no boot tone or self-test is added.

Native AMD `1022:15e3` is not bound yet. The supplied ALC257 dump identifies its
advertised topology, format and EAPD state, not a qualified Pyxis cold-init
sequence. Native checks remain for the owner's ThinkPad batch; confirm closure
policy with the owner at milestone closure. IRQ/refill, per-space grant/calls,
mixing, driver-fault recovery and application audio need their later tasks.
