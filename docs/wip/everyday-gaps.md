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
| Local host names | 2026-10-22 (chronium) | Native (networking) | The LAN router does not resolve `horse` (static .213), so `pkg` repository URLs need the IP. Direction (chronium, 2026-10-22): a proper hosts file with the standard structure (address, canonical name, aliases), consulted before DNS; not yet scheduled. |
| Background output over the edited line | 2026-10-20 (chronium), observed with the DHCP line | Native (shell + terminal) | Asynchronous output from background writers lands on top of the line being edited, and the shell does not redraw its prompt. The DHCP line that showed it now goes to the system log, so that source is fixed; the general redraw is not. |
| Compile flags file | 2026-10-18, owner, clangd on vela | Native or port | Nothing writes `compile_flags.txt` or `compile_commands.json`; the owner wrote theirs by hand after clangd reported a header not found ([language servers](../userland/neovim.md#language-servers)). |
| Copilot key | 2026-10-17, owner, on vela | Native (keyboard + spaces) | [Space picker](../devices/keyboard.md#space-picker) implemented after the owner accepted all three defaults on 2026-10-18; vela's physical chord remains to be captured and qualified. A later idea is a spotlight-like prompt where `:name` moves to a space and a plain command launches a program, once there is a system overlay for it. |
| Absolute pointer in QEMU | 2026-10-25, outside tester on sway (Wayland) | Native (device drivers) | Pyxis reads only relative pointers, so without a grab (Ctrl+Alt+G) the QEMU pointer drifts out of step with the guest's on Wayland. Support for a `usb-tablet` or `virtio-tablet` device would fix it; it needs native drivers. |
| pkg HTTPS connection reuse | 2026-10-25, outside tester, fresh install | Native (pkg, httpfs) | Each package fetch costs about 1 s whatever its size on a fresh install: the 4 KiB `base` took 1.3 s. Likely a new HTTPS connection per package; keep-alive in `pkg` and `httpfs` would avoid it. |
| Process list follow-ups | 2026-10-25 (chronium) | Native (system model) | [`procs`](../userland/procs.md) lists the caller's space implicitly and every space through the manifest-requested system view (chronium, 2026-10-29). Still missing: stopping a process from the list (aligned with delta's cooperative signals), a shell `jobs` builtin, which needs a job table, and a self-query over the same counters for ports with stubs (libuv `uv_getrusage` and `uv_resident_set_memory`, Neovim `os_getpid`, Go `os.Getpid`). |
| xfer inside a remote mux pane | 2026-10-12, owner, during JVM work | Native (remote client + mux) | Refused by design (`PYXIS_REMOTE_MUX=1`, see [remote-terminal.md](../userland/remote-terminal.md#remote-multiplexer)); workaround: a separate non-mux connection. Pane-aware transfer is the [initial remote multiplexer limits](https://git.internal/PyxisOS/pyxis-os/issues/1366) debt item; owner: no rush. |

## Proposed later: session-setting reload

Chronium's direction (2026-10-28): shell startup should eventually inherit/reload
session settings so tab width, program path and related settings can change
without rebooting. This is proposed later work, not an accepted implementation
task. Runtime `export path=(...)` changes the executing shell's value only;
configuration reload is outside that mechanism.

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
