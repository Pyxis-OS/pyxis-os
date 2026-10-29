# Space editor

The optional `spaces` package edits the spaces in an installed system's
[boot configuration](init.md#boot-configuration), `system://config/boot.lua`.
Changes apply at the next boot; nothing changes in the running system.

```sh
pkg install spaces
spaces            # edits system://config/boot.lua
spaces PATH       # edits another file, such as a copy in home://
```

Run it in a space that holds `system://` read-write, such as the installed
`pyxis` space. With `system://` read-only it opens with saving disabled. Live
boots read the archive's `boot://config/live.lua`, so there is no
`system://config/boot.lua` to edit. The editor needs no space setting of its own:
its bundle requests only memory, threads, precise clock and random, and reaches
the file through the caller's inherited root.

## What it edits

- **Spaces:** add (blank or copying the selected space), rename, remove (type
  its name to confirm) and reorder. The order is the tab order.
- **General:** name, title, init, start root and CPUs.
- **Settings:** every boolean the [boot configuration](init.md#boot-configuration)
  defines, Wi-Fi rights and `fat32_mount`.
- **Roots:** a volume or the `pkg`/`sdk` view, access and optional.
- **Grants:** [selected sources](init.md#fat32-volumes-and-selected-grants) with
  volume, kind, access, optional and path.

`hostname`, `input`, `volumes`, `ram_npfs_bytes` and `wifi_policy` are kept
unchanged; volumes appear only as root and grant targets. A key the editor does
not know, at the top level or in a space, root or source, is kept with its value
and listed as a warning in the space list, its Settings tab and the save review.

## Saving

Ctrl+S checks the file and blocks saving on errors. The checks copy the space
rules boot applies: names, titles, init, CPUs, start, one network owner, one
focus space, roots and sources naming defined volumes, FAT32 access, package and
HTTP staging rules and the source limit. Two warnings do not block saving: a
`system://` init that is not readable, and no space keeping `system`
read-write. Volume mounting, CPU presence and the other files are not checked;
`config validate` checks CPUs, and boot remains the authority. The copy exists
only because Go on Pyxis cannot launch `config validate`.

Without errors, the review shows the diff against the file on disk. Write
refuses if the file changed since it was opened. Otherwise it stages and syncs
the loaded file as `boot.lua.previous`, replacing the previous backup, stages
and syncs the new file and renames it over `boot.lua`, then syncs
`system://config/`. A failed directory sync is reported with the saved message.

The file is rewritten in a fixed form: two-space indentation, keys in schema
order, booleans written only when true except that both clipboard settings are
always written, and no comments. Explicit `false` values and absent clipboard
settings keep their meaning.

Ctrl+R discards changes and reloads the file. Ctrl+P opens `boot.lua.previous`
as unsaved changes, which save like any other edit. There is no per-edit undo.
If a saved configuration does not boot, the rescue entry keeps `system://`
read-write:

```sh
cp system://config/boot.lua.previous system://config/boot.lua
```

## Keys

| Key | Action |
| --- | --- |
| Up/Down, PgUp/PgDn, Home/End | Move |
| Enter | Open a space, edit a field or toggle a setting |
| Space | Toggle a setting |
| 1–4 | General, Settings, Roots, Grants |
| a, c, r, Del, [ and ] | Space list: add, copy, rename, remove, move up and down |
| a, e, Del | Roots and Grants: add, edit, remove |
| Esc | Back, or close a dialog |
| Ctrl+S | Check, review and save |
| Ctrl+R | Discard changes |
| Ctrl+P | Open `boot.lua.previous` |
| Ctrl+Q, Ctrl+C | Quit, asking about unsaved changes |
| ? | Help |

The editor uses the alternate screen and the `pyxis` terminal profile. It has no
mouse support.

## Limits

- Enter and Ctrl+J are the same key: the Pyxis keyboard sends LF for Enter.
- tview marks a truncated table cell with `…`, which the Pyxis terminal draws as
  its replacement glyph. Layouts fit 80 columns with ordinary names.
- The build and its pins are in the userland
  [space editor README](../../userspace/space-editor/README.md);
  `make spaces-package REPOSITORY=DIR GO_PREFIX=DIR` packs it like
  [git](git.md).
