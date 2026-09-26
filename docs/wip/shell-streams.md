# Capability-backed standard streams, redirection and pipelines

Status: tasks 1 and 2 implemented. Independent standard-stream bindings use
explicit protocol information and dedicated handles adopted directly by libc.
Foreground file redirection and cat stdin consumption are available. The later
pipe/pipeline policies below remain proposals with decision gates. Discuss unresolved choices before their implementation task; this document
does not authorize starting code work.

## Intended result

An ordinary program can use libc stdin/stdout/stderr with terminal, file or pipe
resources selected by its launcher. The shell can redirect files and connect
foreground external programs without the programs knowing the backing resource.

Illustrative completion examples, subject to the syntax decisions below:

```sh
cat app://share/hello.txt > home://copy.txt
cat < home://copy.txt
cat home://missing.txt 2> home://errors.txt
cat home://copy.txt | cat > home://another-copy.txt
```

Add one small bounded consumer, such as `head`, to demonstrate an early-closing
reader as well as complete transfer. Select its exact scope before that task.
Large transfers must work with bounded pipe storage; a program fault or endpoint
closure must not leave the other side asleep on a dead peer.

## Current foundation

[Stdio](../stdio.md) already distinguishes files and consoles. File positions
belong to each FILE and native operations use explicit offsets. Standard streams
use independent protocol/handle bindings and directly adopt dedicated handles,
without retaining startup copies. They need no initial user heap allocation.
Named terminal resources remain separate.

[Launch](../processes.md) already delegates restricted copies into a child's
handle table. The shell currently launches and waits for one foreground child;
its simple background mode drops input authority. A process observer only waits
for completion: closing it does not terminate the child. There is no general
process cancellation or atomic multi-child launch to assume for pipeline cleanup.

## Agreed standard-stream and authority contract

- Provide independent stdin, stdout and stderr startup bindings with explicit
  protocol information in fixed launch/startup slots. Protocol
  metadata selects the userspace adapter; it neither grants rights nor overrides
  kernel validation of object type and authority.
- Keep existing file and console protocols. Libc dispatches to the declared
  backend, later adding pipe operations. No universal kernel resource protocol,
  generic dispatch framework or representation negotiation is needed.
- Keep terminal-specific grants separate from standard streams. Line editing,
  cursor operations, dimensions and physical input are not file/pipe operations.
  Replacing stdout must not turn its destination into a terminal. Decide terminal
  input delegation per child so a noninteractive pipeline stage cannot compete
  with the foreground input consumer through an extra console grant.
- Normal boot still binds streams to the terminal; stderr is independently
  selectable. Missing authority remains an unavailable stream, without an
  implicit fallback to the terminal or kernel log.
- Delegate actual capabilities through child startup, not numeric handles in
  arguments or environment strings. Copy only the needed rights, retain each
  child's grant, and close temporary shell copies after successful handoff.
  Capability moves are not required. The shell's close does not revoke the child.
- Preserve resource semantics: files can seek with per-FILE offsets; consoles
  and pipes are sequential. Copying a file grant does not create a shared seek
  cursor. Do not silently promise descriptor-duplication behavior for stdout and
  stderr redirected to the same object.
- Update in-tree producers and consumers together, without legacy startup
  compatibility or a version increment solely for the layout change. Preserve
  ownership cleanup for normal exit and faults, and keep early stdio usable
  before heap initialization.

## Implemented task 1 contract

Launch selects three optional standard-stream grant indices; startup publishes
protocol/handle pairs. Console and file protocols use the existing protocol tags.
Every present stream index is exclusive: it cannot also appear in another stream,
resource, root or working-directory binding. Installation creates one child
handle per grant entry, and libc adopts it without copying. Explicit terminal
grants remain separately owned. Stream handles have exactly READ authority for
stdin or WRITE authority for stdout/stderr.

