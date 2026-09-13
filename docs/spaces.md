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

## Prototype placement idea

The current prototype idea assigns each space exclusively to one core and keeps
its execution on that core. This makes CPU ownership straightforward and bounds
the number of simultaneously running spaces by the number of available cores.

The BSP would initially be reserved for the kernel tab, leaving the remaining
cores available for user spaces. The meaning of “core” still needs clarification
where SMT exposes multiple logical CPUs. Placement of the shared session
interface and other system services remains open; additional reservations could
further reduce the number of cores available to spaces.

Core assignment provides CPU separation. Memory containment still requires
protection and resource limits, and shared kernel resources still require
synchronization across cores. This placement idea does not settle the eventual
process model or commit later versions to one core per space.

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
