# Script launch authority

Accepted by chronium, 2026-10-28. The exact held native Pyxis shell is implemented.
The Lua follow-up is assigned (chronium, 2026-10-28); its scope decisions below
are proposed, not accepted. No Lua implementation has started.

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

## Lua follow-up: inspected starting point

Give the two shipped Pyxis Lua interpreters the same launch-source inventory,
without granting sources to arbitrary programs embedding a Lua library. This is
an extension of the accepted script model: a script is not a sandbox; a restricted
space or explicit reduced launch provides that boundary. Existing source gates,
manifest attenuation, selected streams and held-code identity remain required.

Inspection at parent `8654265ca`, userland `797f95c3`, ports `47d0fa31`:

- Lua 5.5.1 (`ports/lua`) supplies synchronous `pyxis.run`, plus the separately
  bounded captured `pyxis.job`/`pyxis.pipeline` APIs. Its bundle has 20 requests,
  including optional threads and private client `job_work`, but no JIT request.
  `pyxis.run` can offer a held private JIT source to a declaring bundle already;
  it still has its own resource-selection list and never delegates provisioning.
- Lua 5.1.5 (`ports/lua51`) has LPeg and luv, **no native `pyxis` module**, and a
  five-request manifest without threads or JIT. Its launcher API is luv/libuv
  spawn. Do not promise a new `pyxis.run` API for 5.1 as part of source inheritance.
- The 5.5 CLI consumes the handed main-script FILE (`lua/patches/0002-pyxis-runtime.patch`).
  The 5.1 patch does not adapt its CLI script loader: ordinary upstream loading
  would reopen the descriptive filename. The follow-up must consume the handed
  FILE there too; `loadfile`/`dofile` and explicit filename invocation retain
  their own normal file semantics.
- Captured jobs require private work/clock/pipe/launcher context, but their
  **children** are leaf profiles: no delegated launcher, command search or work
  root, and no shebang expansion. Their explicit selectors, deadlines, output
  bounds and retained-job cleanup are a different contract from synchronous run.
- `pyxis.run` waits for a process, not its descendants. The current Lua guide
  explicitly permits a spawned process to outlive Lua; the Lua job registry has
  no per-job caller-death stop guarantee. Equal shell lifetime therefore needs
  an explicit decision, not just adding JIT to two manifests.

Evidence: `ports/lua/pyxis.c:292–505,550–618`, `ports/lua/jobs.c:247–284,532–619`,
`ports/lua/README.md` Owned native jobs, `ports/lua51/README.md` Running scripts,
the two manifests/CLI patches and [Lua launch behaviour](../userland/lua.md#native-helpers).
Some prose in the Lua guide predates the current 5.5 CLI; implementation will
refresh it against the actual port rather than preserve those stale claims.

## Lua decisions requested (round 1)

### 1. Which entry points qualify?

**Recommended default:** the two shipped standalone native interpreter images,
Lua 5.5.1 and Lua 5.1.5. Include a script FILE, `-e`, stdin and the REPL: deciding
by argv would suggest a security distinction that executing Lua code does not
provide. A shebang and an explicit invocation of the same held interpreter get
the same source treatment. A different path to that same FILE is an alias, not
a new authority decision.

A trusted launcher selects and retains the concrete interpreter FILEs from its
trusted installed/interpreter selection before admitting the call. The native
FILE comparison, not `lua`/`lua51` text, a suffix or self-asserted manifest ID,
identifies the continuation. A caller explicitly starting a reduced bundle/native
profile still bounds the resulting inventory; re-entry cannot recover omitted
sources. Missing identity never falls back to naming an arbitrary program as Lua.
No new kernel right or package manifest self-declaration of interpreter trust.
The implementation must retain selection across preparation, not relookup the
mutable command name after redirects. Embeddings such as Neovim and LuaLS keep
their application grants and gain neither an inventory nor a supervisor merely
by linking liblua or luv.

### 2. Which child-launch APIs change?

**Recommended default:** source-preserving interpreter continuation and ordinary
declared child launch through Lua 5.5 `pyxis.run` and Lua 5.1 luv spawn. Factor
common inventory selection into the shared launch path; do not grow independent
shell, Lua and libuv interpreter-extras lists. Offer existing sources to child
manifests, preserve ordinary native profiles, and give another exact supported
interpreter only the current context's sources. Update optional bundle requests
(including JIT/threads) without changing a space's source switches. The normal
libuv declaration/attenuation rule outside a Lua interpreter is unchanged.

Keep `pyxis.job`, `pyxis.pipeline` and `pyxis.commands` wrappers as their current
explicit captured **leaf** API in this task. An explicitly invoked Lua program
can still be a leaf job where its declarations permit; that does not authorize
it to launch descendants. No new script expansion, implicit provisioner, command
search or delegated launcher in captured jobs, and no silent retry via run when
a leaf request fails. Existing exact resource/root/environment selectors stay
bounds. Supporting nested launcher scripts inside those jobs would need its own
scoped-job ownership work; use `pyxis.run` or luv for launcher scripts now.

### 3. Who owns descendants of a Lua launcher script?

**Recommended default:** standalone Lua owns a private execution subtree for
its child launches, using existing native execution groups. All child launchers,
including luv asynchronous launches, are bound within it; the owner CONTROL is
private native interpreter state, not a Lua variable, environment root or child
grant. Synchronous run retains a command scope so its completion includes owned
descendant cleanup. Existing capture/jobs keep their own more restrictive owners.

Normal interpreter exit and an unhandled error stop and wait for this subtree;
termination/crash closes the final controller and requests the same stop. A
caught Lua error or collection of unrelated Lua objects does not stop the whole
interpreter. `os.exit` must not leave descendants outside ownership merely because
upstream bypasses `lua_close`. There is no implicit detached escape or ancestor
launcher. Existing group-depth and allocated-group admission bounds apply; no new limit.

Establish this ownership before the first child launch; pure Lua without launch
authority still runs. If required ownership cannot be established, fail that
launch before effects rather than run it unsupervised. Optional private job
provisioning uses fresh confined trees and enclosing-owner retirement, never
forwards a client leaf as a provisioner. Terminal and input eligibility remain
those of the invocation; no parent interrupt reader is transferred.

## Delivery after decisions

- [ ] Shared inventory/held-interpreter preparation and standalone interpreter
  lifetime integration; keep the shell contract and normal native profiles.
- [ ] Lua 5.5 and 5.1 CLI/manifest/native adapters, including 5.1 handed-script
  loading; no embedding-wide authority and no new 5.1 convenience module.
- [ ] Baseline before implementation, then QEMU JIT-on/off launches through both
  versions, shebang/explicit/nested/cross-interpreter paths, run/luv, background
  and shell capture. Check absent declarations/reduced sources, real renderer
  selection, ordinary exit/error/interrupt cleanup and unchanged leaf-job refusal.
- [ ] Publish userland/ports first, parent pins/docs next; dependencies remain
  fast-forward and existing compiler/container unchanged unless a build proves
  otherwise. Native scheduling stays with the orchestrator.

## You should know

The proposal deliberately changes the old "a child may outlive Lua" behaviour
for interpreter-owned launches. Lua GC is not a cancellation button. Captured
jobs remain leaf jobs, and an embedded Lua engine does not become a shell.
No JIT source is created when the launching context lacks it. Implementation and
qualification wait for the decisions above; no code or new API is delivered here.