Missing streams are unavailable with EBADF on nonempty I/O, not EOF or a fallback.
File positions start independently at zero; adoption never truncates or appends.
The init script, session, shell and native launch examples forward actual streams.
Background launch omits stdin and continues withholding terminal/keyboard input.
Shebang forwarding preserves the same stream indices. Detailed ownership and
errors are documented in [stdio](../stdio.md) and [launch](../processes.md).

## Implemented task 2 contract

Foreground external commands accept `<`, `>` and `2>` after the command name,
interspersed with arguments. The interactive and script paths share the parser.
Unquoted operators need no surrounding whitespace; quotes and escapes keep them
literal. `2>` selects stderr only with an unquoted token-start `2` immediately
before `>`; `2 > file` keeps `2` as an argument. Other descriptor prefixes and
unsupported combinations (`>>`, `<<`, `<>`, `2>&1`, pipelines) are errors.
Each redirect needs one nonempty filename word; duplicate redirects for a stream,
leading redirects and redirect-only commands are rejected. Builtins, `session`
and background commands reject redirection before any file is opened.

The complete line is parsed first, then the executable is resolved/opened.
The shell opens every redirect target in source order, without truncating existing
files. Input needs an existing readable file; output opens or creates a writable
file. Only after all opens succeed does it truncate the output targets in source
order and invoke the existing launch operation. Relative paths use the shell's
current directory and grants, including changes from `cd` and mount setup.
The child receives actual file capabilities and never reopens redirect names.
The shell closes temporary handles on success and failure, before waiting.

Creation may leave empty files after a later open failure. After truncation begins,
a failed resize, malformed image, missing interpreter, allocation or launch failure
may leave outputs truncated; there is no rollback. Redirecting onto an input file
can destroy its contents, including `cat file > file` and `cat < file > file`.
There is no same-file/alias check. stdout and stderr have independent positions
even when they name the same underlying file; writes can overwrite one another.
These are accepted limitations, not shared-offset or transactional semantics.

Unredirected streams preserve the shell's startup bindings. Preparation errors use
the shell's own stderr; `2>` selects only child diagnostics. `<` withholds named
terminal input and keyboard grants so the child cannot bypass stdin. Explicit
terminal output remains separate authority. Append, descriptor duplication,
here-documents, expansion, command substitution, pipelines and builtin/background
redirection are deferred.

`cat` without operands reads stdin; each `-` operand reads that same stream at its
current position without closing or rewinding it. Explicit paths retain their
existing behavior; `./-` names a literal dash file. Cat reads terminal stdin one
byte at a time for immediate output, retaining bulk reads for files and redirected
file stdin. Terminal stdin remains raw and blocking, with no EOF convention or
child interruption from Ctrl+C. This task does not introduce line discipline or
Ctrl+D semantics.

## Proposed pipe contract

Create a kernel-owned bounded byte stream with separately granted read and write
endpoints. Proposed capacity: 64 KiB per pipe, allocated at creation under the
existing BSP allocation rules. Allocation failure must unwind both endpoints.
Capacity, per-call limits and creation authority need agreement before task 3.

Proposed native behavior:

- A nonempty read returns available bytes, potentially short. Empty storage waits
  while writers remain. After the final writer closes, drain buffered bytes and
  then return zero for EOF. A zero-length read is a no-op, not an EOF observation.
- A nonempty write can return positive partial progress when space exists; a full
  buffer waits for space. Last-reader closure wakes writers with a defined
  broken-pipe status, translated to EPIPE by libc, without adding signals.
- libc continues short transfers according to fread/fwrite semantics; seeking a
  pipe reports ESPIPE. Define errors after prior progress without losing or
  replaying confirmed bytes.
- Endpoint references govern closure, including copied startup grants and libc's
  own references. The shell and unrelated children must not retain unused writers
  and prevent EOF, or retain unused readers and hide peer closure.
- Synchronize parked waiters, user-buffer access and object teardown using current
  scheduler/VM ownership rules. Do not retain another CPU's user pointers or task
  stack as durable queue storage. No lock spans a context switch.

