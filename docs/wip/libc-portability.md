# Standard C libc over the native ABI

Status: next milestone after shell streams and pipelines. The result is a small
libc descriptor layer exercised by an upstream cksum port, with tee as a
conditional second consumer. Resolve the decisions below before implementing
each task; this document does not authorize the whole compatibility backlog.

## Intended result

Pyxis should provide a standard C library implemented using its own OS
primitives and capability ABI. Simple C programs should eventually build with
minimal source changes; ordinary allocation, formatting, file I/O and other
library calls should not require each port to learn native object protocols.
Grow the existing libc toward that goal, with correct behavior and documented
gaps rather than successful stubs or repeated application-specific workarounds.
This is a direction toward conformance, not a claim that today's libc is complete.

The application-facing library contract and the kernel interface are separate
decisions. Familiar C APIs can map to native resources without prescribing
syscall names, a global Unix filesystem namespace or a POSIX kernel architecture.
Library code runs with the caller's grants; an adapter cannot manufacture missing
authority. Native libpyxis interfaces remain available where applications need
explicit capability operations.

ISO C library coverage and selected compatibility APIs are related but distinct.
Descriptor functions such as open/read/write/close are not ISO C requirements,
but many otherwise small C programs use them. Support useful subsets as actual
ports require them, without committing to full POSIX compatibility, Unix process
semantics or binary compatibility with another OS.

## Boundary for application patches

Prefer implementing conventional library behavior over patching each application
to use native APIs. Ordinary file reading, writing, seeking and allocation should
work through the application's existing library calls. Missing libc support is
a shared compatibility gap to address, not a reason to permanently rewrite those
operations in every port.

Pyxis-specific application changes are appropriate for platform integration and
intentional features. For example, teaching TCC's include-path handling to accept
URI schemes for future `http://` imports is useful integration. Replacing its
ordinary file reads solely because libc lacks the expected interface or semantics
is a workaround. Keep URI resolution and resource access in shared libraries or
providers where possible; a compiler should not need its own HTTP client.

When a bounded milestone needs a temporary workaround, record the missing library
or OS support and the condition for removing the patch as technical debt. This
direction does not require retroactively rewriting existing ports in this PR or
silently expanding the current implementation task.

## Userspace file-descriptor adapter

A future libc-owned table can map process-local integer descriptors to owned
native capabilities and the state needed to implement their operations. A
descriptor is not a kernel handle or evidence of authority. File, console and
pipe operations continue through their native protocols; closing an owned entry
releases its reference. Copied references must preserve peer-closure semantics.

Start with a bounded consumer and agree the required operations before coding.
Potential initial APIs include open, read, write, close and seek where supported,
with standard descriptors supplied from explicit startup streams. Decide absent
stream behavior, flags, error translation and descriptor allocation together.
Unsupported operations must fail honestly rather than silently approximate a
contract that the backend cannot provide.

FILE and descriptor ownership need one deliberate design before adding fdopen,
fileno or duplication:

- fdopen must establish who owns the descriptor and what fclose releases.
  fileno exposes the associated descriptor without creating a hidden reference.
- dup-style entries need shared open state where the promised semantics require
  it, including file position. Copying a native file capability alone currently
  does not supply that shared cursor.
- Reads through FILE and descriptors must account for any future buffering,
  read-ahead and seek synchronization. Do not maintain two independent positions
  while claiming that both interfaces access one open stream.
- Endpoint references must close predictably on explicit close, normal exit and
  faults. No extra startup or adapter reference may keep a pipe writer alive
  after its final owner closes it.

Cross-process shared offsets and descriptor inheritance need separate decisions;
a process-local table alone cannot provide them. Preserve explicit capability
delegation at launch. Any necessary native support must solve a concrete semantic
need, rather than importing a descriptor syscall layer wholesale.

## Milestone boundary

The completed [shell streams and pipelines](../shell-streams.md) provide native
pipes and dedicated startup streams. Preserve their ownership and closure
behavior while making conventional descriptor I/O available in libc. Keep
fread_some as a useful extension; ports should not need to replace read with it.

