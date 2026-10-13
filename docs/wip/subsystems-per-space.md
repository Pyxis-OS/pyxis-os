# Subsystems per space

Status: **parked direction, owner idea 2026-10-13.** Not a milestone and not a
plan. It authorizes no code, placeholder API, configuration field or format
change. Revisit it before any of these lands: a second executable format or
loader, a userspace filesystem server that claims storage, session or space
configuration growing a per-space execution setting, or a kernel interface that
would encode one set of foreign semantics.

## The idea

Today a space runs one kind of program: native Pyxis programs on the Caelum
kernel. A *subsystem* (a personality) would let a space run programs written for
another environment, selected per space. The owner's example: an NT subsystem
with its own volume, running its own filesystem, such as NTFS inside a container
volume or on a claimed partition. Nothing here schedules that. The point is that
the design should not preclude it in, say, two years.

## Inspiration

- **The original NT environment subsystems** (Win32, POSIX, OS/2): several APIs
  over one native API, each served by user-mode code, with a server process
  (csrss for Win32) holding the shared state.
- **Wine:** loads PE executables and implements the Win32 API in userspace over
  the host's own primitives; the host kernel is unchanged.
- **WSL1 pico processes, as the contrast:** the kernel itself dispatches foreign
  (Linux) system calls. That ties the kernel to one foreign API's semantics.

## Recommended shape

A **userspace personality**:

- Foreign executables are loaded by a subsystem loader selected per space, not by
  the kernel image loader.
- The foreign API is a set of libraries plus subsystem server processes, talking
  over native Pyxis IPC (endpoints and capabilities).
- The kernel stays single-personality. This follows the project rule to port
  through native interfaces and not to bend the kernel around one application.
  Kernel-level foreign syscall dispatch is explicitly not the direction.

## How existing Pyxis pieces fit

- **Spaces and their supervisor.** A space is already the environment boundary,
  and [Continuum](spaces.md#asterism), the proposed per-space supervisor, would
  own what runs inside it, including a subsystem's loader and servers.
- **URI providers.** An NT drive namespace could be a provider under the
  [userspace services](../interfaces/userspace-services.md) and
  [scheme provider](userspace-scheme-providers.md) model, bound by name into the
  space's [namespace](../interfaces/namespaces.md).
- **Volume claims.** The [FAT32 volumes](fat32-volumes.md) work introduces a
  physical-extent registry with explicit mount authority. A subsystem filesystem
  server would claim an extent the way any mount does, and be excluded from
  conflicting users the same way.
- **Declared grants.** [Program bundles](../userland/program-bundles.md) request
  named grants by declaration. A subsystem would receive its authority the same
  way, never through a wrapper that holds more than the program declared.
- **Related directions.** [Capability-valued environment](capability-environment.md)
  (how a loader hands a foreign program authority it can name) and
  [nested execution groups](nested-execution-groups.md) (contained subtrees for
  foreign process trees).

## Invariants current work should not break

Reviewers can use these as a lens; they add no work now.

- Executable loading goes through the launcher and loader path, so a per-space
  loader could later handle formats other than P1F.
- No kernel ABI assumes a single personality, and none encodes POSIX or NT
  semantics in the kernel.
- Volumes and extents stay claimable by userspace filesystem servers, not only by
  kernel filesystems.
- Space configuration stays extensible, so a personality selection could be added
  later. No field is added now.

## Open questions

For the owner later; none has an answer here.

- Where does a subsystem server live in the space hierarchy: inside each space,
  shared between spaces, or under the supervisor?
- How do foreign file handles map to native objects, and what happens to semantics
  with no native counterpart?
- How does a foreign process interact with native IPC and grants, and what can it
  never obtain?
- What are the licensing terms of any reused code (for example from Wine or an
  NTFS implementation)?
