# Neovim first slice (2026-10-10)

[Editor contract and limits](../../../userland/neovim.md).
Dependency merge order: userland #205, ports #90, Pyxis #680. Pins stay on the
published dependency heads, not later merge commits.

## Configuration and build

Final kernel/image baseline: main `2a1e4f99`, integration `aa8bbd0f` before
reference-only edits; userland `54f65d0`, ports `b688665` before final resize
review corrections. Final ports `64422ec` corrects initial resize registration
ordering and withdraws resize-only watchers on hangup; strict affected-object
builds passed. The fresh ordinary recipe/image rerun passed as well. Earlier tab/pane runs used main `b0d0bc07` with the same
editor/file policy and final libc buffering. Final correction heads and rerun
are recorded below.
Existing LLVM 23.1.3 builder (`pyxis-llvm23.1.3-49e2c1a`), mirror-only fetches,
`make -j16 image`; fresh recipe builds and full static target links passed.
Host Lua/LPeg/nlua0 and source, syntax and help-tag generation ran natively;
target Lua 5.1/luv/LPeg/libuv, utf8proc/tree-sitter and libc iconv were real.
No placeholders or compiler rebuild. Existing upstream compiler warnings remain.

Interactive QEMU 10.2.2, nested KVM, CPU max, four CPUs, 8 GiB, std VGA,
1280x800, OVMF. Optical boot used VirtIO-SCSI in the local launcher wrapper:
the host's stock QEMU AHCI path had intermittent pre-kernel failures. This is
not native performance or proof about those failed firmware boots. Live home
was RAM with valid wall time. The earlier mux run set `multiplexer = true` on Development; the final
rerun added a separate local validation mux space alongside the default tab.
Neither local configuration change is part of the PR.

## Manual observations

| Check | Result |
| --- | --- |
| Tab and split mux pane | Opened a C file, inserted with vi keys, ordinary `:w`, reopened and quit; no startup errors. |
| Legacy syntax/runtime | C PreProc, Type, String, Statement and Constant colours; `:syntax list cType` loaded packaged rules. Defaults stayed `notermguicolors`, sixteen colours and no style attributes. |
| Working path | `:cd tmp://`, `:pwd`; failed missing-directory change kept the prior cwd. Exit left shell cwd unchanged. |
| Save guard | Replaced an already loaded path through `vim.uv.fs_rename`; ordinary `:w` refused. Reading the target still returned replacement content. `:w!` selected it; the next ordinary `:w` succeeded. |
| Mux geometry and cleanup | Changed BSP to equal layout and split an active half-width editor pane to quarter-width; redraw kept content/status. Quit restored each shell's alternate screen and prompt. |
| Unsupported external command | `:!hostname` reported function not implemented, shell returned -1; no fabricated success. |
| Debugger inspection | Client/server idle waits had four/two interests (console, process observer and directional pipes); the shell observed the client separately. Console RAW passthrough had one held reference; after quit the editor waits
  disappeared and only the shell wait remained. The prompt then held its own
  normal line-reader passthrough reference. |

Unknown-mtime refusal, allocation/close/flush failures, redirected-fd-3 refusal,
and absence of implicit grant forwarding were inspected in code, not injected.
No new tests, boot automation or native editor run.

## Size and startup

The final static P1F executable is 5,040,302 bytes; packaged runtime is about
22.1 MB. Five same-configuration tab launches
used `--startuptime home://startN.log home://file.c` and the `NVIM STARTED` marker:
43.215, 75.735, 60.317, 65.257, 70.859 ms (median 65.257, range 43.215–75.735).
The first created the file; the next four reopened it. One configured-pane
sample was 65.744 ms. These are editor-instrumented startup times, excluding
firmware boot and command typing; shared-host variation is visible. There was
no runnable prior Pyxis Neovim to supply a before/after editor comparison.
After the resize corrections, three same-CPU tab launches on ports `64422ec`
measured 44.068, 59.358 and 77.656 ms (median 59.358), with the local mux space
configured alongside the tab. Editing/save, pane geometry and exit were repeated
on that final artifact. The registration/hangup race corrections are inspected;
no resize race or hangup was injected.
Raw logs/screenshots remain local under `<worktree>/build/neovim/`.

## Owner native slice

Run `boot://share/neovim/nvim.pxb home://file.c` in a tab and configured mux
pane. Insert C with vi keys, save, reopen, inspect `:syntax`, change cwd with
`:cd`, resize and quit. Check restored shell screen/input and shell cwd. On the
selected writable native backing, replace a loaded target, confirm ordinary
write refusal and explicit force. Native qualification and the richer terminal
profile remain separate steps.

## New-name saves (2026-10-10)

Baseline Pyxis `6ff8671e`, userland `796087b1`, ports `5e98894`: valid-parent
new-file creation already worked; loaded-buffer `:saveas` to a missing name
reproduced EINVAL. The owner's native report identified wrong-parent selection;
that exact native session was not reproduced. Inspection found the unproved
cwd-text fallback and the old-name reference surviving save-as.

Implementation matrix used ports `22ee343`; rebase onto ports main `43eec19`
changed no Neovim input. Published head `fb42736` and parent base `476b2a33`
passed the ordinary image build and a final quick reopen/save. No kernel/libc
changes or compiler rebuild. Same four-CPU/8-GiB nested-KVM/VGA/OVMF configuration
as above, with VirtIO block and a disposable GPT disk: partition 2 contains a
128 MiB npfs pool, 8 MiB journal, system/home volumes. A local stored boot config
adds a second shell and a shared RAM volume; no configuration changes are in
the PR. Run the editor through `boot://share/neovim/nvim.pxb`.

| Manual check | npfs `home://` / shared RAM |
| --- | --- |
| New and existing names | `expand('%:p')` stayed absolute across `:cd`; new `:w` created, and `:e!` reloaded the same target. |
| File appears before first save | Second shell created the target. Plain `:w` refused and preserved creator contents; `:w!` replaced them; the next plain save succeeded. RAM force also removed the tail of a longer original. |
| Different name | `:w newname` and `:saveas` created missing destinations. Existing save-as refused without force, then succeeded with force. |
| Append to missing name | `:w >>` refused; `:w! >>` created and appended. |
| Invalid parent | A doubled relative prefix after `:cd` refused immediately; the selected buffer/name remained unchanged. |

After guest sync and shutdown, host structural checking passed and the npfs
inspector read the expected new-file/save-as contents. Final-image quick checks
reopened the persistent header, repeated cwd/save and existing-target save-as,
created a RAM file across cwd change, and opened help. Native recheck remains
for the owner: from `home://`, open a new `jvm/include/...` file with its parent
present, inspect `%:p`, change to `home://jvm`, then save/reload; from that cwd
use `include/...`, not another `jvm/` prefix. Repeat the second-shell appearance,
plain refusal/force, new-name/save-as and append checks. Creation races,
unavailable identity, allocation failure and rename/reference lifetimes were
inspected, not injected. Raw screenshots/logs remain local.
