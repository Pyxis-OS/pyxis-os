# Neovim

The [recipe](../../ports/neovim/README.md) packages Neovim 0.12.5 as a native
terminal bundle. From a local shell with launcher and pipe authority:

```text
nvim file.c
```

Bare `nvim` selects `nvim.pxb` from the `nvim` package, without a catalog.
`pkg://nvim/nvim.pxb file.c` explicitly selects the same default entry. Port notes and source provenance live under `app/metadata/`; application
and linked-dependency notices live under `app/metadata/licenses/`; read them at
`pkg://nvim/nvim.pxb/app/metadata/`. Ordinary commands and
bundles do not inherit authority the shell lacks. The bundle needs memory,
clock READ/SLEEP, launcher and pipe CREATE, plus a console and the directory
roots for the files being edited. Its read-only `nvim_runtime://` resource
contains runtime Vimscript, core Lua modules, syntax files and help tags.
Configuration loads from `home://.config/nvim/init.lua` or `init.vim` after
`nvim_runtime://sysinit.vim`. XDG overrides remain explicit environment settings.

The first profile uses sixteen terminal colours, reverse and alternate screen.
The `pyxis` scheme highlights C with Comment, Statement, Type, PreProc, Constant
and String groups, all `cterm=NONE`. Startup sets `NVIM_NOTTYFAST=1`, removes
`COLORTERM` and defaults to `notermguicolors`; it sends no unsupported terminal
probes. The terminals' newer RGB and style support is available for a separate
profile follow-up. Input retains a native RAW passthrough grant; resize observes
console geometry generations, including mux panes. Exit restores the shell
screen and releases input ownership.

The bundle statically links Neovim's seven bundled Tree-sitter grammars: C,
Lua, Vim, Vimdoc, query, Markdown and Markdown inline. `language.add()` registers
real language objects with ABI validation, without module loading. Tree-sitter
highlighting is on by default for every bundled grammar: upstream ftplugins
start it for Lua, Markdown, help and query, and `sysinit.vim` starts it for C
and Vim from one `FileType` autocommand in the `pyxis_treesitter` group, which
`init.lua` can remove with
`vim.api.nvim_clear_autocmds({ group = 'pyxis_treesitter' })`. Markdown inline
is the grammar Markdown injects. `sysinit.vim` also sets `g:c_syntax_for_h`, so
`.h` files are `c`. An unavailable language returns no parser from
`language.add()`/`get_parser()`; highlighting start quietly preserves/restores
legacy syntax. Explicit dynamic parser paths remain unsupported. The exact
pins and MIT/Apache-2.0 notices are in the recipe and staged provenance.

`:cd` uses the [shared working path](process-state.md), affecting the editor
server and its future child snapshots, not the shell. Filename expansion keeps
native schemes; a descriptive cwd is never accepted as a proved `realpath`.
The TUI launches only its same-image internal `--embed` server with explicit
pipe CREATE. It does not forward its private `app://` root; the runtime grant
survives independently. Redirected stdin requiring fd 3 is refused before launch.

## External jobs and shell commands

`jobstart({...})`, `vim.system({...})` and libuv spawns start native children
directly, with stdin, stdout and stderr pipes. A bare name uses the shell's
held command list, and an explicit `.pxb` path starts a complete bundle.
`executable()` and `exepath()` probe the same resolver, without promising a later
selection. Spawn uses its one retained selection. Absent PATH uses the list;
an unsupported textual PATH refuses rather than adding directory authority. A child receives only authority the editor holds:
plain programs get the inherited memory, clock, launcher, exported grants and
roots; a bundle gets what its manifest declares from memory, clock, threads,
random, launcher, pipe, system information and network grants. The console and
input devices stay with the editor. `nvim.pxb` declares optional threads and
random to pass on, which hosted Clang requires.

