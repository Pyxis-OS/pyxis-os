# HD Audio playback engine

Caelum supports QEMU's `8086:2668` Intel HDA controller with `hda-output` or
`hda-duplex`, discovering a checked 48 kHz S16LE stereo analog route. One BSP
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
prefers analog line-out, then headphones. Native fixed-speaker/jack policy is
still a separate task.

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
hardware. The mixer adds all sources in signed 32-bit accumulators and clips once
to S16. Starving sources supply zeros independently of other spaces.

RELEASE and exit discard queued PCM without resetting another source's stream.
Frames already mixed into DMA can remain audible. After the last queued PCM,
the worker lets one ring of zeros follow the last observed mixed data before
stopping. This accounts for QEMU's backend tail internally; it exports no audible
drain promise. Acquired sessions survive a normal stop and can restart on later
writes. Stop verifies RUN clear/reset and codec detach/mute, then command-ring
stop and BME clear. The worker parks without idle IRQs or periodic watchdog work.

QEMU's two measured codec IDs ignore pin-control writes and retain OUT=0x40.
That exception is explicit and narrowly matched. Codec detach and supported mute
do not replace physical-stream stop.

## Progress and refill limits

An IRQ completion is a coalesced hint, not a period count. The worker brackets
status capture with LPIB reads and retries a moving sample at most three times.
Within the same IF=0 phase it consumes the completion hint with that position;
a completion without the expected boundary advance is refused. The RUN epoch
begins with HPET and WALCLK captured immediately before RUN, allowing legitimate
progress before the first observation. Monotonic HPET and wrapping WALCLK bound
observation and progress gaps. While running, a **5 ms watchdog** supplements
IRQ wakeups; it is absent while parked. A gap reaching **20 ms**, lack of observed
progress for that interval, stale completion notification, alignment error or
FIFO/descriptor fault fails the engine rather than guessing missing laps. The
notification age begins when the CPU first observes status, not at hardware
completion. A bounded retry can conservatively refuse healthy moving progress.

Before a reclaimed period is mixed directly into DMA, the worker takes a fresh
position/time observation with IF=0 and requires more than **8,192 bytes plus
one frame** of headroom before that period's next consumption. A DMA barrier
follows copying. A second observation rejects a commit taking **1 ms or more**
in HPET or WALCLK, or progress reaching the target. Commit duration is retained
for measurement. These bounds are runtime guards and accepted starting tuning;
they are not hard real-time guarantees or an absolute hardware consumption
counter.

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

## Evidence and native scope

[Task 1](../development/experiments/audio-task1/README.md) records production idle
ownership and the unmerged static-PCM consumer used for initial engine checks.
[Task 2](../development/experiments/audio-task2/README.md) records the public
sessions, refill, mixing and matched measurements. The initial four-CPU QEMU
waveform matched all 240,000 requested PCM frames exactly. Its unprimed start
also produced **58.667 ms of silence after the first 21.33 ms of PCM**; no zero-gap
startup or audible-latency guarantee follows from the waveform match.

Eight admitted producers also produced an exact saturated mix segment, but the
nested-QEMU run subsequently exceeded the 20 ms observation horizon and failed
closed. Its measured BSP-thread cost was about one full host CPU. The
[profiling follow-up](../development/experiments/audio-task2/profiling.md)
separates guest time from host exit/emulation work. Accepted notification gating
and a shared readiness-scan clock snapshot reduce deadline/HPET amplification.
Both repeated eight-source profiles passed without failure, but whole-VM CPU
increased with retry/wait traffic while BSP savings were modest. Sustained
eight-session qualification and owner review remain delivery gates: the longer
current-main run failed the codec commit-clock guard after 112.227 s of output.

Native AMD `1022:15e3` remains unbound. The supplied ALC257 dump establishes
advertised topology, format and EAPD state, not native Pyxis cold-init, speaker or
headphone playback. Eight periods are starting tuning to revisit during native
latency/refill qualification. The owner's later ThinkPad batch remains the
qualification direction; confirm QEMU closure with retained native checks at
milestone closure. SDL2, Quake, recording, HDMI/DP, USB/dock audio and suspend
remain outside this implementation.
