# PCM audio sessions

The named `audio` grant provides playback within its own space through
`PROTOCOL_AUDIO` and `AUDIO_RIGHT_PLAYBACK`. It grants no controller, DMA,
input-focus or other-space authority. The kernel adds it to a new space's first
process alongside its display and input devices; ordinary launches receive it
only through explicit delegation. Boot init does not receive a space-device
audio grant. Session, shell and remote-terminal handoffs preserve an optional
audio grant independently of stdin and keyboard/pointer focus.

Each space admits one exclusive, process-owned session, with at most eight
sessions system-wide. Playback continues in hidden spaces. Repeated ACQUIRE,
including by the owner, is BUSY. Copying a handle does not transfer session
ownership, and closing the last caller handle does not release the session.
WRITE, STATUS and RELEASE require the acquiring process. RELEASE or process exit
invalidates its generation, cancels uncommitted work and discards queued PCM;
another process can then acquire the space's session. Already mixed hardware
frames may outlive either action. No audible drain or play cursor is exported.

The fixed format is **48 kHz S16LE stereo**, four bytes per frame. Conversion and
resampling belong to userspace. A session owns a copied queue of **3,840 frames /
15,360 bytes / 80 ms**; it never owns a DMA mapping. The
[HDA worker](../devices/hda.md) mixes sessions in signed 32-bit accumulators and
clips to signed 16-bit only after adding every source. Empty sources supply zeros;
zero-valued PCM still consumes queue capacity and counts as submitted audio.
Volume, pause, device selection, recording and application audio backends are
outside this interface.

## Calls and replies

[audio.h](../../include/abi/audio.h) defines the protocol constants and wire
layouts. Requests use the ordinary 16-byte message header; the table describes
the fields following it. Replies have no message header. Native audio grants
have zero transport rights.

The libpyxis helpers `audio_acquire()`, `audio_write()`, `audio_status()` and
`audio_release()` borrow the grant handle. The write helper validates the returned
accepted byte count; acquire/status helpers clear their outputs on failure.

| Operation | Request payload | Successful reply |
| --- | --- | --- |
| `AUDIO_ACQUIRE` | None | 40-byte `audio_acquire_reply`: `generation`, `rate`, `channels`, `format`, `capacity_frames`. |
| `AUDIO_WRITE` | `uint64_t buffer`, `uint64_t length` | Eight-byte accepted byte count, equal to `length`. |
| `AUDIO_STATUS` | None | 48-byte `audio_status_reply`: `generation`, `capacity_frames`, `free_frames`, `starvations`, `discontinuities`, `state`. |
| `AUDIO_RELEASE` | None | No payload. |

WRITE is nonblocking and atomic: it copies and commits the whole request or
accepts nothing. The maximum is **4,096 bytes / 1,024 frames**. Length must be
divisible by four; the source address need not have frame alignment. The caller
may reuse or free its buffer after return. Insufficient queue capacity returns
`CALL_WOULD_BLOCK`, preserving reply storage; STATUS can guide a smaller write.
Zero length is a no-op after the same request, buffer, owner and backend checks,
and still requires room for the eight-byte reply. It returns UNAVAILABLE for a
failed session.

The generation identifies the acquired session, rather than a handle or DMA
position. It advances on acquisition and invalidation and is never reused.
Free frames describe copied queue capacity: mixing makes them available before
hardware consumes those frames. The two counters saturate at `UINT64_MAX`:

- `starvations` counts an empty episode when a requested mixer period has
  insufficient queued PCM. Repeated empty periods do not increment it again;
  a nonempty accepted WRITE allows the next empty episode to be counted.
- `discontinuities` counts terminal engine failure affecting the session. It
  does not count lost or repeated audible frames.

`state` is `AUDIO_STATE_READY` or `AUDIO_STATE_FAILED`. READY describes a usable
session; it does not imply active hardware, a primed queue or continuous sound.

