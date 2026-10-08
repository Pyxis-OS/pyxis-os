# Session configuration

`boot://session.pxe` is a native userspace launcher that evaluates
`boot://config/session.lua` and optional `boot://config/network.lua`, applies
terminal tab spacing, and hands off to `boot://shell.pxe`, or `boot://mux.pxe` when the local space
opts into the [multiplexer](multiplexer.md). The optional
`--configure-network` argument also applies shared NIC settings.
`--start-services` selects the trusted `boot://init-services` script after
configuration; it publishes HTTP with the configured resolver, then optionally
publishes HTTPS with packaged trust before starting the interactive shell.
Custom CA augmentation is selected by the trusted service command in
`init/services.sh`; it is not a session Lua setting. Without it, session starts the shell directly. The development
init script selects it:

```text
#!boot://shell.pxe
mount --optional --read-write host
namespace create
session boot://session.pxe --configure-network --start-services
```

`make run` uses this path without overrides. `INIT=/path/to/init.sh` can still
select a different startup script. A direct native init selection also works.
Interactive use must go through the shell's
`session boot://session.pxe` handoff so the launcher receives launch authority
and the parent stops using terminal input.

The installed `userspace/config/session.lua` supplies
`boot://config/session.lua` in the read-only boot archive. Edit that source file
and rebuild the image to change the system selection. It returns one table:

```lua
return {
  timezone = "Europe/Bucharest",
  terminal = { tab_width = 8 },
  environment = {
    -- vi: three-column tab stops, spaces for Tab and autoindent.
    EXINIT = "set ts=3 et ai",
  },
}
```

A missing file or setting defaults to UTC and eight-column tabs. An empty
timezone also selects UTC. Unknown keys, incorrect value types, NUL bytes in
names, non-integral tab widths or widths outside 1–32 are errors. Fields are
read directly from the returned tables; metatable lookups do not supply settings.
An existing invalid/unreadable file produces a diagnostic and no shell launch.
There is no fallback after a configuration error.

`environment` adds variables to the shell's startup environment, for the local
shell and for [remote terminal](remote-terminal.md) sessions alike. The shell
forwards its whole environment to the programs it starts, so the packaged
`EXINIT` gives [vi](vi.md) its default options. Each key is a variable name:
an ASCII letter or underscore, then letters, digits or underscores. Each value
is a string without NUL bytes. Other keys or values are configuration errors.
`TZ` and `DNS_SERVER` are refused, because `timezone` and the network
configuration set them. A configured variable replaces an inherited one of the
same name. There is no separate size limit; launch fails with a diagnostic if
the environment exceeds the 64 KiB startup metadata limit.

Values are strings for now. Environment variables are expected to carry
capabilities later, as in the
[capability-valued shell variables](../wip/userspace-scheme-providers.md#prepared-requests-and-shell-handoff)
idea, so string-only is not a permanent property of this table.

Timezone names follow libc's IANA-name syntax: slash-separated components of
ASCII letters, digits, underscore, hyphen and plus. UTC needs no file; other
names must identify a packaged file under `boot://share/zoneinfo` beginning with
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
[configuration policy and authority](../devices/networking.md#boot-configuration-and-use).
When requested, network application precedes the first terminal change. The
launcher preserves the startup environment except that it adds or replaces the
configured `environment` variables, replaces `TZ` with the selected timezone
and `DNS_SERVER` with the shared chosen resolver's numeric IPv4 address. Without
a published choice it uses profile DNS or `1.1.1.1`. Trusted setup publishes its
DNS choice even without IPv4 assignment; startup performs no DNS lookup. It forwards the
input/output, memory and launcher grants; app/home and optional
host roots with their actual queried grants;
the working-directory chain and display path; and optional display, clock,
[random](../devices/randomness.md) and keyboard resources, using the same rights as the shell's session handoff.
The optional service namespace is forwarded with its actual held rights, and
namespace-creation authority is forwarded explicitly through trusted session
handoff. Ordinary shell children receive namespace LOOKUP only.
The init shell explicitly delegates `net_config` through session handoff only.
The launcher applies network settings only with `--configure-network`, then
passes READ alone to the interactive shell or mux for later child DNS snapshots.
Non-owner provider scripts wait within the initial ten-second setup budget before
launching providers. The read-only init omits
the option, so starting its session does not reconfigure the shared NIC. DNS
and terminal/environment configuration are still read and applied per session.
It also does not forward mount authority, arbitrary named resources or the
interpreter's script handle.

The kernel copies launch metadata before returning. The launcher closes its
child observer without waiting or reading input; the shell owns its copied
grants and environment. Static setup then exits. A DHCP owner instead retains
its maintenance resources and stays alive independently of shell exit; see
[DHCP](../devices/dhcp.md). Tab spacing belongs to the shared TTY and survives
launcher exit. If applying spacing or shell launch fails, no shell starts and
DHCP cleanup attempts to clear its settings. Applied spacing and static network
settings have no rollback.
`make run INIT=build/userspace/shell.pxe` bypasses configured startup for recovery.
The packaged config remains present but is not evaluated. Live reload and
per-user/space configuration policy remain deferred.

The launcher lives in pyxis-userland and links against `liblua.a` and public
headers exported by the Lua port. These build inputs travel in the ports bundle
outside the boot payload; the SDK does not depend on Lua. See
[build bundles](../development/build-bundles.md) for the dependency order.
