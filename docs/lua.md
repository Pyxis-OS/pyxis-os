# Lua in Pyxis

The boot archive includes `app://lua.pxe`, resolved as `lua` by the shell.
The port uses Lua 5.5.1, pinned to upstream commit
`7579fc9d7ed90240487251dfb69168f8e64e9294`, with the recorded upstream GC fix.
The MIT notice is installed at `app://share/licenses/lua/lua.h`.
See the [port notes](../ports/lua/README.md) for the source adaptations.
Host Lua used by build recipes and image manifests is independent of guest Lua.

## Interpreter

```text
lua -e 'print("Hello from Lua", 2 ^ 0.5)'
lua home://scripts/hello.lua Alice
lua
```

`-e` executes one text chunk. A filename runs a script, with `--` available for
filenames beginning with `-`. Paths use the inherited working directory or an
explicit Pyxis URI; the interpreter does not change to the script directory or
search for modules. `arg[0]` is the supplied filename, positive indices and `...`
carry script arguments, and negative indices retain preceding interpreter
arguments. `loadfile` and `dofile` require explicit filenames and use those same
path rules. Script/chunk return values are discarded. File, syntax and runtime
errors return a nonzero process status; runtime errors include tracebacks.

With no arguments, Lua opens the libterm REPL. Expressions print their results,
globals survive across chunks, and locals belong to the current chunk. Lua's
parser determines when a statement needs another line, shown by the `>> `
prompt. Language errors return to the primary prompt. Ctrl+C discards the whole
pending chunk; Ctrl+D on an empty primary or continuation line exits. Neither
control interrupts an executing chunk.

The line editor supports insertion, deletion and cursor movement. Each input
line is limited to 1023 bytes and the visible terminal capacity. Input loss or
line-limit rejection discards the pending chunk. Multiline source grows on
Lua's heap; allocation and terminal failures exit nonzero. Prompts are fixed,
and history is not implemented.

## Libraries and runtime

The interpreter opens base, coroutine, table, string and UTF-8. It keeps Lua's
64-bit integers and double-precision numbers. Library initialization, loading
and execution are protected against Lua errors. Decimal point and string
collation use the fixed C-locale hooks; no locale or signal subsystem is implied.

There is no io/os, package/require, debug or full math library. Stdin scripts and
direct shebang handoffs are unsupported: the latter must eventually consume the
launcher's existing script capability rather than reopen its diagnostic name.
No environment startup chunks or native dynamic modules are loaded.

The native stack has the general 1 MiB eager backing and an unmapped guard page;
Lua value stacks live on the heap. Parser, callback and pattern recursion retain
upstream limits, which do not prove that every combination fits the native stack.
Automatic stack growth and signal-driven interruption remain deferred.

## Embedding and session configuration

The ports build exports `liblua.a` and public headers separately from its boot
payload. The archive has the core, auxiliary library and selected libraries,
without the CLI main; consumers choose which libraries to open. It is built
against the selected SDK and carried in the [ports bundle](build-bundles.md).
The SDK itself does not depend on Lua.

The first-party session launcher uses `userspace/libconfig` to evaluate
`app://config/session.lua` and `app://config/network.lua`. Default init selects
Bucharest, applies eight-column tabs and configures the optional QEMU NIC before
starting the shell. Settings and recovery policies are documented in
[session configuration](session-configuration.md) and [networking](networking.md).
Clock/calendar, terminal and native network operations remain usable without Lua.

`libconfig.a` is built with the application layer against the ports-provided Lua
library; it introduces no Lua dependency into the SDK, libc or libpyxis. Its
`config_read(path, decode, output)` creates a fresh state, loads text source in the
restricted base environment, requires exactly one result table and invokes the
consumer's decoder inside the protected call. The decoder receives the table at
index 1 and the caller's output pointer as lightuserdata at index 2. It returns
no Lua results. Source loading, evaluation, diagnostics and stream/state cleanup
are shared; allowed keys, types, defaults and application policy stay with the
consumer. Small helpers provide raw field access and allowed-key checks.

Missing files are returned distinctly without a diagnostic. Errors are reported
to stderr. Consumers own cleanup of partially populated native output even after
an error; no Lua state or borrowed Lua strings escape. These are trusted boot
files with no new execution-time or heap quota. There is no schema builder,
native resource binding, live reload or module search framework.

[Later Lua work](wip/later-os-directions.md#lua-follow-ups) includes broader
libraries and module policy.
