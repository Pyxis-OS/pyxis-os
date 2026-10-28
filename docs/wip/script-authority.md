# Script launch authority

Accepted by chronium, 2026-10-28. The exact held native Pyxis shell is implemented.
The Lua follow-up decisions below are accepted (chronium, 2026-10-28), and
implementation is assigned. The Lua changes are not delivered yet.

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

## Accepted Lua decisions (chronium, 2026-10-28)

### 1. Which entry points qualify?

**Accepted:** the two shipped standalone native interpreter images,
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

**Accepted:** source-preserving interpreter continuation and ordinary
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

### 3. Lua follows the shell lifetime rule exactly

**Accepted with chronium's amendment:** no Lua-only stricter lifetime. Preserve
the shell's distinction between handed scripts and explicit positional scripts:

| Shell invocation | Foreground children | Background/service and successor children | Script exit |
| --- | --- | --- | --- |
| Handed `script` FILE, including a native shebang | The script's foreground subgroup; nested launchers stay bound beneath it | Launched outside that foreground subgroup through the original permitted launcher | Normal completion, error or `exit` stops and waits for the foreground subgroup only |
| Explicit `shell FILE` or `shell -c` | The positional script's subgroup | Confined under that same positional script owner rather than escaping it | Ending the script retires its owned subtree; waiting successor policy can delay exit |
| Interactive shell | Existing per-command foreground scope | Existing background or successor route | No script-wide foreground group |

Closing the script's final CONTROL on termination requests the same stop for its
owned subgroup. Being outside the script's foreground subgroup is **not** a
promise to outlive an enclosing owner: an outer foreground group, capture,
Remote disconnect or session retirement may still stop those processes. Local
successor handoff ends the script after accepted launch; the existing Remote
waiting policy waits and propagates the successor result before exit. The known
rootless positional SESSION refusal remains an existing limit, not a newly
promised route. These are the current shell routes in userland `797f95c3`:
`delegated_launcher`, `shell_script_group_open/close`, script startup in `main.c`
and SESSION selection/wait in `launch.c`.

Map Lua to the corresponding entry form: handed/shebang FILE to handed shell
script; explicit filename, `-e` or stdin script to explicit script/`-c`; REPL to
interactive shell. Lua 5.5 `pyxis.run` is foreground; luv's asynchronous spawn is
the background route. Lua has no new successor builtin in this task; an existing
shell successor launch keeps that shell's rule. A caught Lua error or unrelated
GC is not script exit. Normal return, unhandled error and **`os.exit`** use the
same applicable cleanup/routing as shell script completion, error and `exit`;
`os.exit` cannot bypass it merely because upstream bypasses `lua_close`.

Use the existing native group/launcher machinery and share its routing/cleanup
with the shell instead of adding a Lua-wide "kill every child" policy. Do not
copy ancestor CONTROL into Lua variables, environment roots or children. Where
the shell establishes a script group before executing code, standalone Lua does
the same when delegated launch authority is present; failed scope admission is
an explicit startup failure. Pure Lua without launch authority still runs.
Optional private job trees retain their existing enclosing-owner cleanup; client
leaves never become provisioners. Existing group-depth/allocated-group admission
bounds, stream selection and terminal/input ownership are unchanged.

## Lua delivery

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

A Lua launcher is not a sandbox. Its foreground/background/exit behaviour follows
the corresponding shell invocation, not a new Lua-wide descendant shutdown rule.
Captured jobs remain leaf jobs; embedded Lua gains no interpreter authority.
No JIT source is created when the launching context lacks it. Lua implementation
and qualification are assigned but not yet delivered.
