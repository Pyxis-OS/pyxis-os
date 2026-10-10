# Neovim

The [recipe](../../ports/neovim/README.md) packages Neovim 0.12.5 as a native
terminal bundle. From a local shell with launcher and pipe authority:

```text
boot://share/neovim/nvim.pxb file.c
```

A configured catalog may name the same bundle `nvim`. Ordinary commands and
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
real language objects with ABI validation, without module loading. Upstream
ftplugins start Tree-sitter for Lua, Markdown, help and query; C and Vim keep
legacy syntax by default. Their parsers are available to explicit
`vim.treesitter.start()`. An unavailable language returns no parser from
`language.add()`/`get_parser()`; highlighting start quietly preserves/restores
legacy syntax. Explicit dynamic parser paths remain unsupported. The exact
pins and MIT/Apache-2.0 notices are in the recipe and staged provenance.

`:cd` uses the [shared working path](process-state.md), affecting the editor
server and its future child snapshots, not the shell. Filename expansion keeps
native schemes; a descriptive cwd is never accepted as a proved `realpath`.
The TUI launches only its same-image internal `--embed` server with explicit
pipe CREATE. It does not forward its private `app://` root; the runtime grant
survives independently. Redirected stdin requiring fd 3 is refused before launch.

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
The [backing validity table](../wip/neovim-groundwork.md#slice-4-contract-accepted-2026-10-10)
explains RAM, npfs, host and archive cases. This comparison is not a mutation
lease: concurrent changes remain possible, and equal times do not prove equal
bytes. Saving retains the existing checked write/sync path.

Swap, backup/writebackup and patchmode are unavailable; defaults disable them.
Persistent undo (`:wundo`, `'undofile'`) works: undo files are created with
libc's [0666 creation mode](../technical-debt.md#public-open-creation-mode)
rather than the edited file's permission bits. Names
longer than 255 bytes fail with "name too long" on `home://` installed volumes
and `host://`, and a `host://` symbolic link is never followed
([name limits and links](libc-portability.md#name-limits-and-symbolic-links)).
External jobs, `system()`, `:terminal`, PTYs, listeners, signals, numeric PIDs,
workers, asynchronous filesystem calls, file watches, dynamic Lua modules and
dynamic tree-sitter grammar loading remain unavailable. Explicit requests return errors;
the automatic socket listener is omitted. Legacy Vimscript syntax needs no
parser modules. Arbitrary plugins can encounter these limits. Font rendering
remains the terminal's ASCII profile.

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
record editing, highlighting, safe-save checks, startup time and cleanup.
Native qualification is the milestone's next slice.
