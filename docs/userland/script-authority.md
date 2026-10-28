# Script launch authority

A script is not a sandbox. Use a restricted space or an explicit reduced launch
when reduced authority is required. The native Pyxis shell and standalone Lua
5.5/5.1 interpreters retain their launching context's available sources; child
manifests still select and attenuate those sources. Missing JIT, threads or
other authority stays missing. Privileged shell builtins work only when the
shell holds their authority.

## Code selection and sources

Interpreter continuation uses retained native READ FILE identity, never a name,
suffix or manifest ID. Trusted startup selects the interpreter files from its
held archive/package publication views. Rootless interpreters retain their own
held executable and supplied code records. A shebang's selected interpreter is
held across preparation; the main script consumes the handed FILE rather than
reopening its diagnostic name. Bundle-interpreter shebangs remain unsupported.

The SDK's `program_sources` owns only source capabilities and configured
`source.*` entries. `program_interpreters` owns code-only READ FILE records.
`program_runtime` is an explicit standalone-CLI context; linking Lua, luv or
libuv into an embedding such as Neovim or LuaLS does not initialize it.
Shell and Lua source preparation share the SDK selection and scope helpers.
The standalone interpreter initializes before startup chunks, `-l` or user code.

Sources keep actual rights and transport. Scoped launcher selection cannot hand
an ancestor launcher to a descendant. Ordinary native programs retain their
existing profiles; bundles receive declared resources. An explicitly reduced
libuv resource selection also bounds later declarations and continuation.
Neither inventory contains process observers, ancestor CONTROL, pending
transactions, publication controllers or retention factories. Private work leaves
do not become provisioners. Exported values, roots, cwd and standard streams use
the current invocation's existing snapshot, attenuation and ownership rules.

Lua 5.5 `pyxis.run` is the synchronous foreground route. Standalone luv spawn is
the asynchronous background route. `pyxis.job`, `pyxis.pipeline` and
`pyxis.commands` remain bounded leaf jobs: no implicit launcher, provisioner or
shebang expansion, and no fallback to another launch API after refusal.

## Lifetime

| Invocation | Foreground children | Background/service and successor children | Exit |
| --- | --- | --- | --- |
| Handed/shebang script FILE | Script's foreground subgroup | Original permitted launcher, outside that foreground subgroup | Stop and wait for the foreground subgroup |
| Explicit filename, shell `-c`, Lua `-e` or stdin script | Script's subgroup | Same positional script owner | Retire the owned subtree |
| Interactive shell or Lua REPL | Existing command routing | Existing background/successor routing | No script-wide group |

This is the same rule for shell and Lua, including normal return, unhandled
error and explicit `exit`/`os.exit`. A caught Lua error or unrelated GC does not
end the interpreter. Lua has no successor builtin; invoking a shell successor
uses the shell's existing handoff/wait policy. Local successor handoff ends its
script after accepted launch; Remote's waiting policy waits and propagates the
successor result. The existing rootless positional SESSION refusal remains.

Being outside a script's foreground subgroup does not promise survival of an
enclosing foreground group, capture, Remote connection or session owner. Closing
the final CONTROL on process death requests the same owned-subgroup stop.
Scope admission failure refuses before user code; cleanup failure is reported,
turns success into failure and preserves an explicit nonzero status. Pure Lua
without launch authority needs no scope and still runs.

See [shell runtime grants](shell.md#script-runtime-grants),
[Lua](lua.md#native-helpers), [bundle job contexts](program-bundles.md#private-job-contexts)
and [JIT authority](../interfaces/jit.md#authority-and-ownership).
The [qualification record](../development/experiments/lua-script-authority.md)
distinguishes QEMU evidence from inspected behavior and native limits.
