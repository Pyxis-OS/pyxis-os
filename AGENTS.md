# Pyxis OS / Caelum development

## Scope and workflow

- BOOTSTRAP.md records the initial bring-up assignment, not today's milestone
  boundary. Pyxis now has SMP, interrupts, userspace, capabilities and drivers.
  Follow the user's current task and relevant docs; do not reapply obsolete
  single-CPU, interrupts-disabled or halt-after-initialization restrictions.
- Work through bounded milestones with focused, reviewable PRs. Unless the user
  requests otherwise, start from current main on a new branch, make focused
  commits, and open a PR. Do not merge it yourself. Keep dependent repository
  changes and submodule pins explicit.
- Discuss unresolved behavior, authority, lifetime and policy decisions before
  implementing the affected task. Use judgment for routine implementation details.
  A future idea or WIP note does not authorize code or placeholder interfaces.
- Preserve architecture and subsystem boundaries outside the assigned change.
  The goal is a useful OS shaped by concrete needs, not conformance to POSIX,
  difference for its own sake, or reproducing another OS. Familiar library/tool
  interfaces may adapt to the native ABI without dictating the kernel model.

## Implementation and ownership

- Use freestanding GNU C23, snake_case, two spaces, K&R control braces and
  function braces on their own line. Preserve upstream vendor formatting. Use
  names and logical separator lines for readability; keep comments sparse and
  reserve them for constraints, ownership and non-obvious ordering.
- Keep Limine in its adapter, hardware details in arch, and allocation policy
  in kernel/mm. Early initialization and fault reporting cannot use the heap.
  Do not link kernel or target userspace against host libc; host tools are native.
- Physical addresses are not C pointers. Keep ownership, overflow checks and
  failure unwinding explicit. Follow the current BSP allocation/VM-mutation and
  scheduler handoff rules in docs/smp.md and docs/memory.md; do not casually add
  allocator locks or remote mutations. Publish only after the required stack/VM
  handoff, and do not access references after relinquishing ownership.
- Pin dependencies and record local changes and licenses. Do not preserve
  backwards compatibility unless requested: replace obsolete interfaces, formats
  and implementations, updating in-tree consumers together. Do not increment
  versions merely because implementation changed; do so only when versions must
  intentionally coexist or migration is required.

## Validation and documentation

- Validate implementation with ordinary builds, QEMU boots and debugger
  inspection. Do not add tests, self-tests, fault injection, CI or boot/output
  automation unless explicitly requested. Use existing required build checks.
  Documentation-only changes need document/link review, not a gratuitous boot.
- Keep README short and practical; document interface invariants beside code.
  Mark proposals, selected decisions and implemented behavior distinctly.
- On milestone completion, rewrite its docs/wip document as concise documentation
  of implemented behavior, interfaces and limits, move it into docs and update
  links. Remove completed worklists and superseded discussion; Git preserves the
  history. Carry relevant deferred work into WIP or technical debt, without
  keeping duplicate archive copies.
