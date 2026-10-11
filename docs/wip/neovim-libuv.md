# Neovim on Pyxis

Status: **milestone decisions accepted 2026-10-10; tasks 1-6 delivered; task 7
(owner native qualification) is open.** The owner wants Neovim as the
development editor (vi bindings now, clangd later) instead of patching BusyBox
vi. Implemented behavior is in the [Neovim reference](../userland/neovim.md).
Later tasks start only on the owner's go.

**Scope of the first slice.** Neovim 0.12.5 in its normal terminal mode: the
client launches an `--embed` server of the same program over pipes, so the
space needs `launch = true`. It edits and saves native files in a framebuffer
tab or a multiplexer pane with Vimscript and Lua configuration under `home://`.
Not in it: swap and backup recovery, jobs and `system()`, `:terminal`, language
servers, dynamically loaded parsers, threads. BusyBox vi stays until the owner
accepts the slice, and receives no further editor work.

## Tasks

1. [x] **Pipe readiness and try operations** (#651): `wait_many` watches pipes,
   which gain try read and write ([pipes](../interfaces/pipes.md#readiness),
   [qualification](../development/experiments/pipe-readiness/README.md)).
2. [x] **File identity and modification stamp** (#656): [metadata
   reference](../interfaces/file-metadata.md),
   [qualification](../development/experiments/file-identity/README.md).
3. [x] **Terminal profile for a full-screen editor** (#657): alternate screen,
   scroll region, saved cursor and `TERM=pyxis` in the framebuffer TTY and the
   multiplexer ([sequence table](../userland/terminal.md#tty-output-controls),
   [qualification](../development/experiments/terminal-profile/README.md)).
4. [x] **libuv backend** (#664): libuv 1.52.1 with a one-thread Pyxis platform
   layer ([port notes](../../ports/libuv/README.md),
   [qualification](../development/experiments/libuv-native/README.md)).
5. [x] **Lua 5.1.5 with luv** (#672): the [`lua51` recipe](../../ports/lua51/README.md),
   [qualification](../development/experiments/lua51-luv/README.md).
6. [x] **Neovim recipe and first slice** (#680): the [recipe](../../ports/neovim/README.md),
   [editor reference](../userland/neovim.md) and
   [QEMU qualification](../development/experiments/neovim-first-slice/README.md).
   Shared libc groundwork: inventory (#667), working path and environment
   (#674, [reference](../userland/process-state.md)), proved `realpath` and `fdopen`/`dup`
   (#677), `mktime`, `iconv` and the numeric/string closure (#678,
   [qualification](../development/experiments/calendar-encoding/README.md)).
   Follow-ups: persistent undo (#690), static Tree-sitter parsers (#694,
   [qualification](../development/experiments/neovim-static-parsers/README.md)),
   native libuv OS queries (#695), anchored new-file saves (#698). The shared
   libc work also serves [hosted Clang](hosted-clang.md) and [Git](git-on-pyxis.md).
7. [ ] **Owner native qualification and closure.** On the ThinkPad, run the
   bundle in a tab and a configured multiplexer pane: edit, save and reopen C,
   inspect syntax, use `:cd`, resize and quit. Confirm ordinary replacement
   refusal and explicit `:w!` on the selected native backing. The profile
   exclusions stay. Native checks and the richer terminal-profile follow-up
   remain separate assignments.

The terminal work Neovim drove is delivered separately: shared SGR (256 colours,
RGB, bold/italic/underline) in the kernel TTY, multiplexer and remote client
(#681, [qualification](../development/experiments/terminal-sgr/README.md)), and
a bounded UTF-8 repertoire (#699,
[qualification](../development/experiments/terminal-utf8/README.md)); both are in
the [TTY output controls](../userland/terminal.md#tty-output-controls). The
Neovim profile deliberately stays at sixteen colours and ASCII input.

## Accepted decisions

1. **Lua runtime (2026-10-10).** PUC Lua 5.1.5. It needs no executable memory
   or JIT support; it is slower, and plugins that need LuaJIT's `jit` or FFI
   will not run. LuaJIT (Neovim's bundled commit `fbb36bb6`) waits for a native
   executable-memory transition.
2. **Event model (2026-10-10).** Readiness plus try operations, extending
   `wait_many` as for console, terminals and TCP. There is no second
   submission and completion I/O model.
3. **First-slice scope (2026-10-10).** ASCII only, swap and backup off in the
   packaged configuration, no threads, jobs or `:terminal`. Swap and backup need
   the identity contract and a lease and recovery policy first.
4. **Thread-free first slice.** libuv creates its pool only when work is
   submitted, and Neovim's own file calls pass no callback and are synchronous,
   so the backend refuses pool and thread requests with an error. The source
   audit found no mandatory pool, worker or callback-style file call in bounded
   startup, opening and `:w`. Excluded Lua configuration can invoke those APIs.
5. **Save safety (2026-10-10).** One held file reference per loaded buffer;
   ordinary `:w` refuses replacement or unavailable comparison metadata and
   `:w!` selects the current native target. This is not a mutation lease. The
   behavior and the per-backing validity table are in the
   [Neovim reference](../userland/neovim.md#saving-and-limits).

## Later tasks

Each needs its own proposal and the owner's go.

- Swap and backup recovery: needs a lease policy. The swap creator PID in file
  bytes must not become control authority, and Unix `flock` is not a basic-editor
  prerequisite. Poll-based change detection needs truthful metadata and timers;
  file watches can follow.
- Jobs and `system()` consume [native command execution](native-command-execution.md)
  for opt-in FILE/resource selection and supervised capture and deadlines; Neovim
  retains its job and terminal integration. Ordinary `uv_spawn` stays the
  default without executor authority. Descendant jobs require
  [nested execution groups](nested-execution-groups.md), a parked required
  direction, before they are delivered. Stopping a job needs a cooperative-vs-
  forced policy over held capabilities or authorized groups, not numeric-PID
  killing; closing a process handle does not stop the child.
- Terminal input reporting: xterm mouse modes 1000/1002/1006 SGR and focus mode
  `?1004` (`CSI I` / `CSI O`) in TTY tabs and spaces, multiplexer panes and the
  remote client. This enables `vim.opt.mouse` and `FocusGained`/`FocusLost`; the
  owner wants optional save on focus loss with `autocmd FocusLost * silent! wall`.
  Neither reporting path exists today; mouse and focus are one follow-up task.
- Netrw, currently unavailable natively; native scheme handling in generic
  runtime path helpers, including the `:checkhealth` frontend.
- The richer `pyxis` profile: advertise indexed/RGB colours and bold, italic and
  underline (now rendered by the terminals), then select `termguicolors`; the
  current profile stays at sixteen colours. A small Vimscript scheme can assign
  highlight groups explicitly because upstream's default dark groups do not all
  set a terminal foreground. A Neovim Unicode profile is separate.
- `:terminal`: needs terminal-session creation, child attachment, raw/cooked
  responsibilities, resize, hangup and flow control, with a Pyxis PTY adapter;
  native launch replaces `forkpty`'s launch role without fork or Unix session IDs.
- User threads and the libuv pool: need shared-address-space create/join,
  synchronization, per-thread TLS and errno, thread-safe libc and a process-wide
  lifetime policy; see [threads](threads.md). Separate worker
  processes cannot replace them.
- Dynamic Tree-sitter parsers, C Lua modules and LuaJIT: need relocatable code
  loading with permissions and lifetime, and executable-memory transitions
  (the PXE tool rejects dynamic linking and TLS). The seven bundled grammars are
  already static.
- LSP and networking: stdio language servers need jobs first; TCP LSP and
  listeners need asynchronous native networking with listen/accept. Working the
  LSP client would not show that clangd runs; clangd follows the
  [hosted Clang direction](hosted-clang.md).
- Universal Ctags for tags (parked by the owner 2026-10-10).
- Compiler diagnostics as Neovim diagnostics before clangd (the owner prefers
  red underlines to jump-to-definition).
- Far-future owner direction, 2026-10-10 (unassigned): scalable terminal fonts,
  choosing TTF/OTF/WOFF by implementation ease, for example an stb_truetype-style
  rasterizer with a glyph cache. No font source, dependency or scalable-font
  contract exists.

## Configuration acceptance target

Owner direction, 2026-10-10: the real `chronium/nvim-configs` configuration
(lazy.nvim, Primeagen-style) should mostly work on Pyxis. This is a long-term
acceptance test, not a claim that these plugins were qualified. The first
acceptance set is `set.lua`, remaps, undotree, a copied-in harpoon, and tokyonight
once the richer terminal profile lands. Exercise the actual editing and
navigation bindings, undo history and colours, preserving explicit errors for
unavailable features.

| Config feature | Required follow-up / acceptance condition |
| --- | --- |
| lazy.nvim bootstrap, fugitive, plugin updates | [Git on Pyxis](git-on-pyxis.md) and Neovim jobs/`system()`; install and update real sources through native processes. |
| `<leader>pv` -> `:Ex` | Native netrw directory browsing. |
| `"+y` / `"+Y` | New Neovim clipboard provider (`g:clipboard` or a provider module) wired to the [native typed clipboard](../interfaces/clipboard.md), with a small native helper if needed; settle authority and activation before implementation. |
| nvim-treesitter `:TSUpdate` | Runtime parser compile/load remains unavailable; use rebuilt, statically bundled parsers instead. |
| mason / lspconfig | Native LSP servers; clangd follows [hosted Clang](hosted-clang.md). Mason stays out because it downloads Linux binaries. |
| telescope | Plenary jobs; ripgrep/fd are optional additional tools. |
| tmux-sessionizer / chmod | Unix-specific and out; Pyxis mux replaces tmux. |
| `vim.opt.mouse`, focus/save | The terminal input reporting task above; enable `FocusLost` with optional `silent! wall` only once real focus events arrive. |

Stock launch does **not** set `HOME` for Neovim; an explicitly inherited `HOME`
remains the caller's environment. The profile supplies native standard-path
defaults without inventing that variable. Replace the config's
`os.getenv("HOME")` undo-directory construction with
`vim.fn.stdpath("state") .. "/undo"`; the stock state path is
`home://.local/state/nvim`. This portable configuration change needs no profile
or global environment change. Use a writable, existing undo directory.
