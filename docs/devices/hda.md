# HD Audio playback engine

Caelum supports QEMU's `8086:2668` Intel HDA controller with `hda-output` or
`hda-duplex`, and native HDA controllers admitted by PCI class with explicit
per-controller quirks. Native output needs an exactly matched codec profile:
the Realtek ALC257 on the ThinkPad T14 Gen 1 AMD (qualified) and the Conexant
SN6140 on the IdeaPad Slim 5 16IPH11, vela (plays natively since **2026-10-15**;
see [vela](#vela-intel-panther-lake-h-and-conexant-sn6140)). All use a checked
48 kHz S16LE stereo analog route. The playback milestone
closed with owner-run native evidence on **2026-10-09**. One BSP
worker owns command transport, stream state, DMA and software mixing. Userspace
uses process-owned [PCM sessions](../interfaces/audio.md) through its space's
named audio grant. The trusted [power overlay](../kernel/power-overlay.md#button-sound)
also supplies one static, non-session click through the same master-controlled
mixer. Initialization itself is silent: command DMA stops,
PCI bus mastering is disabled and the worker parks without polling. Normal boot
prints only `audio: ready ...`; absence is quiet. Controller/codec details are
debug records, per-verb and per-stream lines are trace, and failure explanations
remain visible normally.

## Kernel UI source

After readiness, the power overlay can request the owner's
[button sound](../../assets/ui/power/README.md). A build-time host converter
produces 48 kHz S16 stereo PCM; the kernel does no decoding or resampling.
One static cursor and a coalesced BSP restart flag supply the worker, without
heap storage, a session slot or userspace authority. UI input never waits for
codec work. Absent/unready/failed audio drops the request quietly; playback
failure discards the current/pending clip rather than retrying it by itself.

The clip joins the ordinary accumulator before saturation and master gain, so
master mute/zero produce silence. It has no per-space gain. A new press fades
the current voice to zero over 5 ms before restarting the clip; repeated
requests during that fade coalesce without extending it. The compiled clip
also has 5 ms endpoint fades. This changes unmixed source frames, not already
committed DMA: the shared ring can retain up to 80 ms of preceding audio.
The same pending-output, zero-tail, activation
and headphone-settle paths apply. A cold click can therefore wait for ordinary
codec power-up, while rapid clicks can interrupt the existing settles; this
change does not shorten either 75 ms settle. Owner listening confirmed clean
rapid restarts on ThinkPad speakers; headphones remain untested. See the
[qualification record](../development/experiments/power-overlay-sound.md).
Power-off/reboot dispatch never waits for the clip and may truncate it.

## Ownership and initialization

[Preparation](../../kernel/audio/hda.c) reserves the unique admitted controller
in a complete PCI inventory. It masks delivery, halts firmware streams and command
rings and asserts link reset before completing the claim or sizing BARs. A checked
4 KiB BAR prefix contains the used registers; QEMU's complete BAR is 16 KiB.
Coherent, physically contiguous CORB, RIRB, BDL and PCM backing is allocated on
the BSP before AP startup. CPU mappings and physical DMA addresses are separate.
Unsupported or absent hardware remains optional.

### Admission and controller quirks

Admission accepts PCI class `04/03/00` (HD Audio). A GPU's display audio is
skipped: an HDA function whose function 0 is a display controller (`03`) from
the same vendor, such as Renoir HDMI `1002:1637` beside the GPU at `07:00.0`.
The ThinkPad's analog controller `1022:15e3` sits in that same device at
`07:00.6`, under the CPU vendor, so it remains. Two remaining candidates are
ambiguous and leave audio inactive, as before. Owner-accepted on
**2026-10-15**, replacing the two exact controller identities.

One table in `hda.c` holds every controller quirk:

| Profile | Match | Model | Coherence | DMA ceiling with 64OK | Other |
| --- | --- | --- | --- | --- | --- |
| QEMU ICH6 | `8086:2668` | QEMU | none | none | QEMU timing guards, generic codec search |
| Intel Panther Lake-H | `8086:e328`, class `04/01/00` | native | DEVC `0x78` | none | TCSEL clear, CGCTL around reset, Immediate Command |
| AMD | vendor `1022` | native | PCIe Device Control | 40 bits | — |
| unlisted | class `04/03/00` | native | PCIe Device Control | none | — |

Intel's DSP-capable controllers report audio subclass `0x01`, so only a listed
Intel entry admits that class; it is never inferred for unlisted devices. An
unlisted controller without a PCIe capability fails closed. The native model
selects the FIFO-based timing profile below and exact codec profiles. Linux
references are to `sound/hda` at
[torvalds/linux `af32da41b032`](https://github.com/torvalds/linux/tree/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda);
no Linux code is imported:

- Panther Lake-H is
  [`PCI_DEVICE_ID_INTEL_HDA_PTL_H`, `AZX_DRIVER_SKL | AZX_DCAPS_INTEL_LNL`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda/controllers/intel.c#L2588),
  and the Lunar Lake capability set is
  [`AZX_DCAPS_INTEL_SKYLAKE | AZX_DCAPS_PIO_COMMANDS`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda/controllers/intel.c#L298-L299);
  Skylake includes the PCH snoop type. The flag
  [selects PIO commands](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda/common/controller.c#L1177-L1178),
  introduced because Intel recommends the Immediate Command interface over
  CORB/RIRB on Lunar Lake and later (Linux commits `a1de26c0b74f`,
  `f20bee3898c5`).
- [`azx_init_pci()`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda/controllers/intel.c#L388-L446)
  clears TCSEL bits 0–2 and, for the PCH snoop type, DEVC bit 11 (No Snoop).
- [`hda_intel_init_chip()`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda/controllers/intel.c#L556-L583)
  clears CGCTL `0x48` bit 6 (MISCBDCGE) across controller reset for the
  `AZX_DRIVER_SKL` type and sets it afterwards, without reading it back. Intel's
  SOF driver does the same in
  [`hda_dsp_ctrl_init_chip()`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/soc/sof/intel/hda-ctrl.c#L186-L291)
  for current platforms, updating only bit 6
  ([`PCI_CGCTL`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/soc/sof/intel/hda.h#L27-L35)); Linux commit `6639484ddaf6` records that reset can otherwise miss
  the command logic.
- [AMD's 40-bit DMA limit](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda/controllers/intel.c#L1964-L1966).

Before reservation, debug records
actual boot COMMAND (Mem/BME) and validated PMCSR state, NoSoftRst and PME bits.
Captured Fedora runtime state is not a boot default. The existing PCI probe
requires BME off for a wake/decode change; D3+BME and reset-causing D3hot wake
remain refused with specific failure explanations. There is no new handoff
helper. An already usable D0/memory-decode device retains firmware BME until
the established halt/claim sequence.

After halt and claim, PCIe coherence requires one valid PCIe endpoint
capability containing Device Control. With BME off, it clears only No Snoop
Enable (bit 11), using a 16-bit write that preserves other controls and the
adjacent Device Status W1C word. An already clear bit needs no write; the full
control word and BME-off state are checked before DMA allocation. Missing,
malformed or ambiguous capabilities, invalid state and refused clear fail closed.
The owner accepted this replacement on **2026-10-09**. Relaxed
Ordering and unknown vendor controls are unchanged. Intel DEVC coherence is the
same check on vendor register `0x78`, with BME off; an already clear bit needs
no write. A refused No Snoop clear stays fatal, because the device could then
read stale descriptors or PCM from CPU caches.

The other Intel quirks are advisory, as in Linux, which never reads them back.
TCSEL is cleared first. CGCTL MISCBDCGE is cleared before link reset is
re-entered and set again after codec detection; only that bit is written, and
only when it must change, because CGCTL holds other controls such as DSP clock
gating. A value that does not read back logs a warning with the before, written
and observed words, and initialization continues. Link reset, codec detection
and command timeouts still decide whether the controller is usable. Owner
policy, **2026-10-15**: recover or continue through ordinary hiccups, refusing
only an unusable controller or a real hazard. The first vela run had failed
closed on an exact whole-word CGCTL readback.

DMA backing is allocated with
[`dma_buffer_allocate_below()`](../../include/kernel/mm/dma.h) inside the
controller's reach: 32 bits without GCAP 64OK, otherwise the profile ceiling.
Page alignment exceeds HDA's 128-byte requirement. A first-fit search bounded
by the ceiling replaces the former check after unconstrained allocation, which
could refuse audio on a large-memory machine while low memory was free.
Immediate-command controllers allocate no CORB/RIRB backing. The
single-message, 64-bit, non-maskable MSI layout uses the existing helper.
Boot-enabled MSI/MSI-X still prevents reservation. No licensed vendor fixup or
speculative GPIO/codec coefficient write is included. The ThinkPad controller
has a 32 KiB BAR and single-message MSI at capability `0xa0`; PM is at `0x50`. The supplied
[Fedora codec inventory](../development/experiments/audio-investigation/thinkpad-alc257-codec.txt)
records the graph used for the checked route.

The [worker](../../kernel/audio/audio.c) alone releases reset, initializes command
transport and reads the codec graph. Runtime helpers assert the owning worker
and IF enabled; PCI command writes and explicit observation/mix/RUN commits use
brief IF=0 sections. AP syscalls stage copied PCM and forward requests through
the common BSP executor. The worker alone allocates, frees and consumes session
queues. Readiness workers inspect locked snapshots. Process exit invalidates
ownership immediately and sends an allocation-free cleanup notice; no destroyed
process pointer survives in the worker. See [SMP ownership](../kernel/smp.md).

Codec presence is read after the required post-reset delay, before acknowledging
STATESTS. Presence can already be latched while reset is asserted; clearing it
first loses QEMU's detection event. CORB/RIRB use their largest supported size
and one solicited command at a time. Barriers precede publication and response
consumption. Replies must match the pending codec; unexpected progress, overrun,
memory errors or deadline expiry fail the engine. An expired command is not reused
for a later reply. Unsolicited replies are counted and ignored, without jack policy.

Immediate-command controllers (Panther Lake-H) never start CORB/RIRB DMA. Each
verb waits for ICB clear, clears IRV, writes IC and sets ICB, then polls for IRV
with ICB clear and reads IR (HDA 1.0a §4.4.2). The response carries no codec
address; one command in flight keeps it unambiguous. Unsolicited responses have
no path there, which matches today's policy of ignoring them; live jack
switching would need RIRB alongside, as Linux keeps it. The same 1 ms polling
and 100 ms deadline apply. Command start needs neither BME nor ring backing.

Command operations retain bounded 1 ms polling and 100 ms phase/command deadlines.
Each codec readiness wait (a widget's power transition, the native AFG
subsystem read) has its own budget from when that wait starts, so time spent
earlier in setup on a busy BSP cannot expire it; individual verbs have the
transport bound.

Initialization (link release, command rings, codec discovery) is attempted up
to **3 times**. A failed attempt records its first reason instead of failing
the engine; the worker logs `hda: setup attempt N failed: <reason>; resetting
link`, then masks delivery, stops both command rings, asserts link reset and
disables bus mastering, checking each step, and starts again. Audio fails
closed only if that reset cannot be verified or the last attempt fails, with
the usual checked shutdown. This follows Linux's practice of resetting the link
and probing again after a failed codec probe; the attempt count is tuning. QEMU requires response-status generation and RINTFL
acknowledgement with RINTCNT=1. Playback completion and errors use checked
single-message MSI routed to the BSP. PCI INTx remains disabled. The IRQ captures
bounded status, acknowledges only observed bits and wakes the worker; it never
mixes, allocates or changes DMA/codec ownership.

## Codec and stream activation

[Discovery](../../kernel/audio/codec.c) reads the candidate audio function group
before publishing a route. It checks widget types, connections, PCM capabilities,
power and amplifier metadata. Direct NIDs bound the static graph and iterative
search to 128 entries. Unsupported indirect addresses, malformed/range-overflow
connections, digital/mono/processing paths and unusable amplifier indices are
rejected. No recursive search or runtime graph allocation is needed. Selection
prefers analog line-out, then headphones for QEMU.

Native controllers accept only an exact codec profile in
[`codec.c`](../../kernel/audio/codec.c): codec vendor, AFG subsystem, speaker and
headphone pin default configurations, and each output's DAC and connection
index. Codecs on the link without a profile, such as Intel display audio, are
read for their vendor ID and otherwise untouched. A matching vendor with another
subsystem is logged and not used; no profile means native audio fails with
`no qualified native codec profile`. There is no generic hardware graph parser.

| Profile | Vendor / subsystem | Speaker pin ← DAC (index) | Headphone pin ← DAC (index) | Mute point |
| --- | --- | --- | --- | --- |
| Realtek ALC257 (ThinkPad) | `10ec0257` / `17aa5081` | `0x14` (`90170110`) ← `0x02` (0) | `0x21` (`04211020`) ← `0x02` (0) | pin amplifiers |
| Conexant SN6140 (vela) | `14f11f87` / `17aa3890` | `0x17` (`91170110`) ← `0x11` (1) | `0x16` (`03211040`) ← `0x10` (0) | DAC amplifiers |

Discovery checks those widgets, the speaker's output and EAPD capabilities and
the headphone's output, headphone, EAPD and presence capabilities, then parks
both outputs muted and disabled, EAPD off and converters detached. At physical
playback start, one checked headphone Pin Sense selects the output, then D0,
connection (when the pin has several), format/tag, pin controls and EAPD are
programmed/read back; the selected output is unmuted last at its advertised
0 dB gain. With pin mutes the DAC gain opens before the pin; with DAC mutes the
DAC amplifier is that last step. Stop parks both outputs. No jack verbs or
unsolicited-response handling run during playback. A session joining an active
mix does not resample. Inserting headphones mid-playback keeps the speaker until
the next start; unplugging retains headphones until then. See the
[routing debt](../technical-debt.md#hd-audio-jack-routing-at-playback-start).

Discovery does not activate pins, EAPD or amplifiers. Activation checks D0,
selected connections, advertised amplifier gain offsets, output pin controls,
EAPD where present, converter format and stream tag. The measured QEMU route is
codec 0, AFG 1, pin 3 → DAC 2. The stream has eight immutable 1,920-byte BDL
periods: **15,360 bytes / 80 ms**, backed by a 16,384-byte page-rounded allocation.
Each period holds 480 stereo frames / 10 ms. BDL entries are configured while
stopped and never changed during RUN. Session queues separately hold 80 ms each;
with eight active sessions their PCM storage totals 122,880 bytes.

The first queued frames trigger activation without a priming threshold. Codec
verbs and stream preparation finish before the worker drains exit notices and
prefills DMA. Mix consumption, direct output copy, barriers and RUN publication
then share IF=0, preventing exit between consuming a source and handing it to
hardware. The mixer applies per-space software gain, adds all sources in signed 32-bit
accumulators, clips once to S16, then applies master gain. The
[volume contract](../interfaces/audio.md#user-volume-controls) preserves unity
behavior and keeps codec command rings stopped during playback. Starving sources supply zeros independently of other spaces.

RELEASE and exit discard queued PCM without resetting another source's stream.
Frames already mixed into DMA can remain audible. After the last queued PCM,
the worker lets one ring of zeros follow the last observed mixed data before
stopping. This accounts for QEMU's backend tail internally; it exports no audible
drain promise. Acquired sessions survive a normal stop and can restart on later
writes. Stop verifies RUN clear/reset; QEMU and the native speaker then mute,
detach/disable the route and stop command rings with BME clear.

Accepted **2026-10-09** for the ALC257 and reused for every native profile: the
headphone route instead verifies amplifier mute, settles for at least **75 ms**, disables both pins/EAPD and detaches the
converter, then settles for at least **75 ms**. Command rings and BME are off
during both waits. Fully parked idle remains muted/disabled with EAPD off, in D0,
without idle IRQs or periodic watchdog work. This independently implemented
sequence follows the mute/pin-disable settling steps in Linux's GPL-2.0
[`alc256_shutup()`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda/codecs/realtek/alc269.c#L505),
used for ALC257 suspend; no Linux code or vendor coefficients are imported.
Pyxis retains its existing EAPD-off idle policy rather than importing the full
Linux suspend implementation.

The BSP worker owns absolute settle deadlines and continues servicing requests,
volume changes and exit cleanup. Codec work requires IF enabled; settle waits
use the ordinary task-wait handoff, never IRQ, exit or IF=0 mix/commit paths.
Queued PCM cancels either settle before its next phase. A restart samples jack
presence afresh: during the first settle it preserves the selected pin's bias
if the route still matches; during the second it performs ordinary activation.
Neither restart waits out the remaining settle. The software gain, 5 ms ramp,
mute and zero-tail contracts are unchanged. The owner reported no end-of-tone
headphone pops across many runs after #628 in the
[native stop recheck](../development/experiments/audio-headphone-pop/README.md);
rapid restart during settling remains separately unqualified.

QEMU's two measured codec IDs ignore pin-control writes and retain OUT=0x40.
That exception is explicit and narrowly matched. Codec detach and supported mute
do not replace physical-stream stop.

## Vela: Intel Panther Lake-H and Conexant SN6140

Vela's controller is `00:1f.3`, `8086:e328`, class `04/01/00`, BAR0 512 KiB at
about 1 TiB, single-message 64-bit MSI at `0x60` and PM at `0x50` with
NoSoftRst; it has no PCIe capability. Linux binds `snd_hda_intel` (not SOF), so
no DSP firmware is involved. Codecs are the SN6140 at address 0 and the Panther
Lake HDMI codec at address 2. The SN6140 AFG reports five GPIOs, all disabled
under Linux; Linux's Lenovo fixup for this board only drives mute LEDs, and its
one vendor write (node `0x1c`) is headset-microphone bias. No GPIO or vendor
verb is used for output. Source: the owner's Fedora inventory
(`lspci -vvv`, `/proc/asound/card0/codec#*`) and
[`conexant.c`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/hda/codecs/conexant.c#L161-L193).

The internal speakers sound poor under Linux too, so the native speaker check
means clean, correctly pitched, gapless tones at a sensible volume; the
headphone jack is the quality check.

Owner native evidence, **2026-10-15**, #881 at `18511fa1` (debug build, rescue
entry): admission, Intel quirks, Immediate Command transport and the SN6140
profile came up (`audio: ready codec=14f11f87 … speaker=23/DAC=17
headphone=22/DAC=16`), and two playbacks ran with no refusal. RUN read back
**5,222 ns** and **5,313 ns** after the write. The earlier `0873ad23` run
refused every start because the single immediate SDnCTL readback raced that
latch (IOCE/FEIE/DEIE set, RUN clear); Linux's legacy driver never reads RUN
back, and Intel's SOF driver polls for it. The same build also changed the
stream-tag write from a byte to a dword read-modify-write, so this run cannot
separate the two changes; the RUN latch is the explanation consistent with
both logs, not an isolated proof. CGCTL MISCBDCGE still never sticks
(`before=0 written=40 after=0`); TCSEL and DEVC read 0 before and after. GCAP
`9b01`: 11 input, 9 output streams, 64-bit addressing; SDnFIFOS `c0`.

The RUN poll applies to every controller. ThinkPad regression of the same build,
hands-free, **2026-10-15**: RUN read back **3,577 ns** after the write on the
AMD `1022:15e3`. The earlier single immediate readback always passed on that
controller, so this figure is mostly the cost of the uncached SDnCTL read in
the poll, not a late latch; the poll does not record how many reads it took.
Each figure is therefore an upper bound on when RUN latched. `pcm 440 220 30`
ran 29.94 s with `discontinuity=0` and no refused, restarted or unavailable
line.

Owner listening check, same day: Chocolate Quake plays with sound through the
internal speakers, "as expected, not great", consistent with how the speakers
sound under Linux. Real SDL audio therefore works natively through this path.
The headphone V-shape comparison stays open: tones and Quake's heavily
compressed effects cannot show it, and it needs real music, for which no
player exists yet.

Open question (owner, **2026-10-15**; no change made): under Linux, vela's
headphone output is strongly V-shaped, with clear mids but far too much bass and
treble, probably codec-internal Conexant processing left at firmware defaults.
Later decide whether Pyxis keeps that default or asks for a flat output. The
codec graph exposes no standard processing: no output-path widget sets the
processing capability, and the only other widgets are vendor-defined (`0x15`,
`0x1b`–`0x1f`). Any EQ control would therefore use undocumented vendor verbs.

## Progress and refill limits

An IRQ completion is a coalesced hint, not a period count. The worker brackets
status capture with LPIB reads and retries a moving sample at most three times.
Within the same IF=0 phase it consumes the completion hint with that position;
a QEMU completion without the expected boundary advance is refused. Native BCIS
can occur when descriptor data enters the FIFO, before link consumption, and is
only a wake hint. The RUN epoch
begins with monotonic time and WALCLK captured immediately before RUN, allowing
legitimate progress before the first observation. The selected kernel clock
and wrapping WALCLK bound
observation and progress gaps. While running, a **5 ms watchdog** supplements
IRQ wakeups; it is absent while parked. An observation gap reaching the
**service horizon**, lack of observed progress for that interval or a stale
completion notification is a [timing fault](#timing-recovery): the worker
restarts the stream rather than guessing missing laps. An alignment error,
FIFO/descriptor fault or invalid position fails the engine. If all three position
scans move, fresh RUN/status and error checks plus both last positions' range
and frame alignment must prove that movement alone prevented observation. That
case discards the epoch through bounded timing recovery (chronium, 2026-10-25);
no position from the failed scans is accepted as progress. The
notification age begins when the CPU first observes status, not at hardware
completion. Observation time is sampled after that status scan, which may create
a new notification timestamp; the earlier pre-commit timestamp only anchors
the complete commit interval. A bounded retry can conservatively refuse healthy moving progress.

The horizon is **20 ms** for QEMU. Natively it follows the ring: after a refill
the ring holds at least seven of its eight periods ahead of the position, and
the horizon is that span minus the native commit reserve below, so a late
worker is detected while the next reclaimed period can still keep the reserve.
With the reserve's 20 ms of commit progress it is about 50 ms less the FIFO
(trace logs print `horizon-ns` at each native start).

Before a reclaimed period is mixed directly into DMA, the worker takes a fresh
position/time observation with IF=0. QEMU requires more than **8,192 bytes plus
one frame** of headroom before that period's next consumption. A DMA barrier
follows copying. A second observation rejects a commit taking **1 ms or more**
in monotonic time or WALCLK, or progress reaching the target. Commit duration is retained
for measurement. These bounds are runtime guards and accepted starting tuning;
they are not hard real-time guarantees or an absolute hardware consumption
counter.

`QEMU_CODEC_BURST_BYTES` (8192) and the 1 ms commit/WALCLK bounds are
QEMU-derived and remain unchanged for QEMU. The original measurements used
HPET; newer kernels can select TSC through [timekeeping](../kernel/timekeeping.md).

The owner accepted a separate native profile on **2026-10-09**. After stopped
format programming, `F = round_up(FIFOS + 1, 4)` overestimates both literal-byte
and size-minus-one FIFO encodings; zero, all-ones and unusable sizes are refused.
HDA 1.0a §3.3.40 bounds fetch/untransmitted bytes and LPIB advance by FIFOS.
Before RUN, check that **F + 4 + 3840 bytes** plus one period fits the ring. Before overwriting
a reclaimed period, require more than that reserve: one frame plus FIFO advance
and 20 ms of playback at 192,000 bytes/s. Pre/post position scans and DMA
publication share the checked commit interval, bounded below **20,000,000 ns**
and **480,000 WALCLK ticks**; the final monotonic read follows post-MMIO/device
clock observation. Final distance must remain greater than **F + 4**. Native
LPIB equal to CBL normalizes to zero. Actual FIFOS, progress/skew and commit
timing distributions are not established by the closure sitting; these remain
accepted conservative guards, not hard real-time guarantees.

QEMU's codec can request an 8,192-byte burst, coalesce completions and adjust its
timer origin. The 8,192-byte per-callback bound applies to QEMU's default timer-driven codec;
its callback-driven compatibility mode is not qualified. Multiple catch-up
callbacks can obscure whole laps even when new
observation gaps are short. The controller's modulo LPIB and codec WALCLK do not
prove absolute progress. A post-copy check also cannot undo data already consumed
during a host stall or racing DMA. The larger ring and conservative headroom
reduce the exposure for the qualified workload; they do not establish strict
progress proof or guarantee that stale cyclic PCM never replays before detection.
See the [source assessment and qualification](../development/experiments/audio-task2/README.md).

## Timing recovery

Accepted by the owner on **2026-10-13**, replacing the earlier rule that any
guard trip disabled audio until reboot. A late worker is not a device fault.
Timing faults are a reached service horizon, stalled progress, an expired
completion notification, an exhausted refill margin, a commit over its clock
bound, progress reaching a period during its commit, and validated moving-only
position-sampling exhaustion (chronium, 2026-10-25). This includes a freshly
started RUN epoch: the worker handles its recovery before another refill or
watchdog wait. On one, the worker:

1. clears RUN and resets the stream through the normal bounded, checked stop;
2. counts one discontinuity in each usable session, keeping its queued PCM;
3. logs `audio: <fault>; output restarted (N since boot)` at info level, at
   most once per 10 s (owner-accepted 2026-10-13);
4. prepares the stream again, primes all eight periods from the sessions
   (silence where empty) and starts a new RUN epoch.

Priming overwrites the whole ring, so PCM from before the stop never plays
again. A miss longer than the PCM queued ahead (about 70–80 ms after a refill)
can still replay the excess once before the stop; the horizon is chosen to
detect earlier. With no PCM pending, recovery takes the normal stop instead.

A restarted stream is proven once it observes one full ring lap of healthy
progress. A recovery needed before that is a failed recovery. Failed recoveries
that keep recurring for **2 s** mean the cause is not transient: the next one
fails the engine as `output kept missing service after repeated recovery`.
Isolated restarts never fail it, however often they occur. These bounds are
tuning, not ABI. The owner accepted this repeat
rule on **2026-10-13**, replacing the proposed "more than 3 recoveries within
10 s", which in nested QEMU disabled audio after one EDuke32 startup burst (four
restarts in about 130 ms); see the
[record](../development/experiments/audio-recovery/README.md#nested-qemu-validation-of-recovery). The
[recovery record](../development/experiments/audio-recovery/README.md) has the
native evidence and nested validation.

## Failure and retained ownership

Transport failure after initialization, stream errors, a refused stream reset
or stop, unaligned or invalid/uncertain positions and repeated recovery stop admission
until reboot. Failed initialization retries first, as above. Existing
sessions report FAILED, writable waits report WAIT_ERROR and writes/acquisition
of an unowned session report UNAVAILABLE; owner STATUS, RELEASE and exit cleanup
remain serviceable. Ordinary producer starvation is separate and does not
quarantine the engine. The live worker continues servicing requests and cleanup
even if the controller is absent or initialization fails.

A playback start refused without a controller fault fails that playback only
(owner policy, **2026-10-15**). This covers a RUN, interrupt-enable or first
position check that does not verify, or a native codec activation step that
does not read back. RUN may read back late on Intel controllers, whose SOF
driver polls for it
([`hda_dsp_stream_trigger()`](https://github.com/torvalds/linux/blob/af32da41b0327b9c6a37856ba82b6760d6c8d10e/sound/soc/sof/intel/hda-stream.c#L397-L435),
300 µs plus one 500 µs poll interval), so Pyxis polls up to 1 ms before
refusing. The worker logs the refused step with INTCTL, SDnCTL/SDnSTS as one
dword, LPIB and stream error bits (`audio: output start refused: …`), then PCI
COMMAND, GCTL, DPLBASE and, from the extended capability list within the mapped
first 4 KiB, PPCTL and link 0's LCAP/LCTL (`hda: start state …`). It then fails
the current sessions, and stops and resets the stream. FIFO/descriptor errors
are reported rather than fatal there. It then parks the output through the
normal stop sequence. Producers see FAILED/UNAVAILABLE; queued PCM is
discarded, so the worker does not retry by itself, and a new acquisition tries
again. A command transport failure, a stop or reset that does not verify, and
a refused coherence setup still fail the engine.

Shutdown masks MSI/HDA delivery, clears RUN and resets the stream, stops command
rings, asserts link reset and disables PCI bus mastering. Each step is bounded
and checked. All claims and backing remain retained until reboot; uncertain
ownership quarantines DMA without reuse. There is no controller reset after
failure or hot removal. Session release never exposes or frees engine DMA.

## Qualification and remaining scope

The owner's [native results on #578](https://git.internal/PyxisOS/pyxis-os/pulls/578)
close the playback milestone on **2026-10-09**. The image was main `51cbec9`
plus Bluetooth #564 `a1d97dc5` and audio #578 `65af50b0`, with default logging,
a nine-space validation configuration and UDP logging on horse.

- Cold and warm boots both reported `audio: ready codec=10ec0257`; the checked
  PCIe NoSnoop coherence setup passed and the speaker/headphone route was found.
- One-session `pcm 1000 500 10`, `pcm 1000 0 10`, `pcm 0 500 10` and the
  pause/reacquire run completed. The owner heard the correct tones on the
  internal speaker and wired headphones.
- Eight simultaneous `pcm 0 0 660` sessions in Development and Audio 1–7
  completed the full **11 minutes**, with no errors or logged audio/HDA failure
  or guard trip. The ninth acquisition in Remote returned `CALL_LIMIT`.

The eight-session run used silence through the same queue, mixer, refill and
DMA path. Audible eight-source content was skipped because full-scale speaker
tones were painfully loud; content and routing were checked by ear at one
session. This meets the owner's native eight-session closure requirement. It
establishes neither end-to-end latency nor native CPU/commit-time distributions.
That evidence predates the [software volume controls](../interfaces/audio.md#user-volume-controls).
Their [native listening/mute/reboot check](../userland/audio-volume.md#native-qualification)
passed later that day. The post-volume eight-session silent regression passed
on **2026-10-15** on the ThinkPad with #881 at `18511fa1`, hands-free. A
validation-only live archive added spaces Audio 1–8, each running
`pcm 0 0 120` from its init. All eight finished with `discontinuity=0` and
`starvation=2`, the ninth acquisition (Remote, 18 s after boot) returned
`CALL_LIMIT`, and the log had no restarted, unavailable, failed or refused audio
line. Other volume items stay
[open](../technical-debt.md#hd-audio-volume-native-regression).

For later native batches, **two minutes of eight simultaneous silent sessions**
(`pcm 0 0 120` in eight spaces) is sufficient as a regression check. Let every
producer finish naturally, retain each final STATUS and the complete log, check
for errors/guard trips, and check ninth-session refusal. Ten minutes was the
one-time closure duration requirement; the closure run lasted eleven. Longer
runs are warranted only by counter findings. Do not halt active audio for GDB.

QEMU evidence remains separate. [Task 1](../development/experiments/audio-task1/README.md)
records private engine/idle qualification; [task 2](../development/experiments/audio-task2/README.md)
records exact PCM and saturated mixing. The initial four-CPU waveform matched
all 240,000 requested PCM frames but included **58.667 ms of startup silence**
after the first 21.33 ms of PCM. No zero-gap startup or latency promise follows.
The [profiling report](../development/experiments/audio-task2/profiling.md) records
BSP costs before/after notification gating and one clock snapshot per readiness
scan. Sustained eight-session playback still fails closed in nested QEMU; the
longer run failed its 1 ms WALCLK commit guard after **112.227 s** of output.
A transient nested-host scheduling/VM-exit delay during a roughly 5–6 µs mix is
the most likely cause, not a proven trace. This is a QEMU limitation, not the
native result. [Task 5's QEMU record](../development/experiments/audio-task5/README.md)
contains matched presenter measurements and regression evidence.

[Technical debt](../technical-debt.md#hd-audio-sustained-eight-session-playback)
retains the QEMU limit, deferred batching, reboot-only failure recovery, startup
and jack-switching limits. Eight periods remain starting tuning. SDL2 and Quake
adapters are separate consumer work: conversion/resampling stays in userspace;
SDL2 needs real callback/thread execution under the
[threads direction](../wip/scheduling-and-threads.md). Recording, HDMI/DP,
USB/dock and Bluetooth audio, ACP, suspend/resume and general device selection
remain outside this analog playback implementation.

## Later directions

Owner ideas, **2026-10-09**. [Volume control](../userland/audio-volume.md) is
implemented and natively checked, completing the prerequisite for later players.
Players remain unassigned: MIDI with
TinySoundFont + TinyMidiLoader (MIT, single-header C; a SoundFont needs its own
asset licence/cache entry), SPC with blargg's snes_spc (LGPL 2.1, C++, userspace
32→48 kHz resampling), and the keyboard piano test app. A broader shared
black-and-white bitmap pool, including battery reuse, is separate UI work.
