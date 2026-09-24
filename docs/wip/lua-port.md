# Lua port milestones

Status: expression and script execution are implemented; the REPL is next.
Configuration is the first system integration: choose the default timezone and
terminal tab width. Host Lua for build recipes and image manifests remains independent of
this guest port. Libraries consume the [SDK](../sdk-and-repositories.md) and
[port recipe setup](../ports.md).

Clock reads, calendar conversion and timezone conversion must work without Lua.
Audit the port before implementation and discuss newly discovered runtime gaps;
this milestone does not authorize unrelated kernel or libc expansion.

The general [userspace stack](../userspace.md) now has 1 MiB of eager backing
and a reserved guard page. Lua's heap value stack does not eliminate native C
recursion in parsing and callbacks; review those paths against this budget
without silently lowering Lua's recursion limits. Automatic growth is deferred.

## 1. Pin Lua and define the runtime slice

The expression interpreter uses Lua 5.5.1 from the
[official Git mirror](https://github.com/lua/lua), pinned to commit
`7579fc9d7ed90240487251dfb69168f8e64e9294` (`v5.5.1`). Its C sources and headers
match the release except that the mirror does not contain `luac.c`; the separate
bytecode compiler is outside this slice. Preserve the MIT license in `lua.h`.
The port carries upstream's [negative-shift GC fix](https://github.com/lua/lua/commit/0b29f408433e92953cc72b1d3e06c7ac8139e439)
as a recorded production-code patch.

The initial compile audit identified the runtime requirements below, now
implemented in the SDK. Lua keeps its normal 64-bit integers and double-precision
numbers. The normal ports build includes the executable and its MIT notice;
see the [recipe notes](../../ports/lua/README.md) for source adaptations.

Prerequisites and focused PR tasks:

- [x] General 1 MiB userspace stack and reserved guard page. Review Lua's native
  recursive paths when integrating the port; individual frame sizes do not
  establish a worst-case bound.
- [x] Add libc `memchr`, `strspn`, `strpbrk`, and the missing ASCII classifiers
  `isalnum`, `isalpha`, `iscntrl`, `isgraph`, `islower`, `ispunct`, `isupper`,
  `isxdigit`. Preserve the existing unsigned-byte/EOF argument contract.
- [x] Add core double-precision `floor`, `fmod`, `pow`, `frexp` and `ldexp` using
  the pinned musl subset and its required internal helpers/tables. Lua's core
  needs these even without the standard `math` library; the full library is
  deferred.
- [x] Add the pinned recipe, license and ordered adaptations, then the expression
  execution slice below. Record fixed-C-locale decimal point and byte collation
  through Lua's existing hooks; no locale subsystem or signal stubs.
- [x] Add explicit-filename script loading with inherited cwd/URI lookup,
  `arg`/`...`, and restored `loadfile`/`dofile`.
- [ ] Add the libterm REPL with the exit/cancellation behavior below.

The interpreter registers base, coroutine, table, string and UTF-8, including
explicit-filename `loadfile` and `dofile`. Broad `io`/`os`, native dynamic modules
and the full math library are not prerequisites. The `io`/`os`
audit found additional needs including pushback, temporary files, stream-buffer
control, process CPU time and calendar formatting/conversion. Do not substitute
wall time for CPU time or introduce successful stubs for missing operations.

The auxiliary script loader uses a port-owned 512-byte buffer and opens files
in binary mode initially, avoiding the unnecessary `freopen` cycle on Pyxis.
Pure-Lua `require` still needs an explicit module search-path policy.

The pin, library selection and runtime prerequisites are complete. This does
not authorize unrelated libc or kernel expansion.

## 2. Run Lua code in Pyxis

Implemented as `app://lua.pxe`, resolved as `lua` by the shell:

```text
lua -e 'print("Hello from Lua", 2 ^ 0.5)'
```

The small native driver accepts exactly one `-e` text chunk. It opens the five
selected libraries and protects initialization, compilation and execution.
Success exits zero; usage and execution errors exit one. Runtime errors include
tracebacks. No arguments prints usage. Chunk return values are discarded.
The driver also supports explicit scripts as described below. There is no REPL
fallback or environment startup hook.

The full image build and manual boot/debugger inspection cover real Lua
execution and error reporting. This does not imply system-wide configuration,
dynamic modules or complete standard-library support.

## 3. Scripts and an interactive REPL

Script execution is implemented as `lua file.lua [args...]`, with `--` to
terminate option parsing for filenames beginning with `-`. Paths resolve through
the inherited working directory or explicit Pyxis URIs; the interpreter does not
change to the script's directory or search for modules. `arg[0]` is the supplied
filename, positive indices and `...` carry script arguments, and negative indices
retain preceding interpreter arguments. The base library's `loadfile` and
`dofile` require explicit filenames and preserve Lua's usual return/error rules.
File, syntax and runtime errors return a nonzero process status.

Stdin scripts and direct shebang launches are deferred. The latter needs to
consume the launcher's already-open `script` capability rather than reopen a
diagnostic name; the current driver rejects that handoff explicitly.

The next focused PR adds the libterm REPL. The agreed controls are Ctrl+D on an
empty line to exit, and Ctrl+C to discard the whole pending multiline input and
return to the primary prompt. Cancellation is not EOF. Current libterm needs a
reusable EOF result; update its callers deliberately. History is not part of this
slice. Preserve Lua's multiline compilation/error handling and keep the selected
libraries and filesystem policy above.

Lua's CLI normally uses SIGINT, which Pyxis does not provide. Line cancellation
does not interrupt an executing CPU-bound script. Signals, module lookup and
additional standard libraries remain separate work.

Completion: script execution is done; an interactive REPL with useful language
errors and the agreed exit/cancel controls remains.

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
