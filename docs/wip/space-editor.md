# Space editor

Accepted and assigned (chronium, 2026-10-29). A keyboard-driven terminal program,
`spaces`, edits the installed system's spaces in `system://config/boot.lua`.
Changes apply at the next boot; there is no live apply. It is written in Go with
tview and tcell, lives in pyxis-userland beside `libconfig/boot.c`, and ships as
an optional program package. Screenshots of every view and modal, on the host
and in QEMU, come before any hardware test.

## Contract

**What it edits.** Every space key `libconfig/boot.c` reads: `name`, `title`,
`init`, `start`, `cpus`, the booleans (`launch`, `jobs`, `focus`, `multiplexer`,
`clipboard_local`, `clipboard_shared`, `network`, `tcp_listen`, `power`,
`screenshot`, `acpi_tables`, `jit`, `package_management`, `package_upgrade`,
`remote_multiplexer`, `remote_power`), `wifi` rights, `fat32_mount`, `roots` and
`grants`. Spaces can be added (blank or copied), renamed, removed and reordered;
order is tab order. `hostname`, `input`, `volumes`, `ram_npfs_bytes` and
`wifi_policy` are carried through unchanged; volumes are offered as root and
grant targets only.

**Unknown keys.** A key the editor does not model, at the top level or in a
space, root or grant, is kept with its value and written back unchanged. The UI
marks it per space and per key, and the save review lists it.

**Validation.** Before saving, the editor applies a Go copy of the `boot.c`
per-field rules and the `boot_plan_build` cross-entry rules (one network owner,
one focus space, roots and sources naming defined volumes, `start` naming a
root, source limits). Errors block saving. Two warnings do not: a missing
`system://` init file, and no space keeping `system` read-write. Volume mounting,
CPU presence and network profiles are not checked. Boot remains the authority.
The Go copy exists only because Go on Pyxis cannot launch `config validate`
today; when it can, the editor should run that and drop its own rules. A new
space key updates `boot.c` and the editor in the same userland PR.

**Writing.** The file is rewritten in a fixed form: schema key order, booleans
written only when true except that both clipboard keys are always written, and
no comments. Saving shows a diff and needs Enter on Write. It refuses if the file
changed on disk since it was loaded. It stages and syncs `boot.lua.previous`,
replacing the previous backup, then stages, syncs and renames the new file over
`boot.lua`, then syncs `system://config/`. *Load previous* opens the backup as
unsaved changes; *Discard* reloads. There is no per-edit undo. Removing a space
asks for its name to be typed.

**Authority.** The ordinary inherited `system://` root. Read-write enables
saving; read-only opens with a banner and saving disabled. The manifest declares
only the Go runtime grants (memory, threads, precise clock, random). No space
toggle gates it. Live boots have no `system://config/boot.lua`, so the editor
reports the file missing. `spaces PATH` edits another file, which the host build
requires.

**Terminal.** Alternate screen, the `pyxis` 16-colour profile, keyboard only:
arrows, Home/End, PgUp/PgDn, Delete, Backspace, Tab, Enter, Escape and Ctrl
letters. Ctrl+S saves, Ctrl+R discards, Ctrl+P loads previous, Ctrl+Q or Ctrl+C
quits, `?` shows help. tcell receives Pyxis platform files (screen hook, `Tty`
over `syscall.Terminal*`, a `pyxis` terminfo entry) as an ordered vendor patch.

## Tasks

- [x] **1. Go terminal waits (pyxis-go).** A blocking, cancellable resize wait on
  the actual terminal `os.File`, parked on the readiness netpoller and woken by
  Close. Confirm, or implement, that a read deadline on console stdin wakes a
  parked Read. Pyxis pins the new Go commit; the Go prefix is rebuilt, not the
  compiler container.
- [ ] **2. Editor and host screenshots (userland).** Literal reader/writer,
  schema, unknown-key carry-through, validation, save procedure, all views and
  modals, the `pyxis` terminal profile for tcell and a build following userland
  `git/`. Full host screenshot set for chronium's review.
- [ ] **3. Pyxis build and package.** tcell's Pyxis terminal backend (needs
  task 1), the `spaces` program package and `make spaces-package`, QEMU
  installed-disk check, full QEMU screenshot set including a reboot that shows a
  saved change applied and a restore. User reference and release-ideas entry 6
  updated. Then chronium tests on hardware.

Tasks 2 and 3 of the proposal (model, then editing UI) were delivered as one
task: the views and the model they edit are one change.

## Screenshots

Host: the Linux build runs in tmux with `TERM=pyxis`; `capture-pane -e` output is
rendered with Bizcat glyphs and the Aardvark palette, at 80×24 and the
`make run` framebuffer geometry. QEMU: an installed disk, HMP `sendkey` and
`screendump`. Fixtures are the shipped installed `boot.lua` and a larger file
with a placeholder FAT32 GUID. Capture scripts and images are posted with the
PRs, not committed.