String commands (`:!`, `:r !`, `system()`, `systemlist()`, `:make`, `:grep`)
run one line through [`shell -c`](shell.md#command-lines): `sysinit.vim` sets
`shell=shell`, `shellcmdflag=-c`, `shellpipe=>%s 2>&1`, `shellredir=>%s 2>&1`
and `noshelltemp`. `:make` and redirected shell commands collect stdout and stderr
through one byte sink; filters use pipes. Exact-shell scripts run with the
rootless shell's declared runtime grants, so a `build.pxs` can launch a
threads-requiring compiler bundle without acquiring interpreter trust. For example, Clang
errors land in the quickfix list with:

```vim
set makeprg=clang\ -c\ %\ -o\ tmp://out.o
make
```

`v:shell_error` is the line's status: explicit `exit [N]`, a failed foreground
command's positive status, or 1 for other failure. `jobstop()` and Ctrl+C during `:!` terminate the child at once;
the shell's script group takes its commands with it. A terminated job reports
143 and a faulted one 139, the codes Neovim reports for them elsewhere, since a
negative status would mean a still-running detached job. Jobs end when the
editor exits. `pty` and `detach` options are refused. Temporary files live in
`tmp://` (`$TMPDIR` first when set).

## File URIs

Language servers exchange file URIs. `vim.uri` gives a native path `ROOT://rest`
the URI `file:///ROOT/rest`, percent-encoding each segment, and maps it back:
`home://test/src/main.c` is `file:///home/test/src/main.c`, and `home://` is
`file:///home/`. A native path is never sent as a URI itself, since
`home://test/...` would parse `test` as a host. A relative name is resolved
against the working directory first. A URI with an authority or without a root
segment, and a path without a root, are errors. A buffer name `NAME://rest` is a
native path only when the editor holds root `NAME`; other schemes, such as
`man://`, stay URIs. Roots only the server holds map the same way, so a server's
`clang_resources://include/stddef.h` arrives as that path; the editor opens it
through a [server root view](#server-roots).

## Language servers

Lua buffers have a separate opt-in [LuaLS configuration](lua-language-server.md):
install `lua-language-server` and enable `lua_ls`. It reads the held Neovim
`nvim_runtime://lua` annotations and uses JSON/JSONC project settings. The
completion example below works with both clients.

The port ships a clangd configuration using bare `clangd` from the
[hosted toolchain](../development/hosted-tools.md) package. Configure a network
repository with [`pkg`](../interfaces/packages.md#package-client), then run
`pkg install clang-hosted`. Its manifest declares the complete tool bundles as
commands. The shared live package-command entry makes them available in existing
spaces, without a fixed `toolchain://` root or a per-program catalog grant. New
server launches follow the current set; an already running server retains its
selected bundle and [resource views](#server-roots). Live installs last until
reboot, with no archive fallback. The LSP configuration stays off until
`init.lua` enables it:

```lua
vim.lsp.enable('clangd')
```

A short commented example in `DISTRIBUTION=full` development images,
`boot://share/examples/nvim/init.lua`, enables clangd,
turns on completion as you type for servers that offer it, and maps `Ctrl-Space`
to open the menu by hand. Nothing loads it automatically; copy it into place:

```text
mkdir home://.config
mkdir home://.config/nvim
cp boot://share/examples/nvim/init.lua home://.config/nvim/init.lua
```

The default core archive does not include this example. On an installed system,
create `home://.config/nvim/init.lua` with the enable call
above, adding the completion settings below as needed.

For a bundle elsewhere, override the command with `vim.lsp.config` before
enabling. `cmd` replaces the whole list, so repeat the flags:

```lua
vim.lsp.config('clangd', {
  cmd = { 'home://tools/payload/clangd.pxb', '--resource-dir=clang_resources://',
    '-j=4', '--background-index=false', '--pch-storage=memory' },
})
vim.lsp.enable('clangd')
```

clangd starts for `c` and `cpp` buffers. Headers are `c` by default
(`g:c_syntax_for_h`), and `vim.filetype.add({ extension = { h = 'cpp' } })` makes
them `cpp`; either way a header attaches, and clangd itself parses `.h` as C. The project root is the nearest directory with
`compile_commands.json`, `compile_flags.txt` or `.git`, otherwise the file's
directory. The command passes `--resource-dir=clang_resources://`, `-j=4` (libc
reports no CPU count, so clangd would otherwise run one worker),
`--background-index=false` (indexing would write `.cache/clangd` into the
project) and `--pch-storage=memory`. If threads cause trouble, add `-sync`.

Pyxis has no detached children, so a language server's `detached` setting
defaults to off and the server stops with the editor.

Without `compile_commands.json` or `compile_flags.txt`, clangd compiles a file
as `clang FILE` would, with the same target, `sdk://` and resource
directory. A quoted include is then found only beside the including file or by a
relative path from it. A header in `include/`, or one at the project root included
from `src/`, is reported as `'app.h' file not found`, and hover, definition and
completion lose everything declared through it. Add `compile_flags.txt` at the
project root, one flag per line; it also marks the root:

```text
-std=c23
-Wall
-Iinclude
```

A clangd built before fork commit `abc00d0` resolves a relative `-I` against the
editor's working directory: with one, start Neovim in the project root, or use a
native path such as `-Ihome://proj/include`.

### Completion and highlighting

Pyxis builds no preamble, so clangd parses the whole file for every completion
and signature help request, which takes about as long as one diagnostics rebuild.
Member completion after `x.` then offers the struct's fields; a clangd without
fork commit `3d69e83` offers only words found in the file, all as plain text.
Semantic tokens come from the same parse and are drawn above Tree-sitter, in the
`@lsp.type.*` and `@lsp.mod.*` groups. Diagnostics are underlined: the
`TERM=pyxis` profile sends `CSI 4 m` for every underline style, undercurl
included, in the text's colour because the profile cannot colour underlines. The
framebuffer draws it as one pixel row.

### Server roots

clangd reports headers under roots only it holds, such as
`clang_resources://include/stddef.h`. The editor declares `"sdk": "optional"`
and holds `sdk://` itself, so `sdk://include/stdio.h` opens as an ordinary file.
clangd receives the editor's `sdk://`, so it sees the package set captured when
the editor started; a `dev` package installed later reaches clangd after the
editor restarts. Without `sdk://` the editor still starts, and clangd has no
system headers. The LSP transport copies declared read-only
resource directories from the actual retained libuv process selection. It does
not resolve the command again or reopen a manifest through a pathname. A root
the editor holds always wins. Otherwise, header buffers use the retained native
directory and keep the server's URI; they are `readonly` and `nomodifiable`.

Open buffers pin their original view through package switches and server exit.
A new URI read with conflicting active server versions refuses as ambiguous.
Exit removes only that server's active bindings; wiping a buffer releases its
pin. These are read-only grants, not revocation of separately held write
capabilities or a snapshot of mutable development files. Published development
revisions must remain immutable. Plain servers or undeclared roots supply no
view; other unknown roots retain the editor's existing empty, unwritable buffer
behavior.

### Default keys

Neovim maps these when a server attaches, unless a configuration overrides them
(checked against the ported revision):

| Key | Action |
| --- | --- |
| `Ctrl-]`, `Ctrl-W ]` | Go to definition, in place or in a split; `Ctrl-T` jumps back |
| `grt`, `gri` | Type definition, implementation |
| `grr` | References, in the quickfix list |
| `gO` | Document symbols, in the location list |
| `K` | Hover; press it again to enter the float |
| `Ctrl-S` (insert) | Signature help |
| `grn` | Rename |
| `gra` (normal or visual) | Code actions, such as clangd's fix-its |
| `grx` | Run the code lens on the line |
| `gq{motion}` | Format through the server |
| `Ctrl-X Ctrl-O` (insert) | Completion |
| `an`, `in` (visual) | Grow or shrink the selection by syntax node |
| `]d`, `[d`, `]D`, `[D` | Next, previous, last and first diagnostic |
| `Ctrl-W d` | Diagnostic under the cursor, in a float |

`:lsp enable NAME`, `:lsp disable NAME`, `:lsp restart` and `:lsp stop` control
servers, and `:checkhealth vim.lsp` shows what is attached. Completion as you
type is off by default; the example `init.lua` enables it per client from an
`LspAttach` autocommand with
`vim.lsp.completion.enable(true, client.id, bufnr, { autotrigger = true })` and maps
`Ctrl-Space` in insert mode to `vim.lsp.completion.get()`.
Use this with autotrigger completion:

```lua
-- Show the menu and documentation without inserting a candidate while typing.
vim.o.completeopt = 'menu,menuone,noinsert,popup'
```

`Ctrl-]` closes a [`pyxis-remote`](remote-terminal.md) session, so over Remote
use `:lua vim.lsp.buf.definition()`; at the local console it goes to the
definition. The whole setup and its native results are in [clangd](clangd.md).

## Saving and limits

File selection proves the existing parent directory with native `realpath`, then
joins the final component to that absolute scheme path. A new file needs no
identity of its own yet; an absent, stale or unprovable parent refuses selection.
The selected name stays fixed across `:cd`, including `:e!`. Force does not
bypass parent proof or grant write authority.

A buffer without an original creates exclusively: a file that appeared meanwhile
is refused by ordinary `:w`; `:w!` selects the current writable target before
truncation. `:w newname` and `:saveas` to a new name also create exclusively;
an existing destination needs force. Changing names drops the old origin;
a successful save establishes the new one. Plain `:w >> missing` refuses,
following upstream; `:w! >> missing` creates and appends, without truncation.

Each loaded file buffer retains a reference until unload. Before ordinary `:w`
truncates, it compares fresh, valid identity and modification time on that
reference and the actual writable target. Replacement, changed time or missing
comparison metadata requires `:w!`; force selects the current native target and
still needs write authority. Failed force invalidates the comparison baseline.
A successful save establishes the selected target for later ordinary saves.
`:saveas` keeps the selected new name even if writing fails, as upstream does.
Comparison metadata follows the backing, not the scheme spelling:

| Current path/backing | Held identity | Modification time and ordinary `:w` |
| --- | --- | --- |
| `home://` on a live image; `tmp://` RAM | Valid | Recorded when wall time is available; files whose sampled time is unavailable need `:w!`. |
| `home://` on an installed image; other npfs volumes | Valid | Per-inode modified-time validity; records without it need `:w!`, not every npfs file. |
| `host://` VirtIO-FS | Valid within the live mount/session | Fresh GETATTR supplies mtime; normal same-object comparisons work. Query failures remain errors. |
| `boot://` archive | Valid | Authored mtime unavailable, but the archive is read-only: `:w!` cannot overwrite it; save to a writable path. |

None of the writable backends always lacks identity or mtime. RAM without a
valid wall-clock sample and npfs records with unavailable modified time require
explicit force. A valid zero timestamp still participates in comparison. This
comparison is not a mutation lease: concurrent changes remain possible, and equal times do not prove equal
bytes. Saving retains the existing checked write/sync path.

Swap, backup/writebackup and patchmode are unavailable; defaults disable them.
Persistent undo (`:wundo`, `'undofile'`) works: undo files are created with
libc's [0666 creation mode](https://git.internal/PyxisOS/pyxis-os/issues/1369)
rather than the edited file's permission bits. Names
longer than 255 bytes fail with "name too long" on `home://` installed volumes
and `host://`, and a `host://` symbolic link is never followed
([name limits and links](libc-portability.md#name-limits-and-symbolic-links)).
`:terminal`, PTY and detached jobs, listeners, signals, numeric PIDs,
workers, asynchronous filesystem calls, file watches, dynamic Lua modules and
dynamic tree-sitter grammar loading remain unavailable. Explicit requests return errors;
the automatic socket listener is omitted. Legacy Vimscript syntax needs no
parser modules. Arbitrary plugins can encounter these limits. Rendering is
limited to the terminal's bounded UTF-8 set
([TTY output controls](terminal.md#tty-output-controls)); non-ASCII input and
wide or combining layout remain limited.

`vim.uv.os_uname()` reports `Pyxis`, the running kernel's source commit and
name, and `x86_64`, and `vim.uv.os_gethostname()` the boot's hostname, both from
the optional SYSTEM_INFO grant in the bundle manifest (without it the commit and
name are empty). `os_homedir()` and `os_tmpdir()` return `home://` and `tmp://`
when the shell passed those roots. They are scheme roots: joining a name with a
slash gives `home:///name`. `os_get_passwd()`, process and user IDs and
priorities, CPU, memory and load queries are refused and return nil, and the
runtime tolerates that where it checks. Neovim's insert-mode Tab mapping loads
`vim.snippet` and with it `vim.lsp`, which index `os_uname()` while loading;
the LSP client still has no TCP or file-watch support. The editor server
receives SYSTEM_INFO from the client, since Lua runs there. `vim.fs.normalize('~')`
returns `home:/`, because that function strips one trailing slash from the home
root, and `:checkhealth` stops early with E5009 because runtime scheme paths do
not match in `vim.fs.relpath`. The
[libuv notes](../../ports/libuv/README.md#operating-system-queries) list each
query's native source.

[First-slice qualification](../development/experiments/neovim-first-slice/README.md)
and [static-parser qualification](../development/experiments/neovim-static-parsers/README.md)
record editing, highlighting, safe-save checks, startup time and cleanup,
including [new-name saves](../development/experiments/neovim-first-slice/README.md#new-name-saves-2026-10-10).
The owner uses Neovim natively on the ThinkPad, which closes the milestone's
native qualification (2026-10-14). Further work is listed under
[Neovim follow-ups](../wip/later-os-directions.md#neovim-follow-ups).

## Milestone and delivery

The owner wanted Neovim as the development editor (vi bindings first, clangd later;
[clangd](clangd.md) has since been integrated) instead of patching BusyBox vi. The first slice is Neovim 0.12.5 in normal
terminal mode: the client launches an `--embed` server of the same program over
pipes, so the space needs `launch = true`, and it edits and saves native files in
a framebuffer tab or a multiplexer pane with Vimscript and Lua configuration under
`home://`. Decisions accepted on 2026-10-10:

- **Lua:** PUC Lua 5.1.5, which needs no executable memory or JIT. It is slower and
  plugins needing LuaJIT's `jit` or FFI will not run; LuaJIT waits for a native
  executable-memory transition.
- **Event model:** readiness plus try operations extending `wait_many`, as for
  console, terminals and TCP. There is no second submission and completion model.
- **Scope:** ASCII input, swap and backup off, and no threads, jobs or `:terminal`.
  libuv creates its pool only when work is submitted and Neovim's own file calls
  are synchronous, so the backend refuses pool and thread requests with an error;
  the source audit found no mandatory pool, worker or callback-style file call in
  startup, opening and `:w`. Excluded Lua configuration can invoke those APIs.
- **Save safety:** [saving and limits](#saving-and-limits).

| Task | Delivered | Record |
| --- | --- | --- |
| Pipe readiness and try operations | #651 | [pipes](../interfaces/pipes.md#readiness), [record](../development/experiments/pipe-readiness/README.md) |
| File identity and modification stamp | #656 | [metadata](../interfaces/file-metadata.md), [record](../development/experiments/file-identity/README.md) |
| Terminal profile for a full-screen editor | #657 | [sequence table](terminal.md#tty-output-controls), [record](../development/experiments/terminal-profile/README.md) |
| libuv 1.52.1 with a one-thread Pyxis platform layer | #664 | [port notes](../../ports/libuv/README.md), [record](../development/experiments/libuv-native/README.md) |
| Lua 5.1.5 with luv | #672 | [recipe](../../ports/lua51/README.md), [record](../development/experiments/lua51-luv/README.md) |
| Neovim recipe and first slice | #680 | [recipe](../../ports/neovim/README.md), [record](../development/experiments/neovim-first-slice/README.md) |

Task 6's shared libc groundwork is the inventory (#667), the working path and
environment (#674, [reference](process-state.md)), `realpath` and `fdopen`/`dup`
(#677), `mktime`, `iconv` and the numeric and string closure (#678,
[record](../development/experiments/calendar-encoding/README.md)). Follow-ups are
persistent undo (#690), static Tree-sitter parsers (#694), native libuv OS queries
(#695) and anchored new-file saves (#698). The terminal work Neovim drove is
delivered separately: shared SGR with 256 colours, RGB, bold, italic and underline
(#681, [record](../development/experiments/terminal-sgr/README.md)) and a bounded
UTF-8 repertoire (#699, [record](../development/experiments/terminal-utf8/README.md)),
both in the [TTY output controls](terminal.md#tty-output-controls). The Neovim
profile deliberately stays at sixteen colours and ASCII input, with reverse video
and underline as its only attributes.
