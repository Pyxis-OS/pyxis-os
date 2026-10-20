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

### Task 9 first slice (decisions accepted, owner, 2026-10-20)

**This is a first slice, not the final implementation.** The intended end state,
from the [design](capabilities-and-providers.md#data-flow-in-the-shell):
capabilities flow through every pipe, consumers take and use values natively,
and no producer copies a capability's bytes anywhere. The slice builds the pipe
mechanism, one end-to-end path and one consumer that uses a value as a value;
each narrower choice below is a deliberate
limit with its revisit point, recorded under [Later](#later). Implementation
needs a separate go.

**What crosses the pipe.** A pipe carries an ordered stream of segments: byte
runs, as today, and **values**. A value is one moved capability, moved as
`FILE_HANDOFF` moves a value into an [output capture](../interfaces/output.md),
with its grant and lineage moved, not copied. A new `PIPE_HANDOFF` on a write endpoint appends a value
segment; WRITE keeps appending bytes. Segments keep their order, so a script
that runs `cat f` then `echo x`, piped into `wc`, still counts `f` then `x`, and
several writers (copied endpoints, `2>&1`) interleave whole segments as bytes
interleave today. Each pipe keeps its 64 KiB byte ring and gains a small fixed
number of value slots, initially 4; that number is an implementation limit, not
a contract. A handoff into full slots waits like a full write, and TRY returns
WOULD_BLOCK. Eligibility matches the existing libc handoff helper: a fresh,
unread, READ-only re-readable value or a held failure. Anything else, or a pipe
created without value support, gets a definite UNSUPPORTED before commit, the
source stays with the producer, and it copies bytes as today. Nothing is retried
after an accepted or uncertain handoff.

**Readers.** PIPE_READ returns bytes up to the next segment boundary. At a value
it transfers nothing and returns a new definite status, `CALL_PIPE_VALUE`; a new
`PIPE_RECEIVE` then moves that value, reserving destination slots first, exactly
as capture TAKE does. READABLE readiness covers either kind of head segment, and
EOF still means the last writer closed and the queue is empty. Libc's pipe
descriptor backend handles `CALL_PIPE_VALUE` in its blocking and try paths: it
receives the value, reads it to its end through ordinary FILE reads, closes it
and continues with the pipe, so byte-only programs need no change. Any raw
PIPE_READ or PIPE_TRY_READ caller that meets a value-capable pipe gets
`CALL_PIPE_VALUE` and must receive the value itself; there is no in-tree case
today (below), and later raw readers follow this rule.

**The value consumer.** `wc -c` answers from SIZE without reading. The slice adds
`pyxis_input_take()` to libpyxis together with this consumer, not before it: at a
segment boundary, with nothing buffered, it moves the next value out of a stdio
input stream, or reports that bytes come next. A Pyxis patch to sbase `wc`, in
the ports repository, uses it when only `-c` is requested: each value adds its
SIZE and is closed unread, byte segments are counted as today, and a failed value
reports its error. `head` gains nothing, because it reads only a prefix; `tail`
seeking and other value consumers are [later](#later).

**Raw pipe readers.** Every in-tree caller of PIPE_READ or PIPE_TRY_READ at
`05efd5b2`:

| Reader | Pipe it reads | In the slice |
| --- | --- | --- |
| libc descriptor backend (`read`, stdio, `fread_some`, and libuv streams through `pyxis_descriptor_try_read`) | Any stdin or descriptor pipe, including shell user stages | Adopts values (task 2) |
| Native-jobs SDK capture (`libc/job_pipeline.c`), used by Lua jobs | Pipes it creates | Byte-only pipes; unchanged |
| Shell HTTP staging worker (`http-stage`) | The shell's staging pipe | Byte-only pipe; unchanged |
| libuv loop wake pipe, SDL2 event wake pipe | Pipes they create | Byte-only pipes; unchanged |
| Kernel | `kernel/object/pipe.c` implements the operations | Task 1 |

Session, terminal and remote relays only check pipe rights on launch bindings or
write; they read no pipes. Links uses its own in-process virtual pipes.
**Recommendation: adopt, not serve bytes in the kernel.** Only the libc backend
can meet a value-capable pipe, and it adopts values in task 2 together with the
ABI change; out-of-tree compatibility is not kept. Having the kernel serve a
value's bytes to undeclared readers would need PIPE_READ to read arbitrary
capabilities, including provider-served ones, on the reader's behalf, a
cross-object read path the pipe does not have, and it would turn the slice back
into a copy.

**Authority (accepted, owner, 2026-10-20).** A pipe value gives its reader the
same object `READER < SOURCE` would, and no more:

- **Narrowed READ only.** PIPE_HANDOFF narrows the moved grant to READ before
  commit. An observation handle never enters a pipe; it stays with the producer.
- **The live object.** The reader may read the object while it holds the value,
  including re-reading or seeking a value it took, and later writes to an npfs
  file are visible as to any READ holder. Lineage revocation is the cut: cutting
  a revocable value denies the reader's later reads, whether the value is queued
  or taken; reads already done stay done. This slice adds no snapshot machinery.
- **Description without names.** DESCRIBE through a pipe value reports type,
  size, outcome and media type, never a name, path or URI. INFO reports the held
  object's identity and modification time, as for any READ holder; neither
  names a path. Enforcement is task 9a below: until it lands, a pipe value
  describes as `READER < SOURCE` does, including the free-text detail, and no
  program describes a pipe value.

**Lifetime.** A handoff moves the producer's grant into the pipe; the producer
no longer holds it. The pipe owns queued values, then the reader does. Values
carry no new retention charge: their backing keeps its original account, as
with captures. The producer exiting after a handoff is an ordinary
writer close: the value stays queued and EOF follows it. Exiting or failing
before commit leaves the source with the producer's cleanup. The last reader
closing drops queued values outside the pipe lock and fails later writes and
handoffs with ENDPOINT_CLOSED (EPIPE), as for bytes.

**Failures.** A held-failure value fails its reader: libc's first read of it
returns that outcome's errno (ENOENT, EACCES, EIO, EFBIG), so `cat https://bla |
grep x` makes grep report the error and exit non-zero instead of seeing empty
input. The producer's own exit status is unchanged.

**Shell.** No syntax changes. The shell sets `PIPE_CREATE_VALUES` only on pipes
between user stages; it holds no values.

**First pair.** `cat SOURCE | READER` with a libc byte reader (`wc`, `grep`,
`jq`): single-source `cat` already hands off through the shared helper, which
today refuses PIPE sinks before submission and copies. Qualify a native npfs
file, an HTTP reply snapshot and a 404 failure.

**Accepted decisions (owner, 2026-10-20).**

- **A. Ordered segments.** Values and byte runs interleave in order; a value is
  not restricted to being the entire stream.
- **B. The creator opts in.** Only the shell enables values, on user pipeline
  stages, in this slice.
- **C. Byte readers see one non-seekable stream.** Programs that want the value
  take it explicitly with `pyxis_input_take()`, which lands with `wc -c`.
- **Authority.** Narrowed READ only, the live object, and DESCRIBE without
  names, as above (owner accepted the review defaults).

**Tasks:** task 1 is implemented for review (the kernel segments in
[pipes](../interfaces/pipes.md#value-segments)); tasks 2 and 3 follow its merge.

| Task | Scope |
| --- | --- |
| 1. Kernel segments | Creation flag, value slots, PIPE_HANDOFF, PIPE_RECEIVE, the boundary status, readiness, close and drain, ABI; QEMU and GDB checks for ordering, full slots, early reader and producer exit, revocation and failed values |
| 2. Userland adoption | The libc descriptor backend (blocking and try paths), `pyxis_input_take()` with the sbase `wc -c` patch in ports, the handoff helper accepting value-capable pipes, the shell flag on user stages, dependency PRs then parent pins |
| 9a. Restricted description | A per-grant description restriction: DESCRIBE through a restricted grant omits the free-text detail (a held failure's detail or a provider's text); the failure's error body stays available. Every COPY, launch, IPC transfer, capture handoff and forwarder preserves the restriction, and PIPE_RECEIVE installs values with it. Own security review; lands before any program describes a pipe value (owner, 2026-10-20) |
| 3. Qualification and docs | The first pair on native files, HTTP snapshots and failures, `wc -c` answering from SIZE, the script writing a value then bytes, `head`; a matched before/after of `cat big \| wc -c` recording revisions, configuration and repeated samples; update [pipes](../interfaces/pipes.md) and [output](../interfaces/output.md) |

## Later

- `rm` on held capabilities as well as names.
- **Capability pipes beyond the task 9 slice.** Each is a deliberate first-slice
  limit:
  - *Shell-only opt-in.* Revisit once the slice is qualified: make every pipe
    carry values, ending the creation flag.
  - *Byte-only native jobs, Lua capture, libuv and `http-stage`.* Revisit with
    the previous item; each adopts segments, and the staging worker hands a value
    straight to its transaction instead of copying it.
  - *Libc reads values through as bytes; `wc -c` is the only value consumer.*
    Revisit with the next consumers: `tail` seeking, a pager, `describe`-aware
    tools and media-type rendering in the terminal.
  - *Live objects, no snapshots.* Revisit if a reader must not see writes made
    after the handoff; that needs snapshot semantics the slice does not add.
  - *Non-seekable stdin view.* Revisit with the previous item: offer stdin as the
    value's FILE when one value is the whole stream.
  - *Four value slots per pipe.* An implementation limit, not a contract;
    revisit with a workload that queues more values than that.
  - *Producers outside `cat`.* Revisit as tools gain re-readable output, so no
    producer copies a capability's bytes anywhere.
- The provider-side open questions in the design: provider lifecycle and the
  cost of interposition, shared memory, and provider policy.
