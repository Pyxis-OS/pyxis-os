# Standard C libc over the native ABI

Status: agreed direction, with implementation scope and sequencing still open.
The shell-streams milestone is complete; this note does not authorize a libc
implementation milestone.

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

## Sequencing and open decisions

The completed [shell streams and pipelines](../shell-streams.md) provide the
implemented fread_some extension. It remains useful for native FILE consumers and
does not require descriptor support. Avoid permanently replacing every upstream
read call with Pyxis-specific code as the port collection grows.

Before an implementation milestone:

1. Pick representative simple programs and inventory their ISO C and additional
   API requirements. Agree the intended C library baseline and known omissions.
2. Select a small descriptor slice and settle ownership, shared positions,
   startup integration and FILE interoperability before implementing wrappers.
3. Add missing behavior in focused PRs, keeping reusable library work separate
   from port patches. Record unsupported semantics and native prerequisites.
4. Demonstrate the selected programs with minimal adaptations, then document
   the supported contract and remaining gaps.

Libc implementation belongs in userland; Pyxis exports matching ABI headers and
assembles the SDK. This direction does not select a replacement libc, introduce
a compatibility subsystem, or change the current stream ownership contract.
Existing [stdio behavior](../stdio.md) and
[directory API gaps](../technical-debt.md#directory-apis-in-libpyxis) remain the
starting point.
