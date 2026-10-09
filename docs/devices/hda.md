# HD Audio playback engine

Caelum supports QEMU's `8086:2668` Intel HDA controller with `hda-output` or
`hda-duplex`. A narrowly matched AMD `1022:15e3`, Lenovo `17aa:5081` controller
and Realtek ALC257 output pair are qualified on the ThinkPad T14 Gen 1 AMD.
Both use a checked 48 kHz S16LE stereo analog route. The playback milestone
closed with owner-run native evidence on **2026-10-09**. One BSP
worker owns command transport, stream state, DMA and software mixing. Userspace
uses process-owned [PCM sessions](../interfaces/audio.md) through its space's
named audio grant. Ordinary initialization plays no audio: command DMA stops,
PCI bus mastering is disabled and the worker parks without polling. Normal boot
prints only `audio: ready ...`; absence is quiet. Controller/codec details use
`LOG_LEVEL=trace`, while failure explanations remain visible normally.

## Ownership and initialization

[Preparation](../../kernel/audio/hda.c) reserves the unique supported controller
in a complete PCI inventory. It masks delivery, halts firmware streams and command
rings and asserts link reset before completing the claim or sizing BARs. A checked
4 KiB BAR prefix contains the used registers; QEMU's complete BAR is 16 KiB.
Coherent, physically contiguous CORB, RIRB, BDL and PCM backing is allocated on
the BSP before AP startup. CPU mappings and physical DMA addresses are separate.
Unsupported or absent hardware remains optional.

Native selection verifies class and subsystem in the complete inventory, rather
than binding the GPU HDMI or ACP function. Before reservation, ktrace records
actual boot COMMAND (Mem/BME) and validated PMCSR state, NoSoftRst and PME bits.
Captured Fedora runtime state is not a boot default. The existing PCI probe
requires BME off for a wake/decode change; D3+BME and reset-causing D3hot wake
remain refused with specific failure explanations. There is no new handoff
helper. An already usable D0/memory-decode device retains firmware BME until
the established halt/claim sequence.

After halt and claim, native initialization requires one valid PCIe endpoint
capability containing Device Control. With BME off, it clears only No Snoop
Enable (bit 11), using a 16-bit write that preserves other controls and the
adjacent Device Status W1C word. An already clear bit needs no write; the full
control word and BME-off state are checked before DMA allocation. Missing,
malformed or ambiguous capabilities, invalid state and refused clear fail closed.
The owner accepted this replacement on **2026-10-09**. PCI byte `0x42` is
read-only ktrace evidence; no legacy write or admission gate remains. Relaxed
Ordering and unknown vendor controls are unchanged. Complete DMA
allocations respect 128-byte alignment and a conservative 40-bit ceiling when
GCAP permits 64-bit addresses, otherwise 32-bit. The supplied single-message,
64-bit, non-maskable MSI layout uses the existing helper. Boot-enabled MSI/MSI-X
still prevents reservation. No licensed vendor fixup or speculative GPIO/codec
coefficient write is included. The native controller has a 32 KiB BAR and
single-message MSI at capability `0xa0`; PM is at `0x50`. The supplied
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

Command operations retain bounded 1 ms polling and 100 ms phase/command deadlines.
Codec power polling shares an absolute deadline; individual verbs have the
transport bound. QEMU requires response-status generation and RINTFL
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

The native path validates codec `10ec0257`, AFG subsystem `17aa5081`, speaker
pin `0x14` (`90170110`) and headphone pin `0x21` (`04211020`). Both share DAC
`0x02`; headphone connection index 0 is checked explicitly. Discovery parks both
pins muted/disabled, EAPD off and converter detached. At physical playback start,
one checked headphone Pin Sense selects the output, then D0, advertised 0 dB
DAC gain, connection, format/tag, pin controls and EAPD are programmed/read back;
the selected pin is unmuted last. Stop parks both pins. No jack verbs or
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

Accepted **2026-10-09**: the ALC257 headphone route instead verifies amplifier
mute, settles for at least **75 ms**, disables both pins/EAPD and detaches the
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
mute and zero-tail contracts are unchanged. Acoustic verification of this stop
sequence is pending the [native recheck](../development/experiments/audio-headphone-pop/README.md).

QEMU's two measured codec IDs ignore pin-control writes and retain OUT=0x40.
That exception is explicit and narrowly matched. Codec detach and supported mute
do not replace physical-stream stop.

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
IRQ wakeups; it is absent while parked. A gap reaching **20 ms**, lack of observed
progress for that interval, stale completion notification, alignment error or
FIFO/descriptor fault fails the engine rather than guessing missing laps. The
notification age begins when the CPU first observes status, not at hardware
completion. A bounded retry can conservatively refuse healthy moving progress.

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
Before RUN, check that **F + 4 + 3840 bytes** fits the ring. Before overwriting
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

## Failure and retained ownership

Unsafe/ambiguous progress, transport failure and stream errors stop admission
until reboot. Existing sessions report FAILED, writable waits report WAIT_ERROR
and writes/acquisition of an unowned session report UNAVAILABLE; owner STATUS,
RELEASE and exit cleanup remain serviceable. Ordinary producer starvation is
separate and does not quarantine the engine. The live worker continues servicing
requests and cleanup even if the controller is absent or initialization fails.

Shutdown masks MSI/HDA delivery, clears RUN and resets the stream, stops command
rings, asserts link reset and disables PCI bus mastering. Each step is bounded
and checked. All claims and backing remain retained until reboot; uncertain
ownership quarantines DMA without reuse. There is no automatic restart or hot
removal. Session release never exposes or frees engine DMA.

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
That evidence predates the [software volume controls](../interfaces/audio.md#user-volume-controls);
their native listening check remains [pending](../wip/audio-volume.md).

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

Owner ideas, **2026-10-09**. [Volume control](../wip/audio-volume.md) now has
software gain and the owner's four-mask bar UI; native listening remains its gate
before ordinary use and the later players. Players remain unassigned: MIDI with
TinySoundFont + TinyMidiLoader (MIT, single-header C; a SoundFont needs its own
asset licence/cache entry), SPC with blargg's snes_spc (LGPL 2.1, C++, userspace
32→48 kHz resampling), and the keyboard piano test app. A broader shared
black-and-white bitmap pool, including battery reuse, is separate UI work.
