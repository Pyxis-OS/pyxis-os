# Spaces: working design draft

Status: working draft for discussion. This is not an approved specification or
an implementation plan. Names, interfaces and policies remain open.

## Idea

Pyxis OS could organize interactive environments into **spaces**: kernel-managed
execution and resource domains presented through a persistent tabbed session
interface. “Spaces” is a working name; earlier discussions used “surfaces.”

A space can contain multiple processes, each with its own virtual address space.
The environment includes execution, resources and access boundaries as well as
its presentation on screen.

POSIX compatibility is not a requirement.

## Session experience

The shared session interface could initially be a text display with a tab bar:

```text
[Caelum   ] [shell    ] [editor   ] [desktop A] [desktop B] [+]
```

The agreed [runtime SMP milestone](scheduling-and-threads.md#scrolling-space-bar)
adds a scrolling viewport over fixed-width tabs. Super+Left/Right changes selection
and reveals the next neighbour in that direction when possible; noninteractive
chevrons indicate hidden spaces at each edge. These navigation choices do not
implement the future dynamic creation flow below.

Starting a new space begins with an empty tab. The user enters the program they
want to run in that tab and presses Enter to launch it, without requiring an
intermediate shell. Tabs have a fixed width to simplify the initial interface.

A shell, editor, game or complete desktop environment is an ordinary application
within a space. Multiple desktop instances can coexist. A desktop can manage
its own windows and child applications while the shared tab and switching
interface remains independent of it.

“Persistent” means the shared session interface outlives individual environments.
Persistence across reboot has not been defined. A stopped or failed space could
leave its tab visible for inspection or restart.

## Pinned kernel tab

The first tab would be pinned and reserved for the kernel, probably titled
**Caelum**. Initially it would show the live kernel log. The proposed first
placement is on the bootstrap processor (BSP), with no direct userspace access
to or control over the tab. This kernel-owned view has a different role from
the application environments described above.

A later extension could add an interactive kernel monitor. A fixed input box
at the bottom would accept commands while the log above continues updating
independently. Command responses would be written into that same log. Possible
commands include inspecting kernel state and spawning workloads; the command
set and authority rules have not been designed.

Log delivery from other cores, input handling and the relationship between this
tab and the shared session interface remain open implementation questions.

## Kernel and supervisor responsibilities

Caelum would own space membership, lifetime, resource accounting and access
boundaries. Descendant processes remain within their space's containment boundary.

Each space would start a userspace init/supervisor, receiving the target
application, arguments and initial resources. That supervisor launches and
manages the environment. It does not imply Unix PID 1 semantics or system-wide
privileges.

A space could receive a scoped view of storage, services and named objects,
with explicit sharing where appropriate. The namespace and authority model,
including the relationship between kernel ownership and supervisor policy,
remains undecided.

The [filesystem and namespace draft](vfs.md) explores a shared system base,
per-space overlays, shared writable storage and URI-based namespace views.
The [process and capability ABI draft](../interfaces/processes.md) works through explicit
resource grants and process lifetime using a console and boot-archive file.

## Named endpoints

Spaces may advertise named endpoints for discovery by other spaces. An
advertised endpoint may refer to kernel IPC, an internal network service, or
another transport. The space namespace model defines naming, visibility, access
policy, and resource-accounting responsibilities. Discovery does not itself
grant access; each transport must preserve the applicable authority and
accounting boundaries.

## Execution placement

The original prototype associated one space with each CPU. The agreed
[runtime SMP milestone](scheduling-and-threads.md) replaces that association with
independent space identity and CPU eligibility. Trusted init can request affinity
within its launcher's permitted CPU set before session handoff. Workload spaces
share all online CPUs by default, including the BSP, or may be restricted to a
CPU set. Affinity is neither an exclusive reservation nor a resource budget.
Caelum remains the restricted kernel log space without owning CPU 0.

This changes placement, not the requirement for memory containment, resource
accounting and synchronization. The detailed SMP task list is authoritative for
that transition; the wider space lifecycle and containment ideas remain future work.

## Failure and resource isolation

A motivating case is a desktop running QEMU whose guest or workload consumes
excessive memory. That environment should be containable or terminable while
other spaces and the shared session interface remain usable.

Accounting must include kernel allocations made on behalf of a space, alongside
its userspace allocations. Shared infrastructure needs capacity that a
disposable space cannot exhaust. Naming and separate page tables alone do not
provide that guarantee.

The treatment of shared resources, limit enforcement, exhaustion, cleanup and
supervisor failure has not been specified.

## Open questions

- What objects represent spaces, processes and resource ownership?
- How are authority, namespaces and explicit sharing expressed?
- What IPC mechanism connects spaces, and how are shared costs attributed?
- What display and input interfaces connect applications to the shared session?
- How does the pinned kernel tab receive logs and input across cores?
- Where do the rest of the session interface and shared services execute?
- What happens when a space exceeds a limit or its supervisor fails?
- How do stopping, termination, inspection and restart affect resources and tabs?
- What, if anything, persists across reboot?

IPC is undecided. No protocol or transport is selected by this draft.

## Scope boundary

A potential next step to discuss is starting additional cores and launching
workloads on them. That work needs its own scope, including per-CPU execution
state and synchronization of shared kernel resources; this draft does not
authorize starting it.

This document records a design direction. It does not authorize implementing
spaces, adding placeholder APIs or structures, or restructuring the current
kernel in anticipation of them. Development continues through separately
assigned work.
