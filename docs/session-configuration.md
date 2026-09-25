# Session configuration

`app://session.pxe` is a native userspace launcher that evaluates
`app://config/session.lua` and optional `app://config/network.lua`, applies
terminal tab spacing, and hands off to `app://shell.pxe`. The optional
`--configure-network` argument also applies shared NIC settings. The development
init script selects it:

```text
#!app://shell.pxe
mount --optional --read-write host
session app://session.pxe --configure-network
```

`make run` uses this path without overrides. `INIT=/path/to/init.sh` can still
select a different startup script. A direct native init selection also works.
Interactive use must go through the shell's
`session app://session.pxe` handoff so the launcher receives launch authority
and the parent stops using terminal input.

The installed `userspace/config/session.lua` supplies
`app://config/session.lua` in the read-only boot archive. Edit that source file
and rebuild the image to change the system selection. It returns one table:

```lua
return {
  timezone = "Europe/Bucharest",
  terminal = { tab_width = 8 },
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

The shared `userspace/libconfig` evaluator opens only Lua's base library, with `load`, `loadfile`, `dofile`,
`print` and `warn` removed. There are no io/os, package, debug, module search or
native resource bindings. Configuration can use language arithmetic, loops,
functions and tables, plus the remaining base helpers. It is trusted boot
configuration: execution time and Lua heap usage have no separate budget.
All Lua initialization, loading and execution are protected against Lua errors;
the state and configuration stream are closed before launch.

Both files are decoded before applying settings. The network consumer owns its
[configuration policy and authority](networking.md#boot-configuration-and-use).
When requested, network application precedes the first terminal change. The
launcher preserves the startup environment except that it replaces `TZ` with the selected timezone
and `DNS_SERVER` with the selected resolver's numeric IPv4 address (default
`1.1.1.1`). DNS selection applies even without a NIC or configuration authority;
it does not perform a lookup or change kernel settings. It forwards the
input/output, memory and launcher grants; app/home and optional
host roots with their actual queried grants;
the working-directory chain and display path; and optional display, clock,
[random](randomness.md) and keyboard resources, using the same rights as the shell's session handoff.
The init shell explicitly delegates `net_config` through session handoff only.
The launcher applies network settings only with `--configure-network`, then
leaves that authority out of the interactive shell. The read-only init omits
the option, so starting its session does not reconfigure the shared NIC. DNS
and terminal/environment configuration are still read and applied per session.
It also does not forward mount authority, arbitrary named resources or the
interpreter's script handle.

The kernel copies launch metadata before returning. The launcher closes its
child observer and exits without waiting or reading input; the shell owns its
copied grants and environment. Tab spacing belongs to the shared TTY and survives
that exit. If applying spacing fails, no shell is launched; any already-applied network
settings remain. If shell launch
fails afterward, the applied spacing remains; there is no rollback or supervisor.
`make run INIT=build/userspace/shell.pxe` bypasses configured startup for recovery.
The packaged config remains present but is not evaluated. Live reload and
per-user/space configuration policy remain deferred.

The launcher lives in pyxis-userland and links against `liblua.a` and public
headers exported by the Lua port. These build inputs travel in the ports bundle
outside the boot payload; the SDK does not depend on Lua. See
[build bundles](build-bundles.md) for the dependency order.