Use a small upstream utility as the acceptance consumer rather than writing a
new Pyxis command with the same name. The proposed source is
[sbase cksum](https://git.suckless.org/sbase/file/cksum.c.html): it uses open,
read and close for input, with stdio for output. Probe its shared helpers and
headers too; the command source alone is not the dependency list. Pin the
selected revision and preserve its license through the normal ports workflow.

[sbase tee](https://git.suckless.org/sbase/file/tee.c.html) is the conditional
second consumer, exercising descriptor output and file creation. Its append and
signal options introduce separate policy questions. Do not expand this milestone
into signals or promise atomic append merely to claim a complete tee port.

This is not a full ISO C or POSIX conformance milestone, a libc replacement, or
a kernel descriptor ABI. Fork/exec, descriptor inheritance across processes,
dup/dup2, polling, nonblocking I/O, general signals, directory APIs and buffered
stdio remain outside the initial slice unless a separately agreed prerequisite
requires one. No speculative stubs or retroactive rewrite of every existing port.

## Focused tasks

- [ ] **1. Pin and probe the consumers; settle the descriptor contract.** Inventory
  the selected cksum and tee sources, transitive helpers and headers against the
  SDK. Propose the exact initial declarations, flags, errno mappings and omitted
  behavior. Decide descriptor allocation/growth, ownership, standard descriptors
  0/1/2, absent streams and FILE integration before implementation. Record whether
  tee fits, needs an explicitly restricted option set, or should be deferred.
  A compile probe is evidence gathering, not a new test framework.
- [ ] **2. Add descriptor ownership and standard-stream integration.** Implement
  the agreed process-local table and shared open state in libc. Standard FILE
  objects and descriptors must use one deliberate ownership/position model;
  avoid retaining an extra pipe writer. Settle fdopen/fileno semantics before
  exposing them; if deferred, document that boundary. Verify close, exit and
  absent-stream behavior while preserving existing native launch grants.
- [ ] **3. Add the conventional I/O slice.** Implement open/read/write/close and
  the headers/types/flags selected in task 1 over native file, console and pipe
  protocols. Read returns available progress without trying to fill the buffer;
  write reports actual progress, including short writes. Reuse capability path
  resolution and shared error handling rather than duplicating them in ports.
  Agree creation/truncation, permission-mode handling and unsupported flags
  before adding writable open; do not pretend Unix permissions are enforced.
  Seeking is included only if selected by the probe, and must reject pipes.
- [ ] **4. Port cksum as the first final consumer.** Build and package the pinned
  utility with its ordinary I/O calls intact. Keep adaptations to build/platform
  integration and agreed library gaps. Check named files, stdin, empty input,
  missing files, redirection and pipeline use on ordinary QEMU boots; compare
  checksum and byte count with a host implementation on identical bytes.
- [ ] **5. Port tee if the agreed scope fits.** Exercise stdin to stdout and named
  output files, short transfers, read-only grants and a downstream reader that
  closes early. Resolve append and interrupt-option behavior before coding: the
  existing separate SIZE/WRITE append is not atomic, and no-op signal handling
  is not support. Any restricted port needs explicit agreement and documented
  limitations; otherwise record the reason for deferring this optional consumer.
- [ ] **6. Complete the handoff.** Document the supported libc contract and port
  adaptations, carry missing semantics into technical debt, and move this WIP
  document into docs as an implementation reference. Record tee as implemented
  or explicitly deferred, not an unresolved requirement for completion.

Tasks are intended as focused PRs; split a task further if review warrants it.
Libc belongs in userland, recipes in ports, and matching SDK/ABI export and pins
in Pyxis. A missing native primitive is a decision checkpoint, not permission to
reshape the kernel. Validate code through normal builds and manual boots/debugger
inspection without adding test infrastructure.

The final manual workflow should include commands such as these once packaged:

```sh
cksum host://hello.c
cat host://hello.c | cksum
cksum < host://hello.c > home://checksum.txt
cat host://hello.c | tee home://copy.c | cksum
```

The last command depends on accepting the tee slice. Existing
[stdio behavior](../stdio.md), [append limitations](../technical-debt.md#non-atomic-stdio-append)
and [directory API gaps](../technical-debt.md#directory-apis-in-libpyxis) remain
explicit starting constraints, not promises this milestone resolves them all.
