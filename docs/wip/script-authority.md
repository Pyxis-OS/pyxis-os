# Script launch authority

Accepted by chronium, 2026-10-28. The exact held native Pyxis shell is implemented.
Equivalent source inheritance for the Pyxis Lua interpreter is a follow-up; it is not implemented or assigned by this task.

## Accepted contract

A directly invoked shell script behaves as commands typed at its launching
shell. Exact held-shell execution retains the shell's owned launch-source
inventory in foreground, pipeline, background and capture positions. Ordinary
native children keep their native profiles and manifest declarations. An explicit
native grant list or shell bundle remains an attenuation boundary, including the
shell bundle's optional JIT request. No launch creates absent authority or enables
a space's source-provisioning switches.

Native FILE identity and retained selection establish shell re-entry; a name,
suffix or manifest ID does not. A rootless held image selects code without
creating trust. Streams, exported environment snapshots, scoped launchers and
execution-group cleanup keep their existing owners. Inventory inheritance copies
launch sources, not process observers, ancestor CONTROL, pending transactions,
publication controllers or terminal interrupt ownership. Private job subtrees
require confirmed enclosing-owner retirement before cleanup. Existing admission
budgets and group bounds remain in force; background redirection stays unsupported.

See [shell runtime grants](../userland/shell.md#script-runtime-grants),
[bundle job contexts](../userland/program-bundles.md#private-job-contexts) and
[JIT authority](../interfaces/jit.md#authority-and-ownership).

## Delivery

- [x] Replace interpreter-specific extras with shared shell source selection;
  update the SDK consumers and shell bundle declarations together.
- [x] Qualify direct and nested scripts, explicit shell commands, pipeline,
  background and capture execution; preserve reduced/missing-source refusals,
  held-interpreter identity and owned group cleanup.
- [x] Publish [userland #416](https://git.internal/PyxisOS/pyxis-userland/pulls/416)
  before the parent pin/docs. No ports changes are needed: Lua/libuv retain the
  generic launch API and do not use the removed extras interfaces.

[Qualification and matched costs](../development/experiments/script-authority.md)
record the representative QEMU checks and their limits.

## Follow-up

Give the Pyxis Lua interpreter the same owned launch-source inventory principle,
with its interpreter identity, child declarations and cleanup ownership made
explicit. This requires a separate assignment and qualification; the current
Lua/native job and libuv declaration rules remain in force.