STATUS and RELEASE remain available to the owner after terminal engine failure.
WRITE returns UNAVAILABLE, and acquisition of an unowned session remains
unavailable until reboot. Ordinary source starvation supplies silence without
failing the engine.

## Writable waits

An owned audio grant accepts `WAIT_WRITABLE` through WAIT_MANY. Readiness is
level-triggered: at least 1,024 free frames guarantees capacity for one
maximum-size WRITE at observation time. It reserves nothing. Other readiness
interests are rejected; terminal session failure returns the automatic
`WAIT_ERROR` bit. WAIT_MANY supplies the existing deadline and caller-stop
semantics; WRITE has no separate deadline.

Readiness workers inspect locked snapshots of owner, free capacity and failure
state. They do not borrow mutable PCM queues. The audio worker publishes changes
before notifying them, outside the snapshot lock. Successful WRITE reduces free
capacity and emits no readiness notification. Consumption notifies when free
capacity crosses the 4096-byte writable threshold; acquisition, release, exit
and failure still notify both the readiness and network workers, including for
mixed audio/TCP waits.

## Validation and refusal

Validation order in the [object adapter](../../kernel/object/audio.c) is:

1. Operation, then playback rights and grant-space identity.
2. Exact request payload extent and minimum reply capacity, then request copy.
3. WRITE size limit, frame-length alignment, source-buffer access and reply-buffer
   access. ACQUIRE and STATUS validate their reply buffers at this step too.
4. Session ownership and generation capture for WRITE, STATUS and RELEASE.
5. Worker caller-stop checks, owner/generation rechecks and session or engine
   admission, with stop checked again before commit.

Unknown operations return `CALL_BAD_OPERATION`; malformed extents or alignment
return `CALL_BAD_REQUEST`, and inaccessible memory returns `CALL_BAD_BUFFER`.
Oversize WRITE returns `CALL_LIMIT`. Missing rights, another space or another
session owner returns `CALL_DENIED`. ACQUIRE first rejects an existing owner
with `CALL_BUSY`, then an unavailable engine with `CALL_UNAVAILABLE`; the eight
session limit or generation exhaustion returns `CALL_LIMIT`, and queue allocation
failure returns `CALL_NO_MEMORY`. A stopped caller returns
`CALL_ENDPOINT_CLOSED` before commit. Failure publishes no reply payload.

## Handoff and output limits

WRITE stages PCM in the task's reusable shared BSP request storage before
publication. The common executor forwards its typed request to the sole audio
worker; it needs no private-root or capability-table loan. The parked caller
keeps process identity and its grant alive until completion. The worker clears
borrowed pointers before waking it and never accesses completed request storage.

Process exit runs on the BSP without allocation or sleeping. It clears ownership,
invalidates the generation and records a cleanup notice, retaining an
execution-group cleanup token when applicable. The audio worker drains notices
before requests and mixing, frees queue storage and finishes that token without
retaining the retired process. Generation/owner checks, queue consumption and
direct DMA publication share a
bounded IF=0 phase, so exit cannot interleave with a mix commit. Already published
DMA remains owned by the engine until reboot.

The owner accepted starting on the first queued frames without a priming
threshold. Eight 10 ms hardware periods therefore can begin partly filled with
zeros. The initial four-CPU QEMU capture matched all 240,000 requested PCM frames
exactly, but also contained **58.667 ms of startup silence after the first
21.33 ms of PCM**. This is observed startup starvation, not a zero-gap latency
guarantee. The [task 2 report](../development/experiments/audio-task2/README.md)
records qualification and the accepted tuning; the
[engine reference](../devices/hda.md#progress-and-refill-limits) describes its
position and scheduling limits. Native AMD/ALC257 one-session tones and an
eleven-minute eight-session silent run are
[qualified](../devices/hda.md#qualification-and-remaining-scope). There is no master volume or per-session gain; full-scale speaker tones were painfully
loud. SDL2 and Quake sound remain separate consumer work.
