# Session configuration

`app://session.pxe` is a native userspace launcher that evaluates
`app://config/session.lua`, applies terminal tab spacing, and hands off to
`app://shell.pxe`. It accepts no arguments. The default init still starts the
shell directly; an explicit init script can select the launcher:

```text
#!app://shell.pxe
session app://session.pxe
```

Select that host file through `make run INIT=/path/to/init.sh`. A direct native
init selection also works. Interactive use must go through the shell's
`session app://session.pxe` handoff so the launcher receives launch authority
and the parent stops using terminal input.

The configuration is Lua text returning exactly one table:

```lua
return {
  timezone = "Europe/Bucharest",
  terminal = { tab_width = 4 },
}
```

A missing file or setting defaults to UTC and eight-column tabs. An empty
timezone also selects UTC. Unknown keys, incorrect value types, NUL bytes in
names, non-integral tab widths or widths outside 1–32 are errors. Fields are
read directly from the returned tables; metatable lookups do not supply settings.
An existing invalid/unreadable file produces a diagnostic and no shell launch.
There is no fallback after a configuration error.

Timezone names follow libc's IANA-name syntax: slash-separated components of
ASCII letters, digits, underscore, hyphen and plus. UTC needs no file; other
names must identify a packaged file under `app://share/zoneinfo` beginning with
the TZif signature. Libc still validates the complete zone data when a child
first uses local time. There is no second timezone parser in the launcher.

The evaluator opens only Lua's base library, with `load`, `loadfile`, `dofile`,
`print` and `warn` removed. There are no io/os, package, debug, module search or
native resource bindings. Configuration can use language arithmetic, loops,
functions and tables, plus the remaining base helpers. It is trusted boot
configuration: execution time and Lua heap usage have no separate budget.
All Lua initialization, loading and execution are protected against Lua errors;
the state and configuration stream are closed before launch.

Validation completes before the first terminal change. The launcher preserves
the startup environment except that it replaces any `TZ` entry with the selected
name. It forwards the input/output, memory and launcher grants; app/home roots;
the working-directory chain and display path; and optional display, clock and
keyboard resources, using the same rights as the shell's session handoff.
It does not forward arbitrary named resources or the interpreter's script handle.

The kernel copies launch metadata before returning. The launcher closes its
child observer and exits without waiting or reading input; the shell owns its
copied grants and environment. Tab spacing belongs to the shared TTY and survives
that exit. If applying spacing fails, no shell is launched. If shell launch
fails afterward, the applied spacing remains; there is no rollback or supervisor.
`INIT=` can bypass configured startup for recovery. Live reload, defaults packaged
in the image and switching the default init remain separate work.

The launcher lives in pyxis-userland and links against `liblua.a` and public
headers exported by the Lua port. These build inputs travel in the ports bundle
outside the boot payload; the SDK does not depend on Lua. See
[build bundles](build-bundles.md) for the dependency order.
