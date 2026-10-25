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
tokens use Neovim's existing bindings. The [qualification record](../development/experiments/lua-language-server-qualification.md)
separates measured QEMU behavior from chronium's attended native check on vela.

## Analysis settings

The default language is `Lua 5.1`, matching Neovim's PUC Lua interpreter.
`workspace.library` includes `nvim_runtime://lua`, including the editor's real
`vim/_meta` definitions. This root comes from the runtime the editor actually
started with; later package changes do not substitute its annotations. Meta
files are parsed as data. Merely adding `vim` to allowed globals does not
provide API types. LuaLS classifies the root `vim` token as a global because
Neovim defines that root in an ordinary runtime file; nested annotated APIs
still receive callable types, completion, diagnostics and metadata navigation.
Use `:Inspect` to see negotiated semantic classes rather than assuming a
particular theme colour or the `defaultLibrary` modifier.

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

## Runtime and limits

The pinned MIT-licensed LuaLS/bee/LPegLabel closure embeds Lua 5.5 and uses native
files, clocks, four owned worker threads and bounded copied channels. It needs
no host Lua, dynamic module loader or target build tool. Source pins, licences
and covered adapter sources accompany the program; the port recipe is their
authority. Stdout contains LSP frames only. There is no fake PID or CPU clock.

Admission is sized from real public-workspace measurements: 8 MiB conservative
serialized messages, 32/64 MiB channel/global byte admission, 512/1024 queued
items and 4 MiB LSP bodies. Graph depth/value/table limits apply independently.
Oversized work fails visibly; congestion waits for consumption or cancellation.
The port's `pyxis/concurrency.md` records exact bounds and headroom. Queue
admission does not bound the Lua heaps: debugger checkpoints on the public source
corpus observed up to 1.07 GiB process backing, not an exact peak or leak proof.
Larger projects can require more memory than that fixture.

TCP, subprocess and debugger modes are also unavailable. The ordinary editor
checks do not qualify forced-stop/allocation-failure cases.

## Qualification

Chronium's attended vela HWP PXE check (2026-10-25) passed hover, typed completion
with descriptions beside the menu, semantic highlighting and diagnostics.
Autotrigger needs the [non-inserting completion option](neovim.md#default-keys);
accept-key mappings remain personal configuration. He closed the editor without
`:LuaLSNativeStop`; the orchestrator verified the server cache directory empty:
**stopped via editor exit, cleanup verified empty**. The captured result JSON is
the earlier readiness/typed-semantic/hover result, not a stop-command receipt.

The [capacity/editor record](../development/experiments/lua-language-server-qualification.md)
covers decoded semantic tuples, real API completion and metadata navigation,
diagnostic updates, separate project resolution, URI paths, matched nested
measurements and native limits. Earlier [runtime](../development/experiments/lua-language-server-runtime.md)
and [package/editor](../development/experiments/lua-language-server-editor.md)
records retain their distinct build and QEMU evidence.
