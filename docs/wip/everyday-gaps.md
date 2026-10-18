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
| Copilot key | 2026-10-17, owner, on vela | Native (keyboard + spaces) | Owner-assigned investigation and [space-switcher proposal](#copilot-key-proposal); binding decisions and vela's actual chord still need confirmation. A later idea is a spotlight-like prompt where `:name` moves to a space and a plain command launches a program, once there is a system overlay for it. |
| Tree-sitter for C by default | 2026-10-17, owner | Port (Neovim) | C files use legacy `syntax/c.vim`, which marks C23 compound literals inside parentheses as errors. Starting the bundled C grammar with `vim.treesitter.start()` from a `FileType` autocommand in `init.lua` fixes it and highlights much better (owner). Make that the port's default for C ([Tree-sitter grammars](../userland/neovim.md)). |
| Remote client message scroll | 2026-10-17, during Neovim jobs | Native (remote client) | In `pyxis-remote` inside tmux, Neovim's multi-line messages and `:!` output lose their scrolled lines (the same commands display fully on the local console); not investigated. |
| Shell startup file | 2026-10-17 | Native (shell) | No per-user startup file, so variables such as `cc` must be set in each session. |
| xfer inside a remote mux pane | 2026-10-12, owner, during JVM work | Native (remote client + mux) | Refused by design (`PYXIS_REMOTE_MUX=1`, see [remote-terminal.md](../userland/remote-terminal.md#remote-multiplexer)); workaround: a separate non-mux connection. Pane-aware transfer is the [initial remote multiplexer limits](../technical-debt.md#initial-remote-multiplexer-limits) debt item; owner: no rush. |

The [everyday commands](../userland/everyday-commands.md) reference covers `echo`,
`cp`, sorted/colorized `ls`, `tree`, `mv` and foreground shell-script launch authority.
The [multiplexer](../userland/multiplexer.md) now supports splitting in the
focused root shell's cwd and Ctrl+B digit focus (owner requests, 2026-10-11).

## Copilot key proposal

Owner-assigned 2026-10-18; **proposal, not accepted or implemented**. Inspected
main `a3efad31`. Vela's [inventory](../targets/ideapad-slim5-16iph11/ideapad-inventory.txt)
identifies an i8042 keyboard; its [native status](../targets/ideapad-slim5-16iph11/notes.md)
confirms PS/2 input. This is not a USB-only key-mapping fix.

The [key ABI](../../include/abi/key.h) names only F1–F12. The
[PS/2 decoder](../../arch/x86_64/keyboard.c) silently drops unmapped scans,
including nonextended `57`; the [USB keyboard table](../../kernel/usb/hid.c)
also omits Keyboard F23, usage `72` on page `07`. Linux's
[set-2 table](https://github.com/torvalds/linux/blob/master/drivers/input/keyboard/atkbd.c)
maps `57` to F23 (`F0 57` releases it); `5F` is F24. These establish the missing
mapping, **not what vela's firmware actually sends**. Left Shift + Left Super +
F23 remains the expected, unmeasured chord.

Main has no scan observation that can confirm that chord. A temporary debug-only
probe can report modifier edges and unmapped non-text scans, including the
extended/break flags, from the task-side decoder. One log format and one
press/release capture suffice; ordinary character keys must not be logged.
The expected make codes are `12`, `E0 1F`, `57`; record the actual release order
as well. This observation stays out of the production binding. No probe or
native capture has been run for this proposal.

### Decisions requested

1. **Picker behaviour — default: a keyboard-operated list.** A fresh Copilot
   press opens a compact list of existing spaces in the tab bar's registry
   order, initially highlighting the current space. Up/Down moves without
   wrapping; Home/End selects an end. Enter commits the highlighted space;
   Escape or a second Copilot press cancels. Releasing the key does not commit.
   The underlying active space changes only on Enter. The visible list is
   clipped/scrolled to the display; no new space or command is created. This
   gives explicit selection rather than advancing one tab per press. Pointer
   selection and the future command prompt are outside this slice.
2. **Chord matching — default: F23 with exactly Shift + Super.** Either side
   of those modifiers works, as with existing logical shortcuts; Control/Alt
   suppress the binding and lock state is ignored. Act only on a fresh press,
   consuming its repeats and release even when modifiers release first.
   Recognize F23 through both PS/2 and USB maps; standalone F23 and nonmatching
   chords remain ordinary physical key events, with no new terminal escape
   sequence. Match the chord, not a Lenovo ID. Confirm vela's actual sequence
   before claiming native support.
3. **Configuration — default: enabled without a new setting.** Use one global
   binding alongside Super+Left/Right, which keep their existing behaviour.
   No per-space grant, boot option or remapping language is needed for this
   action. An opt-out/remapping facility can be considered when there is an
   actual conflicting use.

Implementation would use the presenter's existing keyboard-overlay focus
handling: applications receive focus loss, held state is cleared, and relative
pointer lock requires a fresh activation after dismissal. The picker owns input
while shown; trigger/confirmation repeats and releases cannot leak to the chosen
space. Input reset cancels it. The power overlay has priority and cancels the
picker; the two cannot hold overlay focus concurrently. Rendering uses the
existing framebuffer/font path and bounded visible rows, not a new overlay
service or runtime allocator.

After owner acceptance: qualify PS/2 and USB F23 decoding, exact and rejected
modifier combinations, repeats/release ordering, cancel/commit, long space lists,
small displays, graphical keyboard capture, pointer lock, input reset and power
overlay interaction in QEMU. The owner then checks the physical key on vela.
Source inspection and QEMU must not be labelled native-key qualification.

Related candidates already tracked elsewhere: grep, tail, wc, sort and hexdump in
[application ports](application-ports.md), and the boot configuration checker in
[boot configuration checker](installer-tui.md#configuration-checker).
