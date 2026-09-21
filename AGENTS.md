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
- Evolve project-owned interfaces together with their in-tree consumers. Do not
  preserve obsolete APIs, aliases, formats or fallback paths solely for backwards
  compatibility. Keep compatibility only for an explicit requirement or a
  concrete current consumer that cannot be updated; document that reason.
- Do not bump API, ABI or format versions automatically when their layout or
  behavior changes. Kernel, userspace and host tools are normally rebuilt
  together. Bump only for an explicit need to distinguish supported contracts,
  such as independently maintained consumers or stored data that must remain
  readable, and state the reason. These rules do not weaken input validation or
  override hardware, boot-protocol or third-party format requirements.
