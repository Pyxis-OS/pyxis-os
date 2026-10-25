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
| Tab completion of `./` paths | 2026-10-22 (chronium), on vela, after recompiling font-min | Native (shell) | Typing `./` then Tab in the shell does not complete entries of the current directory. |
| Local host names | 2026-10-22 (chronium) | Native (networking) | The LAN router does not resolve `horse` (static .213), so `pkg` repository URLs need the IP. Direction (chronium, 2026-10-22): a proper hosts file with the standard structure (address, canonical name, aliases), consulted before DNS; not yet scheduled. |
| Background output over the edited line | 2026-10-20 (chronium), observed with the DHCP line | Native (shell + terminal) | Asynchronous output from background writers lands on top of the line being edited, and the shell does not redraw its prompt. The DHCP line that showed it now goes to the system log, so that source is fixed; the general redraw is not. |
| Declarative aliases | 2026-10-20, owner, startup settings | Native (shell) | [Startup settings](../userland/shell.md#startup-settings) hold text variables only; the shell has no alias mechanism. Aliases must stay declarative, with no executable startup hook. |
| Compile flags file | 2026-10-18, owner, clangd on vela | Native or port | Nothing writes `compile_flags.txt` or `compile_commands.json`; the owner wrote theirs by hand after clangd reported a header not found ([language servers](../userland/neovim.md#language-servers)). |
| Personal command directory | 2026-10-20 | Native (shell) | Bare names resolve only through the held `commands` list (installed bin, boot and the package view), by design since 2026-10-19. A program copied to `home://bin` runs by path (`home://bin/NAME.pxe`), but a bare name is not found and Tab does not offer it. The only way to add a directory to the list is a native program calling the SDK's `command_list_create`; the shell has no command for it ([command search](../userland/command-search.md)). |
| Copilot key | 2026-10-17, owner, on vela | Native (keyboard + spaces) | [Space picker](../devices/keyboard.md#space-picker) implemented after the owner accepted all three defaults on 2026-10-18; vela's physical chord remains to be captured and qualified. A later idea is a spotlight-like prompt where `:name` moves to a space and a plain command launches a program, once there is a system overlay for it. |
| Absolute pointer in QEMU | 2026-10-25, outside tester on sway (Wayland) | Native (device drivers) | Pyxis reads only relative pointers, so without a grab (Ctrl+Alt+G) the QEMU pointer drifts out of step with the guest's on Wayland. Support for a `usb-tablet` or `virtio-tablet` device would fix it; it needs native drivers. |
| pkg HTTPS connection reuse | 2026-10-25, outside tester, fresh install | Native (pkg, httpfs) | Each package fetch costs about 1 s whatever its size on a fresh install: the 4 KiB `base` took 1.3 s. Likely a new HTTPS connection per package; keep-alive in `pkg` and `httpfs` would avoid it. |
| Process list | 2026-10-25 (chronium) | Native (system model) | No way to list running processes or jobs: there is no `ps` or `top` equivalent. It would be a native command that shows a Pyxis concept, not a POSIX `ps` port, per the ports-versus-native boundary above. What it lists and who may see it is an authority question: spaces and held job or CONTROL capabilities against a system-wide view. It needs a design discussion before any work; native jobs are in [native jobs](../userland/native-jobs.md). |
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
