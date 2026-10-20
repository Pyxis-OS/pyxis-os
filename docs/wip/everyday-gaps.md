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
| Recursive rm | 2026-10-18, owner, during clangd payload swaps | Native (rm) | `rm` has no recursive mode, so old toolchain trees such as `home://toolchain/clang-next-1` on vela can't be removed. |
| Compile flags file | 2026-10-18, owner, clangd on vela | Native or port | Nothing writes `compile_flags.txt` or `compile_commands.json`; the owner wrote theirs by hand after clangd reported a header not found ([language servers](../userland/neovim.md#language-servers)). |
| Job work in scripts | 2026-10-17, during script runtime grants | Native (shell) | A script's shell holds no `job_provisioner`, so bundles started from a script get no `job_work`; one that requires it cannot start there ([script runtime grants](../userland/shell.md#script-runtime-grants)); [proposal](#job-work-in-scripts-proposed). |
| Bundle command completion | 2026-10-17 | Native (shell) | Tab offers `NAME.pxe` programs only; bare-name bundles such as `nvim` are not completed. |
| Copilot key | 2026-10-17, owner, on vela | Native (keyboard + spaces) | [Space picker](../devices/keyboard.md#space-picker) implemented after the owner accepted all three defaults on 2026-10-18; vela's physical chord remains to be captured and qualified. A later idea is a spotlight-like prompt where `:name` moves to a space and a plain command launches a program, once there is a system overlay for it. |
| Tree-sitter for C by default | 2026-10-17, owner | Port (Neovim) | C files use legacy `syntax/c.vim`, which marks C23 compound literals inside parentheses as errors. Starting the bundled C grammar with `vim.treesitter.start()` from a `FileType` autocommand in `init.lua` fixes it and highlights much better (owner). Make that the port's default for C ([Tree-sitter grammars](../userland/neovim.md)). |
| Remote client message scroll | 2026-10-17, during Neovim jobs | Native (remote client) | In `pyxis-remote` inside tmux, Neovim's multi-line messages and `:!` output lose their scrolled lines (the same commands display fully on the local console); not investigated. |
| Shell startup file | 2026-10-17 | Native (shell) | No per-user startup file, so variables such as `cc` must be set in each session; [proposal](#per-user-shell-startup-proposed). |
| xfer inside a remote mux pane | 2026-10-12, owner, during JVM work | Native (remote client + mux) | Refused by design (`PYXIS_REMOTE_MUX=1`, see [remote-terminal.md](../userland/remote-terminal.md#remote-multiplexer)); workaround: a separate non-mux connection. Pane-aware transfer is the [initial remote multiplexer limits](../technical-debt.md#initial-remote-multiplexer-limits) debt item; owner: no rush. |

The [everyday commands](../userland/everyday-commands.md) reference covers `echo`,
`cp`, sorted/colorized `ls`, `tree`, `mv` and foreground shell-script launch authority.
The [multiplexer](../userland/multiplexer.md) now supports splitting in the
focused root shell's cwd and Ctrl+B digit focus (owner requests, 2026-10-11).

## Copilot key native gate

The owner accepted the keyboard list, exact Shift+Super+F23 chord and always-on
binding on 2026-10-18. The implemented contract is in
[keyboard input](../devices/keyboard.md#space-picker). PS/2 set-2 `57` and USB
page-07 usage `72` map to F23. Vela's i8042 keyboard is expected to send Left
Shift + Left Super + F23; that remains an expectation until the owner captures
its physical press and release. A separate, unmerged diagnostic kernel logs
modifier and non-text scan edges; no scan probe is in the production path.

QEMU qualification and its input-injection limits are recorded in the
[Copilot qualification record](../development/copilot-space-picker.md). The
owner-present vela check is pending; QEMU does not establish the native chord.

Related candidates already tracked elsewhere: grep, tail, wc, sort and hexdump in
[application ports](application-ports.md), and the boot configuration checker in
[boot configuration checker](installer-tui.md#configuration-checker).

## Per-user shell startup (proposed)

Problem: settings such as `cc` must be entered again in each shell. An ordinary
child script cannot set its parent's variables. Pyxis has no UID-based home-file
ownership check: here "per-user" means the home capability selected for that
session, which may be shared by several spaces.

Options are automatic shell code from that home, a declarative settings file
that cannot run commands, or explicit manual setup. Recommended: automatic shell
code in trusted interactive sessions, with the following decisions **pending**.

1. **Location and admission — recommended:** `home://.config/shell/startup.pxs`,
   optional and never auto-created. Trusted session/pane/Remote startup opens it
   through its held home directory and supplies a private READ FILE to its
   initial interactive shell. The shell reads that object, without resolving a
   mutable `home` binding or cwd again. No project-directory search, environment
   text override or fallback path. Missing home/file skips startup; an unreadable
   or invalid file reports the reason and leaves a usable prompt. Alternative:
   require an explicit space opt-in before opening any home startup file.
2. **Authority and trust — recommended:** run in the same shell, with the state
   and capabilities already available to manually entered commands. Variable,
   export and cwd changes persist; external commands retain their normal launch
   profiles. No extra launcher, grant or trusted-interpreter status is acquired.
   This explicitly trusts every writer of that startup file with later shell
   actions, including actions the writer cannot perform directly with its own
   grants. A private READ handle pins the object, not immutable bytes or authorship;
   neither its pathname nor a shebang authenticates it. Alternative: accept only
   declarative text settings, excluding commands and capability changes.
3. **When and recovery — recommended:** once before the first human prompt in
   local tabs, mux panes and interactive Remote sessions. Do not load it for
   scripts, `-c`, `--no-echo` machine sessions, services, init or rescue. Ordinary
   children receive no startup-file grant; opening another startup-enabled shell
   remains a trusted launch choice. Use existing script parsing/line bounds;
   startup commands add no history or terminal-completion events. An unhandled
   error or Ctrl+C stops the remaining startup body and returns to the prompt,
   retaining effects already performed. Explicit `exit`/`session` keep their
   existing meanings. A proposed `--no-startup` escape skips it without adding
   authority. Alternative: abort the shell on startup failure, with recovery
   available only through a launch that omits the file.

No new general `source` builtin, shell grammar or user/permission model is part
of this proposal. References: [shell](../userland/shell.md#script-mode),
[working state](../userland/process-state.md) and
[capability environment](capabilities-and-providers.md).

## Job work in scripts (proposed)

Problem: an interactive trusted shell can provision a bundle's declared
`job_work`, but a script shell receives no private `job_provisioner`.
A bundle requiring work therefore refuses inside a script even when the space
allows jobs. LAUNCH alone does not authorize private work provisioning.

Options are explicit provisioning delegation, an interpreter-only special case,
or keeping the refusal. Recommended: declared delegation through existing native
capabilities, without a broker or call-site wrapper. Decisions are **pending**.

1. **Separate delegation role — recommended:** the shell bundle may explicitly
   request an optional private job provisioner, distinct from a client's
   `job_work` leaf. A caller already holding provisioning authority creates a
   fresh private child subtree for that launch; grant-all-when-available follows
   the existing `jobs = true` space gate and manifest policy. A manifest cannot
   create absent authority. Ordinary bundles receive no provisioner unless they
   explicitly declare this delegation role; none inherits the parent's subtree
   or an existing client's work. Alternative: offer this only as an exact
   trusted-interpreter extra, leaving explicit bundled `shell FILE` unsupported.
2. **Script coverage — recommended:** `shell FILE.pxs`, `shell -c` and the exact
   supported shell shebang all use the same confined provisioning rule, recursively
   for nested scripts. Shebang re-entry retains the existing held-interpreter
   selection; a basename, extension or mutable URI is not proof of trusted code.
   No new authority goes to other interpreters through shebang extras. Each
   declaring job client still gets fresh work with its requested rights and
   the already-held clock, pipes and contained launcher. Required unavailable
   grants refuse before launch; optional requests remain optional. A shell
   reached from a client holding only `job_work` cannot turn that leaf into a
   provisioner: it needs an explicit delegated role from its caller. Alternative:
   support exact shebang scripts first and retain the other documented refusals.
3. **Lifetime — recommended:** keep the provisioning subtree outside roots,
   cwd and exported environment entries. Delete work only after confirmed
   retirement of every process/group that may use it, never on script EOF or
   interpreter exit alone. Preserve background, service and `session` semantics:
   when a user of that subtree outlives the script, retain it privately until
   its enclosing owner proves cleanup, or reboot. Unknown admission, creator loss
   and uncertain cleanup retain the existing no-retry/RAM-retention rule. A
   successor must not receive a subtree that its predecessor will erase.
   Alternative: refuse job-capable launches that escape the script's contained
   group, allowing cleanup solely through that group's retirement.

This does not implicitly forward job contexts to ordinary children or broaden
the direct job core's grant policy. It uses the existing nested-group foundation;
no outside-group executor or new cleanup supervisor is proposed. References:
[bundle job contexts](../userland/program-bundles.md#private-job-contexts),
[script runtime grants](../userland/shell.md#script-runtime-grants),
[nested groups](nested-execution-groups.md), and the existing
[cleanup limit](../technical-debt.md#private-job-context-cleanup-after-caller-failure).
