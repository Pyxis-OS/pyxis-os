# System layout

Pyxis is not a read-only OS. A small boot archive holds what is needed to boot
and rescue the system, and everything else lives in writable, updatable volumes
on the installed pool. Spaces and their inits change through configuration on
the pool, without rebuilding boot media.

## Roots

| Root | Installed systems | Live boots |
| --- | --- | --- |
| `boot://` | The boot archive on the ESP: boot init, the default configuration, the rescue set and shared data (`share/`, `sdk/`). Read-only. | The whole archive. |
| `bin://` | `bin/REVISION` on the pool's `bin` volume: the running kernel's programs. Read-only. | The archive itself. |
| `system://` | The pool's `system` volume, read-write for the default space. | Not bound. |
| `home://` | The pool's `home` volume, shared by the spaces that name it. | A RAM volume, lost at reboot. |
| `tmp://` | A RAM directory shared by every space, lost at reboot. | The same. |
| `app://` | A launched [bundle](../wip/program-bundles.md) receives its own read-only `app/` directory. Plain programs have no implicit binding. | The same. |

Spaces receive `boot://`, `tmp://` and `bin://` from boot init, plus the roots
their configuration names. They start in `home://` unless their configuration's
`start` names another root; a space without that root starts in `tmp://`.

## Boot

The kernel starts one trusted boot init from the archive, in Caelum's space.
Boot init reads `boot://config/live.lua`, or `boot://config/installed.lua` on an
installed boot merged with the pool override `system://config/boot.lua`. It
mounts each volume once, creates each space through the `space_factory`
resource with that space's roots, CPU ceiling and init, then exits. A missing
or invalid override falls back to the default, and an unusable space is created
with its reason on its tab. See [init](init.md#boot-configuration) for the
configuration and its rules.

An installed disk has two boot entries: the normal `Pyxis OS (Caelum)` and
`Pyxis OS (rescue)`, which ignores the pool override and boots the archive
default. In that default, `system` is required and `home` is optional, so the
rescue entry still starts the `pyxis` space with `system://` when `home` cannot
be mounted.

## Programs

The installed archive keeps only the rescue set listed in `boot/rescue.list`,
packaged as `boot://share/installer/rescue.list`: boot init, the installer, the
shell and session, the init providers and a few file commands, including
`echo` and `cp` for writing configuration and copying recovery files. Every other
executable lives in `bin/REVISION`, one directory per kernel revision. Boot
init binds the running kernel's directory. When that directory is missing it
binds the archive instead, which on an installed system holds only the rescue
set, and says so. The shell resolves bare command names through `bin://`, then
`boot://`. An explicitly configured development catalog adds bundle command
resolution after an absent ordinary `bin://NAME.pxe`; `bin://NAME` selects a
registered logical alias. This is launcher resolution, not a new filesystem
directory backend. See [development bundle lookup](../wip/program-bundles.md#unpacked-development-lookup).

The owner accepted flat bundle placement and same-name bare lookup on
2026-10-10; these are not implemented yet. The
[bin bundle proposal](../wip/bundles-in-bin.md) covers archive/install placement,
lookup precedence, the development catalog and future ZIP/index integration.

One directory per revision is interim, until a final program update scheme
replaces it.

## Install and Update

Install formats the pool and creates the `system`, `bin` and `home` volumes.
[Update](system-updates.md) keeps the GPT and the contents of `system` and
`home`. It creates `home` when it is missing, writes the new revision's programs
into `bin`, verifies them, and only then rewrites the ESP, which is the switch
to the new revision. An Update interrupted before the ESP changes leaves the
disk booting its previous revision, and a rerun completes it. Afterwards it
removes every revision directory except the new and previous ones, or none when
the previous revision is unknown.

## Limits

- `home` is one volume shared by every space, with no ownership on disk. How a
  user's `home://` relates to it is for the
  [users work](../wip/users-and-authority.md).
- RAM volumes are carved from one private directory boot init receives; no ABI
  creates them ([technical debt](../technical-debt.md#ram-volumes)).
- `textfs` and `httpfs` stay in the rescue set, and only executables move
  ([technical debt](../technical-debt.md#rescue-set-programs)).
- Program revision directories are interim
  ([technical debt](../technical-debt.md#interim-program-revision-directories)).
- Network profiles stay in the archive
  ([technical debt](../technical-debt.md#archive-only-network-configuration)).
- Installations from before boot init, such as 0.0.2, are rebuilt on Update
  ([technical debt](../technical-debt.md#updates-from-before-boot-init)).

## Qualification

The layout was built as four tasks between 2026-10-05 and 2026-10-07, each
checked in QEMU and tasks 2 to 4 also on the ThinkPad stick:

1. The renames to `boot://` and `tmp://`, with Update from 0.0.2.
2. Boot init and space creation, with spaces added through the pool override.
3. Programs on `bin://` and the two-stage Update. `fsck.npfs` passed on pools
   after volume creation.
4. Persistent home. A Doom save in `home://` loaded after a reboot, and the
   stick's `home://` contents survived a later Update to main `9728d06`.
