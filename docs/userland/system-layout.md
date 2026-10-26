# System layout

Pyxis is not a read-only OS. A small boot archive holds what is needed to boot
and rescue the system, and everything else lives in writable, updatable volumes
on the installed pool. Spaces and their inits change through configuration on
the pool, without rebuilding boot media.

## Roots

| Root | Installed systems | Live boots |
| --- | --- | --- |
| `boot://` | The core archive on the ESP: boot init, the rescue set, Install's files and shared data. Read-only. | The whole archive. |
| `bin://` | The live command view of the installed packages: one entry per exported command. Read-only. | The archive itself. |
| `system://` | The pool's `system` volume, read-write for the default space. | Optional and read-only: a volatile `ram-npfs` volume holding the package repository configuration, lost at reboot. Package repository edits use separately confined authority. |
| `home://` | The pool's `home` volume, shared by the spaces that name it. | A RAM volume, lost at reboot. |
| `tmp://` | A RAM directory shared by every space, lost at reboot. | The same. |
| `app://` | A launched [bundle](program-bundles.md) receives its own read-only `app/` directory. Plain programs have no implicit binding. | The same. |

The pool also holds a `packages` volume, the package store, and a private
`http-work` volume that backs HTTP transfers. Neither is an ordinary root: spaces
receive `packages` only as a read-only package source and `http-work` only through
trusted provider startup. Live boots back both with volatile `ram-npfs` volumes
([boot configuration](init.md#boot-configuration); [packages](../interfaces/packages.md)).

Spaces receive `boot://`, `tmp://` and `bin://` from boot init, plus the roots
their configuration names. They start in `home://` unless their configuration's
`start` names another root; a space without that root starts in `tmp://`.

In `DISTRIBUTION=full` development images, `boot://share/checks/` holds Lua
scripts for hand-run checks, such as native run
sheets: `lua boot://share/checks/console-burst.lua`. See the
[checks README](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/checks/README.md).
`boot://share/examples/` holds C sources to read and compile inside Pyxis, such as
`pointer-min.c` and `font-min.c`
([walkthrough](../development/edit-build-run.md#try-the-pointer-example)).
These directories are absent from the default core archive.

## Boot

The kernel starts one trusted boot init from the archive, in Caelum's space.
Boot init reads `boot://config/live.lua`, or on an installed boot the pool's
`system://config/boot.lua`, `session.lua` and `network.lua`. It
mounts each volume once, creates each space through the `space_factory`
resource with that space's roots, CPU ceiling and init, then exits. A missing
or invalid pool file starts only the built-in rescue space, and an unusable
space is created with its reason on its tab. See [init](init.md#boot-configuration) for the
configuration and its rules.

An installed disk has two boot entries: the normal `Pyxis OS (Caelum)` and
`Pyxis OS (rescue)`, which starts the built-in rescue space with `system://`
read-write. In the initial configuration `system` is required and `home` is
optional, so the `pyxis` space still starts when `home` cannot be mounted.

## Programs

The core archive (`DISTRIBUTION=core`, the default) holds the programs named in
`boot/rescue.list`, packaged as `boot://share/installer/rescue.list`: boot init,
the installer, the shell and session, the init providers, a few file commands
such as `echo` and `cp` for writing configuration and copying recovery files,
`log`, `lsusb` and `lspci` for diagnosis, and `pkg`, `net`, `wifi` and the Wi-Fi
supplicant, so that rescue can reach a repository and reinstall packages. USB/PCI
names come from the archive, with numeric fallback if unavailable. Every other
program is a package ([packages](../interfaces/packages.md)), one per tool.
A `DISTRIBUTION=full` image keeps every program, the init scripts and the live
spaces in the archive, for QEMU and stick work
([decisions 20 and 64](../wip/pkg-upgrade.md)).

On an installed boot, boot init binds `bin://` to the live command view of the
installed packages, so a package's commands appear there as soon as its set is
active; with no packages volume it binds the archive and says so. Live boots
bind the archive itself. Trusted startup supplies a read-only command list:
`bin://` first, then the archive as `boot://`; live bin and boot are the same
entry. The built-in rescue space runs from the archive alone. Shell,
Lua jobs and libuv resolve bare names through that held list only; no catalog,
home or cwd fallback remains. Explicit list replacements can narrow or reorder
held entries; missing and empty lists search nothing. Development/project
commands need an explicitly appended directory. `which NAME` reports the
selected entry, target and captured package identity. Explicit `.pxb` paths
select the default entry. See [command lists](../interfaces/command-search.md).

`nvim` and `lua5.1` need no catalog: their bundles are `pkg://nvim/nvim.pxb` and
`pkg://lua5.1/lua5.1.pxb`, exported as commands. Each contains its application
files, port notes, provenance and application/dependency notices under `app/`;
metadata lives in `app/metadata/`. Other development commands need an explicit
command-directory entry. ZIP launch remains
[future work](../wip/program-bundles.md).

## Install and Update

Install formats the pool, creates the `system`, `home`, `packages` and
`http-work` volumes, writes the initial configuration, and copies the kernel and
the core archive to the ESP as built. [Update](system-updates.md) keeps the GPT
and the contents of `system` and `home`, creating any missing volume, prepares a
compatible complete package set, and replaces the kernel and core archive.
Installs from before the core archive kept their programs in `bin/REVISION` on a
`bin` volume; Update removes copies only when it can prove their ownership. The
emptied `bin` volume stays
([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1426)).

## Limits

- `home` is one volume shared by every space, with no ownership on disk. How a
  user's `home://` relates to it is for the
  [users work](../wip/users-and-authority.md).
- RAM volumes are carved from one private directory boot init receives; no ABI
  creates them ([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1424)).
- Installations from before boot init, such as 0.0.2, are rebuilt on Update
  ([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1423)).

## Qualification

The layout was built as four tasks between 2026-10-05 and 2026-10-07, each
checked in QEMU and tasks 2 to 4 also on the ThinkPad stick:

1. The renames to `boot://` and `tmp://`, with Update from 0.0.2.
2. Boot init and space creation, with spaces added through a pool override file,
   since replaced by the whole [installed configuration](init.md#installed-configuration).
3. Programs on `bin://` and the two-stage Update. `fsck.npfs` passed on pools
   after volume creation.
4. Persistent home. A Doom save in `home://` loaded after a reboot, and the
   stick's `home://` contents survived a later Update to main `9728d06`.
