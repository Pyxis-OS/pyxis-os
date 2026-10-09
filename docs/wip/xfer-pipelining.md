# xfer pipelining

Accepted by the owner on 2026-10-09, all three decisions as the defaults below;
implementation in progress. It pays down the one-chunk-in-flight part
of [remote transfer memory and staging limits](../technical-debt.md#remote-transfer-memory-and-staging-limits).
The current transfer behaviour is in the
[remote terminal guide](../userland/remote-terminal.md#explicit-file-transfer)
and the [`xfer` notes](../../userspace/xfer/README.md).

## Why transfers are slow

Each side sends one 2048-byte chunk, about 2.8 KB as an OSC 5113 frame, and
waits for a PROGRESS reply before the next:
- **Uploads:** `pyxis-remote` waits in `UPLOAD_ACK`.
- **Downloads:** `xfer send` waits in `wire_expect`.

Natively, 15 MiB is 7680 chunks in 6.5 s up and 7.5 s down. That is 0.85–0.98 ms
per chunk, against a ping of about 0.6 ms on the same link. So the rate is one
round trip per 2 KiB, while TCP itself now moves about 100 MiB/s.

## Proposed protocol

Only the data phase changes. Negotiation, confirmation, the announced SHA-256,
staging, publication and the finish handshake stay as they are.

- **Window.** The sender may have at most 64 KiB of file data that the receiver
  has not acknowledged: 32 chunks.
- **Chunks** stay at 2048 bytes, so every frame still fits the 4096-byte OSC
  limit on both sides.
- **Acknowledgements.** The receiver's PROGRESS reply keeps its form and still
  carries the cumulative byte offset. The receiver sends one whenever 16 KiB or
  more has arrived since its last acknowledgement. Since 16 KiB is below the
  window, the sender never waits on an acknowledgement the receiver is holding
  back.
- **`end_data`** still carries the last chunk. The replies to it stay as today:
  OK from the host, and nothing from Pyxis before `finish`.
- **Reading replies.** The sender reads replies only when the window is full and
  after `end_data`. When downloading, Pyxis reads replies in blocks, as today:
  the host sends nothing else during the data phase.

64 KiB is about the bandwidth-delay product at 100 MiB/s and a 0.6 ms round trip.
It matches the 65,535-byte TCP windows, so a larger credit would only queue more
data in buffers and make cancellation drain longer. Acknowledging every 16 KiB
sends 4 replies per window instead of 32. Each reply is a separate small TCP
segment, because Nagle is off.

## Fit with the remote terminal

The remote terminal's frames, the 4 KiB guest input rings and the server are
unchanged.

- **Uploads.** A full window is about 89 KB of frames, more than the guest
  buffers between socket and `xfer`: the server's one pending INPUT frame plus
  the terminal's 4096-byte ring. The rest waits under TCP flow control:
  - when the ring is full, the server stops reading frames;
  - the guest's 64 KiB receive window then fills;
  - `pyxis-remote` queues a data frame only when its 64 KiB outgoing buffer has
    room.

  That is ordinary flow control, not lost input. Each side keeps reading while
  it writes, so the small replies always get through.
- **Downloads.** The only guest input during the data phase is replies, at most
  four per window, about 80 bytes each, far below the ring.
- **Typeahead.** During a transfer the host consumes the user's keys. The
  [remote typeahead limit](../technical-debt.md#process-termination-and-ctrl-c)
  is unchanged; Ctrl+] still closes the session.

## Failure, cancellation and deadlines

The contract stays. The existing in-order cancel handshake already handles
several frames in flight:
- the cancelling side sends `cancel`;
- the other side discards transfer frames until it reads it;
- the other side then answers CANCELED, and the cancelling side drains until
  that reply.

The window only bounds what is discarded.

- **Host Ctrl+C.** The host's `cancel` queues behind at most one window of data
  already sent. Pyxis writes those chunks to staging, then cancels and removes
  the staging file.
- **Receiver failure,** for example a full destination or a hash mismatch. The
  receiver reports its error status at once. The sender sees it at its next
  reply read, at most one window later. The receiver drains the rest, as it does
  today.
- **Drain time.** Up to 89 KB stays far inside the five-second cancellation
  deadline.
- **The 120-second deadline** now runs per received reply, not per chunk: a
  transfer still fails after 120 s without progress.
- **Unchanged.** There is no resume, no progress display and no compression.
  Abrupt death leaves the same `.NAME.xfer-partial-ID` file.

## Owner decisions

Accepted 2026-10-09, all as the defaults.

1. **Window.**
   - **Default:** a 64 KiB credit of file bytes, 2048-byte chunks, cumulative
     replies at least every 16 KiB.
   - **Per-chunk replies inside the same window:** simpler accounting, but
     eight times the reply frames and segments.
   - **No replies, paced by TCP alone:** fewest frames. But in-flight data would
     then be bounded by TCP and terminal buffer sizes, which would become an
     unstated part of the protocol. Pyxis's sender would also need non-blocking
     input checks between writes to see a receiver's failure.
   - **Larger chunks:** they need OSC frames above 4096 bytes, and so changes to
     the frame limits on both sides. Within today's limit, about 2.9 KiB chunks
     would cut frames by about a third. Revisit only if per-frame cost proves to
     be the next bound.
2. **Failure semantics.**
   - **Default:** keep today's contract through the existing cancel handshake,
     discarding at most one window after a failure or cancellation.
   - **Alternative:** add resume from the staging file. That needs a new
     ownership rule for staging names, so it is a separate task.
3. **Versioning.**
   - **Default:** replace. The required negotiation key `px_sha256=1` becomes
     `px_xfer=2`, covering the digest and the window. A peer without it is
     refused with a message to rebuild `pyxis-remote` from the same revision as
     the image. That is already the practice: the tool is built from the Pyxis
     tree, which pins the `xfer` it ships.
   - **Alternative:** coexist, negotiating the window and falling back to one
     chunk in flight. That is two data paths on each side, kept only for
     mismatched builds.

## Measurement plan

Before implementing, a matched QEMU baseline on the development VM. After it,
the same runs, as regression evidence only: nested-VM rates are not a native
prediction.

**Native runs, for the owner:**
- **Setup:**
  - the ThinkPad, wired, on AC, with PXE builds of main before the change and
    of the change;
  - the desktop's `pyxis-remote` built from the same revision as each image;
  - only that one remote session connected, with no other agent's session or
    monitoring connection;
  - the ThinkPad's local console left idle.
- **Files:** random 15 MiB and 700 MiB files, uploaded into `tmp://` and then
  downloaded from it into a disk-backed `--download-dir`. RAM backing avoids
  stick wear; 700 MiB needs that much free memory on the ThinkPad.
- **Runs:**
  - 15 MiB: three each way on both builds;
  - 700 MiB before the change: once each way, about 5 minutes each at today's
    rate;
  - 700 MiB after the change: three each way.
- **Timing:** a stopwatch from pressing `y` to the prompt, as in the earlier
  re-timing. After the change, 15 MiB may take well under a second, near
  stopwatch resolution. The 700 MiB runs carry the rate; the 15 MiB runs show
  start-up cost.
- **Checks:** SHA-256 on both ends; memory flat, from fastfetch before and after
  and the host RSS of `pyxis-remote`.

**Correctness checks during implementation, in QEMU:**
- host Ctrl+C mid-upload and mid-download;
- a source changed during a download, which must not publish;
- an old `pyxis-remote` against the new `xfer`, which must be refused cleanly;
- shell input after each transfer is intact.

**Expectation:** the round-trip bound goes away. The next bound is per-frame
work: base64, OSC parsing, SHA-256, terminal copies and the server's
re-framing. Natively, 2 KiB TCP writes alone reach about 44 MiB/s. No rate is
promised before measurement.

## Out of scope

Resume, compression, multiple files, a progress display, and any change to the
remote terminal wire frames or the guest input rings.
