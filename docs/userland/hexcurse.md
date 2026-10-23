# Hexcurse

Install the `hexcurse` package, then run `hexcurse home://file.bin`.
The static ncursesw application is outside the boot archive. Its bundle declares
memory management and precise clock read/sleep; streams, cwd and exported file
roots use the existing bundle launch contract. It requests no launcher, network
factory or device authority.

Ctrl+P shows help; Ctrl+O opens, Ctrl+S saves, Ctrl+Q quits and Ctrl+Z undoes.
Tab switches hex/ASCII; Ctrl+F searches, Ctrl+G goes to an offset. Filename
dialogs accept ASCII; other names can be supplied in argv. Ctrl+C is a raw byte,
not an interrupt request. Mux prefixes keep their normal ownership.

The editor modifies existing bytes, preserving file length. Ordinary Save checks
the retained file's identity and writes in place, then explicitly syncs before
reporting success. Save As checks whether the target is the same file before
copying; a refused target never falls back to the source. Unknown identities and
prepared provider transactions refuse. Actual authority and backing-store sync
semantics still apply. A failed write can leave partial target changes: there is
no atomic replacement or protection against concurrent writers. Pending edits
remain available after a save error. Cancelled or refused Open retains the old
file, name and edits.

Resize retains edits. A terminal below 70 columns or seven content rows pauses
until enlarged; a filename dialog cancels on resize. The C-locale width and
existing [terminal repertoire](terminal.md) remain unchanged.

[Source and QEMU qualification](../development/experiments/hexcurse.md) cover the
first packaged application. Native checks and terminal-track measurements remain
in [gate 5](../wip/terminal-ui.md#delivery-gates-assigned-in-order-chronium-2026-10-23).
