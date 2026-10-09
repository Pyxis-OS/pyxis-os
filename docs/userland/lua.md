# Lua in Pyxis

The normal image includes `bin://lua.pxe`, resolved as `lua` by the shell.
The port uses Lua 5.5.1, pinned to upstream commit
`7579fc9d7ed90240487251dfb69168f8e64e9294`, with the recorded upstream GC fix.
The MIT notice is installed at `boot://share/licenses/lua/lua.h`.
See the [port notes](../../ports/lua/README.md) for the source adaptations.
Host Lua used by build recipes and image manifests is independent of guest Lua.

## Interpreter

```text
lua -e 'print("Hello from Lua", 2 ^ 0.5)'
lua home://scripts/hello.lua Alice
lua
```

`-e` executes one text chunk. A filename runs a script, with `--` available for
filenames beginning with `-`. Paths use the inherited working directory or an
explicit Pyxis URI; the interpreter keeps the inherited working directory.
Module lookup follows the separate search path below. `arg[0]` is the supplied filename, positive indices and `...`
carry script arguments, and negative indices retain preceding interpreter
arguments. `loadfile` and `dofile` require explicit filenames and use those same
path rules. Script/chunk return values are discarded. File, syntax and runtime
errors return a nonzero process status; runtime errors include tracebacks.

With no arguments, Lua opens the libterm REPL. Expressions print their results,
globals survive across chunks, and locals belong to the current chunk. Lua's
parser determines when a statement needs another line, shown by the `>> `
prompt. Language errors return to the primary prompt. Ctrl+C discards the whole
pending chunk; Ctrl+D on an empty primary or continuation line exits. The parent shell handles Ctrl+C during an executing chunk by terminating
Lua; Lua has no cooperative signal hook.

The line editor supports insertion, deletion and cursor movement. Each input
line is limited to 1023 bytes and the visible terminal capacity. Input loss or
line-limit rejection discards the pending chunk. Multiline source grows on
Lua's heap; allocation and terminal failures exit nonzero. Prompts are fixed,
and history is not implemented.

## Libraries and runtime

The interpreter opens base, coroutine, table, string, UTF-8, `io`, `os` and
pure-Lua `package`, plus the native `pyxis` table. It keeps Lua's 64-bit integers
and double-precision numbers. Library initialization, loading and execution are
protected against Lua errors. Decimal point, calendar names and string collation
use the fixed C locale.

`require` checks preloaded modules, then Lua files. `LUA_PATH`, when present,
is the exact semicolon-separated path, including an empty value; versioned
variables and `;;` default expansion are not used. Otherwise the search is
`?.lua` and `?/init.lua` in the script's directory, followed by
`boot://share/lua/?.lua` and `boot://share/lua/?/init.lua`. Dots in module names
become `/`. For `-e` and the REPL, the first directory is the inherited cwd.
A relative script directory is resolved against that same cwd, without changing
it. Scripts may replace `package.path` or register their own preloaded modules.
`package.loaded`, `package.preload`, `package.searchers` and `package.searchpath`
retain normal Lua behavior; there are only preload and Lua-file searchers.

`io` operates on real libc file, pipe and console streams. Text and binary modes
are identical. Reads, writes, seeking, flush, close, iterators and one-byte
pushback follow [stdio](stdio.md). Rebinding `io.input` or `io.output` changes
Lua's default file only. Standard files retain upstream Lua's protection against
closing them through `io.close`; the native accessor also handles streams closed
at the C runtime level. `io.tmpfile()` creates an exclusive read/write file in
`tmp://` and immediately unlinks it; its open handle survives until close.
It requires creation, read/write and removal authority, plus clock/random grants.
`os.tmpname()` reserves an empty exclusive `tmp://lua-XXXXXX` file and returns its
name; the caller owns its removal. Reserved names are never automatically deleted.
A failure to unlink a temporary file fails the operation and can leave a named
file behind; abrupt death can also leave one between creation and removal.

`os.time()` returns wall-clock seconds; a calendar table argument is rejected.
`os.date` formats through real C-locale `strftime`, including UTC with `!`, date
tables with `*t`, numeric offsets and actual timezone abbreviations. Local time
uses the inherited `TZ` and packaged [TZif data](timezones.md). Clock or zone
failures raise errors. `os.difftime`, `os.getenv`, `os.remove`, `os.rename` and
`os.exit` are available. Native rename retains its no-replace policy.

The following remain absent: `file:setvbuf`, `io.popen`, `os.execute`, `os.clock`,
`os.setlocale`, `package.loadlib`, C-module searchers, debug and the full math
library. Stdin scripts and direct shebang handoffs are unsupported: the latter
must consume the launcher's existing script capability rather than reopen its
diagnostic name. No environment startup chunks or dynamic modules are loaded.

## Native helpers

`pyxis` is a global table and a preloaded module (`require "pyxis"`).

```lua
local entries = pyxis.dir("home://src")
local digest = pyxis.sha256("home://src/vm.c")
local status = pyxis.run{"tcc", "-c", "home://src/vm.c", "-o", "tmp://vm.o"}
```

