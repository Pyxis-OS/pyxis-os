# Session configuration

`boot://session.pxe` is a native userspace launcher that evaluates the session
and optional network configuration that boot init hands its space, applies
terminal tab spacing, and hands off to `boot://init-shell.pxe`, or to the `mux`
command, found by command search, when the local space opts into the
[multiplexer](multiplexer.md). The optional
`--configure-network` argument applies the declared per-interface settings and
starts independent DHCP maintenance under the trusted network owner.
`--start-services SCRIPT` runs the trusted services script SCRIPT after
configuration: `boot://init-services` in a full live image,
`system://config/init-services` on an installed system; it publishes HTTP with the configured resolver, then optionally
publishes HTTPS with packaged trust before starting the interactive shell.
Custom CA augmentation is selected by the trusted service command in
`init/services.pxs`; it is not a session Lua setting. Without it, session starts the shell directly. The development
init script selects it:

```text
#!boot://init-shell.pxe
mount --optional --read-write host
session boot://session.pxe --configure-network --start-services boot://init-services
```

`make run` uses this path without overrides. `INIT=/path/to/init.pxs` can still
select a different startup script. A direct native init selection also works.
Interactive use must go through the shell's
`session boot://session.pxe` handoff so the launcher receives launch authority
and the parent stops using terminal input.