Before implementation, decide multiple-reader/writer behavior, write interleaving,
zero-length writes, fairness expectations and process-exit cleanup. The first
shell use is one producer and one consumer per pipe, but copied grants still need
safe behavior. No message boundaries, PIPE_BUF-style atomicity, nonblocking mode,
wait sets or dynamic buffer growth are assumed.

## Pipeline launch and failure gate

Proposed shell scope: foreground external-command pipelines. Launch all stages
before waiting, reap every child, keep stderr separate, and use the last stage's
exit status for the pipeline. Pipeline length limits, redirection precedence,
input/keyboard ownership and existing background/session syntax need agreement.
Background pipelines, builtins in pipelines and full job control are deferred.

A failure launching a later stage can leave earlier stages running. Closing pipe
ends resolves I/O waits but cannot stop a child doing unrelated work or reading
the terminal. Waiting blindly can hang the shell, and closing process observers
is not cancellation. Before task 4, choose a concrete bounded solution, such as
preparing children before making them runnable or explicit scoped cancellation.
These are alternatives to discuss, not instructions to build either mechanism.
Define cleanup for failed preparation, faults, observation and shell exit as part
of that choice; keep it focused on the pipeline's actual needs.

## Focused PR tasks

- [x] **1. Independent standard-stream bindings.** Define the explicit protocol
  metadata and missing-stream behavior in launch/startup, adapt libc's existing
  file/console backends, and update init, session, shell and other in-tree launch
  consumers. Ordinary terminal boot remains the visible behavior. Keep terminal
  resources distinct; no pipe object or new shell syntax in this PR.
- [x] **2. File redirection and stdin consumer.** Resolve the parser/truncation
  decisions, implement the selected foreground redirections and cat stdin mode,
  and demonstrate separate stderr. Carry actual grants through launch and unwind
  failures. This is the first visible use of file-backed standard streams.
- [ ] **3. Bounded native pipe endpoints.** Resolve the pipe contract, add creation,
  READ/WRITE operations and libc support, with reference-based EOF, blocked-peer
  wakeup and cleanup. Document the protocol beside its ABI; no shell pipelines
  yet. Validate through ordinary build/boot and debugger inspection as appropriate.
- [ ] **4. Safe multi-child launch lifecycle.** Discuss and implement the selected
  preparation/failure strategy before exposing pipeline syntax. Extend only the
  process/launch operations actually required, with explicit ownership of child
  observers and inherited endpoint references.
- [ ] **5. Foreground shell pipelines.** Connect stages using delegated pipe
  grants, launch before waiting, close unused copies and observe all children.
  Integrate agreed redirection precedence, exit status and partial-launch handling
  in both interactive and script paths. No background pipelines or job control.
- [ ] **6. Consumer and milestone handoff.** Add the selected small consumer and
  manually exercise complete transfer, data larger than pipe capacity, early
  reader exit, EOF and ordinary error paths. Rewrite this document as implemented
  usage/contracts in docs, update stdio/shell references, and move still-relevant
  deferred decisions to technical debt or an appropriate WIP note.

Each task should remain reviewable on its own; split further if the chosen
lifecycle solution requires it. Stop and discuss open policy before implementing
it. Use ordinary builds, interactive QEMU and debugger inspection; do not add
self-tests, test infrastructure or boot/output automation.

## Deferred consumers and boundaries

Prepared HTTP requests could give a producer a body writer and a consumer a
response reader through the same stdio bindings, with explicit submission by the
request owner. Flush/close of one copied grant must not implicitly submit it.
Returning a new resource from a running helper to the shell is a separate
[capability handoff](userspace-scheme-providers.md#prepared-requests-and-shell-handoff)
from initial child delegation.

HTTP/SQLite/Git providers, capability-valued shell variables and discoverable
[resource representations](userspace-scheme-providers.md#discoverable-resource-representations)
remain future work. This milestone does not select MIME types, auto-convert data,
implement structured rows or require jq/awk ports. It also does not redesign the
scheduler, unpin processes, add a POSIX descriptor table, or introduce ownership
moves, signals, general cancellation or job control except for any narrowly
approved lifecycle support in task 4.
