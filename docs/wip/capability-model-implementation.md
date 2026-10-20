# Capability model implementation

Status: **milestones A and B complete; milestone C remains planned.** The owner
confirmed B complete on 2026-10-16. This implements the settled
[capabilities and providers](capabilities-and-providers.md) design. Codex beta
owns integration; Codex delta owns the remaining kernel work in C. Each task
starts with a short scope note and stops only for unresolved behaviour, authority
or lifetime decisions; ABI layout and other
routine choices need no approval.

Libc, shell, web/HTTP and Git work outside this milestone stays paused until the
task that owns the area is delivered.

## Agreed decisions (2026-10-14)

- **Order:** milestone A, then B, then C. Each is usable on its own. Owner
  exception, 2026-10-15: bring only C9's capability-output handoff forward as an
  A5 prerequisite; general pipe integration and qualification stay in C9.
- **Ownership:** as in the status line.
- **Expansion of `$x`:** text values, from literal assignments such as
  `f="home://notes.txt"`, expand to their text, as in bash. The text is resolved
  through the caller's own environment and confers no authority. Every other
  value (captured `$()` output, replies, prepared requests, devices,
  directories) passes as `x://` with its entry, so `cat $job` reads the reply
  and `echo $job` prints `job://`. Inline `$(…)` used as an argument passes a
  capability, as bash's `<(…)` does; idioms such as `rm $(find …)` do not apply.
- **Remove** works on names. Removing through a held FILE capability without a
  name is refused for now; making `rm` work on both names and capabilities is a
  later item.

## Rules for every task

- **Replace, don't coexist.** Each task removes the interface it replaces and
  updates every in-tree consumer in the same change, across the parent,
  userland and ports repositories, with linked PRs and stated merge order.
- **Interfaces beside code.** Each task updates the matching
  `docs/interfaces/` reference; this document only tracks progress.
- **Measure natively.** Repeat the matched ThinkPad workloads after A and C:
  native/text OPEN, small-program launch, full startup allocation and pipe
  throughput. The [baseline run sheet](../development/experiments/capability-model-baseline.md)
  records the satisfied gate, frozen hands-free Remote method, exact revisions,
  samples and limits. After-A and after-C repeats remain pending; milestone
  completion does not claim those measurements ran.

## Milestone A: the core — complete