`pyxis.run` takes a nonempty dense list of argument strings, without a shell,
word splitting or expansion. A bare program name resolves to `bin://NAME.pxe`,
then `boot://NAME.pxe` only if absent. A name containing `/` is an explicit path.
The child retains cwd, roots, environment and the allowed ordinary-command
resources, attenuated to the caller's held rights. The required `launcher` is
LAUNCH only, supplied to foreground commands only in an opted-in
[space](init.md#boot-configuration). Missing authority raises a Lua error.

The child inherits the live C `stdin`, `stdout` and `stderr`; closed streams are
omitted. Lua default-file rebinding does not redirect them. File cursors,
append state, pushback and buffered pipe bytes remain private to Lua; child file
streams start at offset zero. Named terminal input is separate from a piped
stdin and never carries interrupt rights. Launch, wait, fault and termination
raise errors; normal exit returns the actual signed integer status, including
nonzero values. There is no descendant supervision: shell Ctrl+C stops Lua,
and a process Lua launched may outlive it. Remote execution-group lifetime
still applies to group members.

`pyxis.dir(path)` returns an array of entries with `name` and `kind` (`file` or
`directory`), in native enumeration order. It does not add metadata, sorting or
a directory snapshot. `pyxis.sha256(path)` returns a lowercase 64-character
hex digest, streaming the file through Mbed TLS PSA SHA-256. Crypto initialization
requires clock and random grants; it does not provide fallback entropy. Path,
read and crypto failures raise Lua errors. Neither helper adds authority.

The native stack has the general [8 MiB eager backing](../kernel/program-loading.md) and an unmapped guard page;
Lua value stacks live on the heap. Parser, callback and pattern recursion retain
upstream limits, which do not prove that every combination fits the native stack.
Automatic stack growth and signal-driven interruption remain deferred.

## Embedding and session configuration

The ports build exports `liblua.a` and public headers separately from its boot
payload. The archive has the core, auxiliary library and selected libraries,
including io/os/package, without the CLI main or native `pyxis` bridge; consumers choose which libraries to open. It is built
against the selected SDK and carried in the [ports bundle](../development/build-bundles.md).
The SDK itself does not depend on Lua.

The first-party session launcher uses `userspace/libconfig` to evaluate
`boot://config/session.lua` and `boot://config/network.lua`. Default init selects
Bucharest, applies eight-column tabs and configures the optional QEMU NIC before
starting the shell. Settings and recovery policies are documented in
[session configuration](session-configuration.md) and [networking](../devices/networking.md).
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

[Later Lua work](../wip/later-os-directions.md#lua-follow-ups) records the
remaining library, stdin and shebang work.

## Validation

Manual QEMU checks used nested KVM, four CPUs, 512 MiB, OVMF, virtio-net/rng and
TCP remote terminals. A private Lua script required a sibling module and a boot
module, read/wrote C source, compiled it with `pyxis.run`, and returned compiler
statuses 0/1 plus the program's actual exit status 7. Directory entries and the
SHA-256 digest matched `ls` and `sha256sum`. Reserved and anonymous temporary
files worked. UTC, historical/future Bucharest abbreviations, wall time and ISO
year boundaries formatted correctly; table-form `os.time` failed explicitly.

Read-only and default Remote denied `pyxis.run`; a plain Read-only command had
no launcher. A separate Remote opt-in boot ran the complete workflow with its
group-bound LAUNCH-only grant. A C stream closed before descriptor-number reuse
remained NONE.
Piped `pyxis.run` retained C stdin after Lua default-input rebinding; bytes read
ahead remained in Lua rather than appearing in the child. Default-output
rebinding was also local. Read-only GDB inspection at `program_launch` found
LAUNCH-only launcher, READ-only named terminal input, pipe stdin, console
stdout/stderr, retained cwd and no keyboard/pointer grants for piped input.
Fault/termination error paths and temporary-failure unwinding were inspected,
not forced. No committed tests or boot automation were added.

Matched I/O observations used kernel `1d57caa` for both runs, baseline userland
`352cf14`/ports `cbab388`, and the Task 6 runtime sources before the integration
rebase. Each session ran `iobench read boot://share/iobench.bin --rounds 5` three
times, with its normal one warmup and 1 MiB verified fixture. Host FILE profiling
was off. These are nested-VM observations, not native-host performance claims.

| Measurement | Before: three medians (ms) | After: three medians (ms) | All sample ranges before / after (ms) |
| --- | --- | --- | --- |
| Payload read | 0.265, 0.273, 0.269 | 0.258, 0.258, 0.258 | 0.254–1.054 / 0.254–0.267 |
| Complete consumption | 0.341, 0.353, 0.346 | 0.333, 0.337, 0.333 | 0.330–1.361 / 0.329–0.343 |

All 30 measured passes completed with 257 payload reads and one EOF call,
without short reads or failures. The small median changes fit the observed VM
variation; they do not establish a speedup. Final integration rebases onto the
independently merged ACPI and startup-log work, so those kernel changes are not
attributed to this runtime comparison.
