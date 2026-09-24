# Lua port milestones

Status: planned after [zoneinfo-backed local time](timezones.md). Configuration
is the first system integration: choose the default timezone and terminal tab
width. Host Lua for build recipes and image manifests remains independent of
this guest port. Libraries consume the [SDK](../sdk-and-repositories.md) and
[port recipe setup](../ports.md).

Clock reads, calendar conversion and timezone conversion must work without Lua.
Audit the port before implementation and discuss newly discovered runtime gaps;
this milestone does not authorize unrelated kernel or libc expansion.

## 1. Pin Lua and define the runtime slice

Choose an upstream revision, preserve its license and review its actual build
requirements against Pyxis. Record the required C library, numeric, file and
terminal facilities, and the Lua libraries exposed in the first interpreter.

Completion: a pinned source recipe/metadata and a concrete list of missing
runtime facilities split into focused PR tasks. This is an audit milestone, not
an instruction to implement an entire libc or change kernel interfaces to POSIX.

Userspace already supports x87/SSE2. Review the selected Lua version's numeric
requirements against that support. Do not quietly change Lua's numeric behavior
to get a build. Current stdio formatting lacks floating point; review conversion, math,
error handling and other requirements against the selected source.

## 2. Run Lua code in Pyxis

Implement the agreed minimal runtime requirements in focused changes, then build
the interpreter through the port recipe and SDK. Settle the exact execution
interface before starting, for example running a supplied expression with output
and an observable exit status. State which standard libraries are available.

Completion: ordinary builds and interactive boots demonstrate real Lua code
executing, printing a result and reporting an error. No system-wide configuration
integration, dynamic modules or complete standard-library promise is implied.

## 3. Scripts and an interactive REPL

Use native filesystem authority through libc/native adaptations to load scripts,
and use the terminal facilities for interactive input and output. Settle the
initial file/module lookup rules and supported libraries before implementation.

Completion: launch a script from the shell and use an interactive Lua REPL, with
useful file and language error reporting. Expand only the runtime facilities
needed for that slice. These may be separate PRs.

## 4. Init/session configuration

Add a userspace configuration evaluator that returns a table, validates its
settings and supplies ordinary values to consumers. Applications need not each
embed Lua, and the kernel receives explicit operations rather than Lua code.
Start with the default timezone and terminal tab width, for example:

```lua
return {
  timezone = "Europe/Bucharest",
  terminal = { tab_width = 8 },
}
```

This explicitly selects Bucharest; absent `TZ` defaults to UTC. Configuration
will pass the selected IANA name through the session environment.

Settle the configuration file location, startup ordering, available Lua
libraries/capabilities and error/default behavior before implementation. The
evaluator need not receive the authority of an interactive interpreter. Define
how init passes the selected timezone to children and applies terminal settings;
no settings API or reload machinery is implied to exist today.

Completion: boot consumes the config and applies its timezone/tab settings,
while direct clock and calendar operations still work independently of Lua.

## Deferred

Dynamic loading, broad standard-library coverage, live configuration reload,
per-user/space configuration policy, additional ports and Neovim remain separate
work. Do not couple the first Lua milestone to virtio-fs: the existing initrd
and RAM filesystem are sufficient places to supply scripts.