Live boots read `boot://config/session.lua`, built from
`userspace/config/session.lua`. Installed boots read
[`system://config/session.lua`](init.md#installed-configuration), which Install
copies from the same source once; edit it there, and the change applies at the
next boot. It returns one table:

```lua
return {
  timezone = "Europe/Bucharest",
  terminal = { tab_width = 8, scrollback_bytes = 33554432 },
  retention = { value_limit_bytes = 67108864, session_limit_bytes = 536870912 },
  environment = {
    -- The terminal profile: exactly the sequences in docs/userland/terminal.md.
    TERM = "pyxis",
    -- vi: three-column tab stops, spaces for Tab and autoindent.
    EXINIT = "set ts=3 et ai",
  },
}
```

A missing file or setting defaults to UTC and eight-column tabs.
`terminal.scrollback_bytes` is the memory each multiplexer pane may hold as scrollback;
absent, the multiplexer takes one 512th of the allocator's capacity, at least 8 MiB. An empty
timezone also selects UTC. Unknown keys, incorrect value types, NUL bytes in
names, non-integral tab widths or widths outside 1–32 are errors. Fields are
read directly from the returned tables; metatable lookups do not supply settings.
An existing invalid/unreadable file produces a diagnostic and no shell launch.
There is no fallback after a configuration error.

`retention` sets positive integral byte limits for the session's
[retained output account](../interfaces/output.md). Omitted values default to
64 MiB/value and 512 MiB/session. They are independent limits, not ABI constants;
per-value need not be smaller than the aggregate. Charge logical FILE length,
including sparse holes, excluding allocator rounding; exact boundaries succeed.
Session setup consumes its private factory and creates one immutable-limits
account after configuration. Trusted successors reuse it rather than minting a
new budget on re-entry. Local/remote mux panes and Remote connections share the
root configured session's account. They receive account-only construction, never
factory/reset authority; ordinary commands inherit neither private entry.
Closing a shell does not remove charges held by exported values. Hard-limit
failure yields an immutable TOO_LARGE value with no successful prefix. This
kernel/SDK prerequisite does not itself enable shell `$(...)` syntax.

`environment` adds variables to the shell's startup environment, for the local
shell and for [remote terminal](remote-terminal.md) sessions alike. The shell
forwards its whole environment to the programs it starts, so the packaged
`TERM` names the [terminal profile](terminal.md#tty-output-controls) to every
program, in tabs, multiplexer panes and remote sessions, and `EXINIT` gives
[vi](vi.md) its default options. No packaged program reads `TERM` yet; remote
clients' typical host terminals support a superset of the profile. Each key is a variable name:
an ASCII letter or underscore, then letters, digits or underscores. Each value
is a string without NUL bytes. Other keys or values are configuration errors.
`TZ` and `DNS_SERVER` are refused, because `timezone` and the network
configuration set them. A configured variable replaces an inherited one of the
same name. There is no separate size limit; launch fails with a diagnostic if
the environment exceeds the 64 KiB startup metadata limit.

This Lua configuration field supplies TEXT entries. The native
[process environment](../interfaces/environment.md) also supports real capability
entries; it never encodes grants as strings. Capability-valued configuration and
[shell assignment](../wip/capabilities-and-providers.md#variables-and-the-environment)
remain separate work in the accepted capability-model milestone.

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
and selects `DNS_SERVER` from the registry's explicit DNS policy. It derives a
matching `dns_udp` grant inside its held UDP scope; without a permitted selected
path, it supplies neither resolver text nor transport. There is no profile-server
or public-resolver fallback. Existing processes retain their startup snapshots;
startup performs no DNS lookup. It forwards the
input/output, memory and launcher grants; app/home and optional
host roots with their actual queried grants;
the working-directory chain and display path; and optional display, clock,
[random](../devices/randomness.md) and keyboard resources, using the same rights as the shell's session handoff.
The selected exported environment entries are captured for each child.
Trusted session handoff explicitly delegates private service controllers;
ordinary shell children do not receive them.
Boot init prepares definitions, bindings and route/DNS/management selections
before starting spaces. The trusted init shell carries private network policy,
declared interface configuration facets and pinned DHCP factories only through
network-owner session handoff. The launcher applies address settings only with
`--configure-network`; ordinary interactive shells and mux panes receive registry
READ and scoped transports, including the selected resolver path. Non-owner
provider scripts wait within the initial ten-second setup budget before launching
providers. The read-only init omits the option and leaves address/DHCP maintenance
with the owner. DNS and terminal/environment snapshots still apply per session.
It also does not forward mount authority, arbitrary named resources or the
interpreter's script handle.

The kernel copies launch metadata before returning. The launcher closes its
child observer without waiting or reading input; the shell owns its copied
grants and environment. Static setup then exits. A DHCP owner instead retains
its maintenance resources and stays alive independently of shell exit; see
[DHCP](../devices/dhcp.md). Tab spacing belongs to the shared TTY and survives
launcher exit. If applying spacing or shell launch fails, no shell starts and
DHCP cleanup attempts to clear its owned interfaces. Applied spacing and static network
settings have no rollback.
`make run INIT=build/userspace/shell.pxe` bypasses configured startup for recovery.
The packaged config remains present but is not evaluated. Live reload and
per-user/space configuration policy remain deferred.

The launcher lives in pyxis-userland and links against `liblua.a` and public
headers exported by the Lua port. These build inputs travel in the ports bundle
outside the boot payload; the SDK does not depend on Lua. See
[build bundles](../development/build-bundles.md) for the dependency order.

## Program path

An optional top-level `path` beside `environment` is a dense ordered array of
nonempty directory-location strings. The selected live configuration is
`boot://config/session.lua`; installed boot uses `system://config/session.lua`.
`config validate` checks the list shape; resolution uses the session's held
roots/cwd when that session starts. Configuration grants no new directory rights.
A missing/inaccessible location warns visibly and is skipped, preserving the
remaining order; it does not refuse the session. `which` reports retained startup
skips, and `config validate` checks availability against its current held roots,
warning without making a well-formed configuration invalid. Syntax/types still
refuse. Resource/admission failures are reported separately.

```lua
path = { "home://tools", "bin://", "boot://" },
```

Absent preserves today's boot-supplied path. Empty explicitly supplies no entries.
User order is exact; defaults are not appended and no home directory is searched
implicitly. The session constructs and exports the read-only native list once;
ordinary panes and descendants inherit the selected path without reapplying the
configuration over runtime edits. `environment.path` is not a TEXT override.
The launcher retains its mux and remote-terminal images before applying user
lookup policy, so an empty or fully skipped path still permits session startup.
See [program lookup](command-search.md#configuration-and-runtime-edits) for runtime
`export path=(...)`, root meanings and lifetime.

Changes take effect at the next boot. Reloading/inheriting session settings through
shell startup, including tab width and path, remains proposed in the
[everyday gaps](../wip/everyday-gaps.md); it is not implemented here.
