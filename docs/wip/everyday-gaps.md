# Everyday gaps

Status: **running list, started 2026-10-07.** Small things that are missing or
awkward during real use of Pyxis. An entry here is a reminder, not a decision:
each one still needs a place in a milestone and the usual discussion before work
starts. Add a line when something is noticed, and remove it when it is done.

Commands follow the [ports and native commands](../development/ports.md#ports-and-native-commands)
boundary: anything that shows or changes a Pyxis concept is native, and familiar
text tools may be ports.

| Gap | Noticed | Likely kind | Notes |
| --- | --- | --- | --- |
| Compile flags file | 2026-10-18, owner, clangd on vela | Native or port | Nothing writes `compile_flags.txt` or `compile_commands.json`; the owner wrote theirs by hand after clangd reported a header not found ([language servers](../userland/neovim.md#language-servers)). |
| Lua job lookup | 2026-10-17, during `home://bin` | Native (Lua port) | `pyxis.job` and `pyxis.pipeline` search only plain `bin://`, `boot://` and `home://bin/` images, without the bundles or development catalog that shell and `pyxis` launch use ([system layout](../userland/system-layout.md#programs)). |
| Job work in scripts | 2026-10-17, during script runtime grants | Native (shell) | A script's shell holds no `job_provisioner`, so bundles started from a script get no `job_work`; one that requires it cannot start there ([script runtime grants](../userland/shell.md#script-runtime-grants)). |
| Bundle command completion | 2026-10-17 | Native (shell) | Tab offers `NAME.pxe` programs only; bare-name bundles such as `nvim` are not completed. |
| Merged stderr and stdout | 2026-10-17, accepted with Neovim jobs | Native (shell) | [Proposal assigned](shell-gaps.md#2-ordered-21-with-one-byte-sink); the shell has no `2>&1`, so `:make` collects only stderr (`shellpipe=2>%s`); a tool reporting on stdout needs a wrapper. |
| Conditional sequences | 2026-10-17, owner, after script arguments | Native (shell) | [Proposal assigned](shell-gaps.md#1-conditional-list-execution-and-explicit-status); `&&` and `\|\|` are unsupported ([command sequences](../userland/shell.md#typed-values-and-command-sequences)), so `./build.pxs && ./app.pxe` cannot run the program only after a successful build. At the prompt `;` runs the next command even after a failure; scripts and `shell -c` lines already stop at the first failure. |
| Copilot key | 2026-10-17, owner, on vela | Native (keyboard + spaces) | The key usually sends a chord (Left Shift, Left Super and F23; check what vela sends), and Pyxis has no F23, so it does nothing. Owner's current choice: a space switcher. A later idea is a spotlight-like prompt where `:name` moves to a space and a plain command launches a program, once there is a system overlay for it. |
| Tree-sitter for C by default | 2026-10-17, owner | Port (Neovim) | C files use legacy `syntax/c.vim`, which marks C23 compound literals inside parentheses as errors. Starting the bundled C grammar with `vim.treesitter.start()` from a `FileType` autocommand in `init.lua` fixes it and highlights much better (owner). Make that the port's default for C ([Tree-sitter grammars](../userland/neovim.md)). |
| Exit status argument | 2026-10-17 | Native (shell) | [Proposal assigned](shell-gaps.md#1-conditional-list-execution-and-explicit-status); `exit` takes no status, so a script or `shell -c` line cannot choose its own nonzero status. |
| Remote client message scroll | 2026-10-17, during Neovim jobs | Native (remote client) | In `pyxis-remote` inside tmux, Neovim's multi-line messages and `:!` output lose their scrolled lines (the same commands display fully on the local console); not investigated. |
| Shell startup file | 2026-10-17 | Native (shell) | No per-user startup file, so variables such as `cc` must be set in each session. |
| xfer inside a remote mux pane | 2026-10-12, owner, during JVM work | Native (remote client + mux) | Refused by design (`PYXIS_REMOTE_MUX=1`, see [remote-terminal.md](../userland/remote-terminal.md#remote-multiplexer)); workaround: a separate non-mux connection. Pane-aware transfer is the [initial remote multiplexer limits](../technical-debt.md#initial-remote-multiplexer-limits) debt item; owner: no rush. |

The [everyday commands](../userland/everyday-commands.md) reference covers `echo`,
`cp`, sorted/colorized `ls`, `tree`, `mv` and foreground shell-script launch authority.
The [multiplexer](../userland/multiplexer.md) now supports splitting in the
focused root shell's cwd and Ctrl+B digit focus (owner requests, 2026-10-11).

Related candidates already tracked elsewhere: grep, tail, wc, sort and hexdump in
[application ports](application-ports.md), and the boot configuration checker in
[boot configuration checker](installer-tui.md#configuration-checker).
