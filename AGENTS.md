# Caelum development

- Follow BOOTSTRAP.md's milestone boundary: single x86_64 CPU, UEFI/Limine,
  serial diagnostics and memory initialization, then halt. No future subsystems.
- Use freestanding GNU C23, snake_case, two spaces, K&R control braces and
  function braces on their own line. Preserve upstream vendor formatting.
- Keep Limine in its adapter, hardware details in arch, and allocation policy
  in kernel/mm. Early initialization and fault reporting cannot use the heap.
- Physical addresses are not C pointers. Keep ownership, overflow checks and
  failure unwinding explicit. Maskable interrupts remain disabled.
- Pin dependencies and record local changes and licenses. Never use host libc.
- Validate with ordinary builds, QEMU boots and debugger inspection. Do not add
  tests, self-tests, fault injection, CI or boot/output automation.
- Make focused commits. Keep README short and practical; document interface
  invariants beside the code.
- Do not preserve backwards compatibility unless requested. This project is
  under active development with no external consumers. Prefer replacing obsolete
  interfaces, formats and implementations outright; update in-tree consumers
  together.
- Do not increment schema or version fields merely because the implementation
  changed. Introduce a new version only when multiple versions must intentionally
  coexist or migration is required.
- When a milestone is complete, rewrite its `docs/wip` document as concise
  documentation of the implemented behavior, interfaces and remaining limits,
  then move it into `docs` and update links. Remove completed task lists,
  superseded proposals and discussion history; Git preserves the original plan.
  Keep still-relevant deferred work in an appropriate WIP or technical-debt
  document rather than losing it during cleanup. Do not keep archive copies.