Owner-confirmed 2026-10-15: tasks 1–5 are merged. Implemented contracts and
limits live in [forwarders](../interfaces/forwarders.md),
[the environment](../interfaces/environment.md),
[typed OPEN and describe](../interfaces/file-providers.md),
[transactions](../interfaces/transactions.md),
[libc](../userland/stdio.md#typed-resources-and-transactions) and
[the shell](../userland/shell.md#typed-values-and-command-sequences).
The delivered [output handoff and retention interface](../interfaces/output.md)
is C9's starting point, not general capability-pipe integration.

The [shell/capture](../development/experiments/capability-shell.md),
[handoff](../development/experiments/capability-output.md) and
[retention](../development/experiments/retained-values.md) records preserve their
qualification, measured revisions and limits; the pending native repeats remain
listed above.

## Milestone B: HTTP on the model — complete

Owner-confirmed 2026-10-16: tasks 6–8 are merged, including #902,
userland #279, ports #120 and #905. The completed contract lives in
[HTTP resources and transactions](../interfaces/http-transactions.md),
[HTTP fetching and preparation](../userland/http-fetch.md),
[libc transactions](../userland/stdio.md#typed-resources-and-transactions) and
[shell captures](../userland/shell.md#held-output-captures).

Before/after revisions, native and nested measurements, qualification and limits
remain in the [snapshot](../development/experiments/capability-http.md),
[preparation](../development/experiments/capability-http-prepare.md) and
[retirement](../development/experiments/capability-http-retirement.md) records.
Deferred credential storage, provider responsiveness, throughput and transitional
syntax are tracked in the reference documents and technical debt. C9 below is the
remaining implementation plan; B's close does not deliver general capability pipes.

## Milestone C: capability pipes

A5 already supplies byte-or-value output, owned handoff, failed values and
session retention. Its collector remains process-local and nondelegatable;
re-readable values move without copying or another byte-retention charge.
C9 must integrate with those [existing ownership and accounting rules](../interfaces/output.md).
General pipelines remain [byte pipes](../interfaces/pipes.md) until then.

- [ ] **9. Pipes carry capabilities.** A pipe can pass its producer's capability
  to the consumer instead of copying bytes, so files and replies travel without
  a copy and a failed capability fails its reader. Byte pipes remain the base
  case, so programs that only read bytes are unaffected.

### Proposal for task 9 (2026-10-20, awaiting the owner)

Proposed, not accepted; no code. The [design](capabilities-and-providers.md#data-flow-in-the-shell)
already decides that `A | B` hands A's output capability to B and that a failed
capability fails its reader. This fixes the smallest mechanism for it.

**What crosses the pipe.** A pipe carries an ordered stream of segments: byte
runs, as today, and **values**. A value is one moved capability, the same payload
`FILE_HANDOFF` moves into an [output capture](../interfaces/output.md): one owned
READ handle plus an optional observation handle, with grants, masks and lineage
moved, not copied. A new `PIPE_HANDOFF` on a write endpoint appends a value
segment; WRITE keeps appending bytes. Segments keep their order, so a script
that runs `cat f` then `echo x`, piped into `wc`, still counts `f` then `x`, and
several writers (copied endpoints, `2>&1`) interleave whole segments as bytes
interleave today. Each pipe
keeps its fixed 64 KiB byte ring and gains a fixed number of value slots (4); a
handoff into full slots waits like a full write, and TRY returns WOULD_BLOCK.
Eligibility matches the existing libc handoff helper: a fresh, unread, READ-only
re-readable value or a held failure. Anything else, or a pipe created without
value support, gets a definite UNSUPPORTED before commit, the source handles stay
with the producer, and it copies bytes as today. Nothing is retried after an
accepted or uncertain handoff.

**Readers.** PIPE_READ returns bytes up to the next segment boundary. At a value
it transfers nothing and returns a new definite status, `CALL_PIPE_VALUE`; a new
`PIPE_RECEIVE` then moves that value, reserving destination slots first, exactly
as capture TAKE does. READABLE readiness covers either kind of head segment, and
EOF still means the last writer closed and the queue is empty. Libc's pipe
descriptor backend (`libc/descriptor.c`) handles `CALL_PIPE_VALUE` itself: it
receives the value, reads it to its end through ordinary FILE reads, closes it
and continues with the pipe. A byte-only program therefore sees one continuous
non-seekable byte stream and needs no change; it reads the file once instead of
the producer copying it through the pipe. A capability-aware program asks
libpyxis for the next value instead (`pyxis_input_take()`), getting the FILE
itself: seekable, sized, with `describe`.

**Authority and lifetime.** A handoff moves the producer's grant into the pipe;
the producer no longer holds it. The pipe owns queued values, then the reader
does. Values carry no new retention charge: their backing keeps its original
account, as with captures. A revocable value moves with its lineage, so cutting
it denies the reader's later reads whether it is still queued or already taken;
reads already done stay done. The producer exiting after a handoff is an ordinary
writer close: the value stays queued and EOF follows it. Exiting or failing
before commit leaves the source with the producer's cleanup. The last reader
closing drops queued values outside the pipe lock and fails later writes and
handoffs with ENDPOINT_CLOSED (EPIPE), as for bytes.

**Failures.** A held-failure value fails its reader: libc's first read of it
returns that outcome's errno (ENOENT, EACCES, EIO, EFBIG), so `cat https://bla |
grep x` makes grep report the error and exit non-zero instead of seeing empty
input. The producer's own exit status is unchanged.

**Shell.** No syntax changes. The shell sets a creation flag, `PIPE_CREATE_VALUES`,
only on pipes between user stages. Its relay, staging and capture pipes, libuv's
and the native-jobs SDK's pipes stay byte-only until each adopts segments, so
their raw `pipe_try_read` loops are unaffected. The shell holds no values.

**First pair.** `cat SOURCE | READER` with a libc byte reader (`wc`, `grep`,
`jq`): single-source `cat` already hands off through the shared helper, which
today refuses PIPE sinks before submission and copies. Qualify a native npfs
file, an HTTP reply snapshot and a 404 failure.

**Tasks** (S is a narrow change, M one subsystem plus its handoffs):

1. **Kernel segments.** Creation flag, value slots, PIPE_HANDOFF, PIPE_RECEIVE,
   the boundary status, readiness, close and drain, and the ABI. QEMU and GDB
   checks for ordering, full slots, early reader and producer exit, revocation and
   failed values. **M.**
2. **Userland adoption.** The libc descriptor backend and its libpyxis helper,
   the handoff helper accepting value-capable pipes, and the shell flag on user
   pipeline stages, with parent pins. **M.**
3. **Qualification and docs.** The first pair on native files, HTTP snapshots and
   failures, the script that writes a value then bytes, `head`, and a matched before/after of
   `cat big | wc -c` for the avoided copies; update [pipes](../interfaces/pipes.md)
   and [output](../interfaces/output.md). **S/M.**

**Decisions, with defaults.**

- **A. Ordered segments.** Default: values and byte runs interleave in order.
  Alternative: a value only as the entire stream, refused once any byte or second
  writer exists. That is simpler, but whether the shell has closed its copy of
  the writer before the producer hands off is a race, so the copy would be
  avoided only some of the time.
- **B. Who opts in.** Default: the creator, with the shell enabling it only for
  user pipeline stages. Alternative: every pipe carries values and all in-tree
  raw readers (libuv, Lua capture, native jobs, `http-stage`) adopt segments in
  the same delivery.
- **C. What a byte reader sees.** Default: one non-seekable stream, with explicit
  `pyxis_input_take()` for programs that want the value. Alternative: libc gives
  stdin the value's FILE, seekable and sized, when it is the whole stream; that
  changes seek and stat behavior for existing programs.

## Later

- `rm` on held capabilities as well as names.
- The provider-side open questions in the design: provider lifecycle and the
  cost of interposition, shared memory, and provider policy.
