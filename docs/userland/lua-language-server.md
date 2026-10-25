# Lua language server in Neovim

The `lua-language-server` program package supplies LuaLS 3.19.2 for native
stdio LSP. Neovim ships an opt-in `lua_ls` configuration using its own held
read-only API annotations. No shell wrapper or automatic download is involved.

## Setup

Configure a [package repository](../interfaces/packages.md#package-client), then
install the editor and server for the running system:

```text
pkg install nvim lua-language-server
```

Start a new editor after updating its package. Full development live images
search archive commands before packages; if their archive editor predates this
configuration, launch `pkg://nvim/nvim.pxb` explicitly. Installed systems search
the package command view first ([command order](command-search.md#defaults-and-lookup)).

Add this to
`home://.config/nvim/init.lua`, or run it with `:lua` in an existing session
whose runtime includes the configuration:

```lua
vim.lsp.enable('lua_ls')
```

The server attaches to Lua buffers. The nearest `.luarc.json`, `.luarc.jsonc`
or `.git` selects the project root; a file without a marker uses its directory.
The bare `lua-language-server` command uses the same live package-command view
as [clangd](clangd.md). An absent package or missing required grant reports a
launch failure; there is no archive fallback.

The existing [completion example](neovim.md#language-servers) works with LuaLS
as well: its `LspAttach` callback enables real server completion for any client
that supports it. Normal LSP diagnostics, hover, signature help and semantic
tokens use Neovim's existing bindings. Full API/semantic-token/native
qualification is tracked in the [milestone](../wip/lua-language-server.md).

## Analysis settings

The default language is `Lua 5.1`, matching Neovim's PUC Lua interpreter.
`workspace.library` includes `nvim_runtime://lua`, including the editor's real
`vim/_meta` definitions. This root comes from the runtime the editor actually
started with; later package changes do not substitute its annotations. Meta
files are parsed as data. Merely adding `vim` to allowed globals does not
provide API types.

Project JSON/JSONC settings override the client defaults. For a Pyxis Lua 5.5
project that does not use Neovim APIs, `.luarc.json` can contain:

```json
{
  "runtime.version": "Lua 5.5",
  "workspace.library": []
}
```

To retain Neovim types while adding other libraries, include
`"nvim_runtime://lua"` in that list. The language being analysed is independent
of LuaLS's embedded Lua 5.5 interpreter. Executable project configuration,
plugins, downloads/migrations and server-side configuration writes are excluded.
Neovim's own configuration still lives in its usual trusted `init.lua`.

Initial scanning and editor document events update the analysis. Closed files
and settings files are not watched; restart the client after external/config
changes, for example disable and re-enable `lua_ls` with `vim.lsp.enable`.
Formatting and spell/name-style features are not advertised.

## Authority and state

The bundle declares memory, real threads, clock read/precise/sleep, pipe creation
and randomness. It requests no launcher, network or SDK grant. Ordinary launch
inherits the editor's exported filesystem roots and cwd with their existing
rights, including possibly writable home; this is not workspace confinement.
The editor's `nvim_runtime` and the server's app resources remain read-only.

Each server owns a real `mkdtemp` instance under
`home://.cache/lua-language-server/` for logs and generated metadata. An orderly
shutdown/exit joins workers and removes its owned instance. A crash/forced stop
can leave that leaf for manual cleanup; it is not reused. Random names prevent
collisions, not access. There is no persistent analysis cache or `--lazy` mode.
Missing writable state, randomness, clocks or required launch grants fails
visibly. The program requires native byte-pipe stdio.

Queue, message and framing limits remain provisional pending task 3's real
workload measurements and explicit headroom; small-fixture success does not
qualify large workspaces. See the [runtime record](../development/experiments/lua-language-server-runtime.md)
for the earlier build/stdio checks and their limits.
