# Removing files and directory trees

`rm [-r] [--] PATH...` removes names with native directory REMOVE. Without `-r`
it removes files only, and a directory operand fails with the actual error.
`--` ends options, and any other option is a usage error. There is no `-f`, no
prompt and no verbose mode. `rmdir PATH...` removes empty directories only.

```text
rm home://draft.txt
rm -r home://toolchain/clang-next-1
rm -r -- -odd-name
```

## Recursive removal

`rm -r` removes a file operand as plain `rm` does. For a native directory
operand it resolves the operand's parent once and works only from held handles:
it looks the directory up from that parent, empties it, and removes its name from
the parent. Each child is likewise looked up from its parent's handle, so the
walk never uses `..`, never re-resolves a path and never follows a link. It adds
no authority. Because a child's rights are bounded by its parent's, the
operand's parent, the directories along its path and every directory in the tree
need LOOKUP, ENUMERATE and REMOVE. A directory without them is reported and left
in place. The text is used as given. A trailing `/` makes the operand
directory-only, so `rm -r file/` fails. An operand that is not a native path,
such as a provider route, has no directory to walk and is removed as plain `rm`
would.

These operands are refused before anything is removed:

- a root such as `home://`, and an operand whose final component is `.` or `..`.

Every directory the walk enters, the operand included, is also refused when it
is the current directory or one of its ancestors, compared by object identity
([file metadata](../interfaces/file-metadata.md#identity-and-lifetime)), or when
it cannot be told apart from them. Such a directory is reported and left in
place before its contents are touched.

Entries that are neither files nor directories, such as a host symbolic link or
special file, are reported as not removed and skipped, never followed. An entry
whose kind the backend leaves unknown is tried as a file and then as a
directory; neither follows a link. A directory nested more than 64 levels deep,
counting the operand as the first, is reported and left in place. A host bind
mount inside a `host://` tree is emptied, then its removal fails as busy.

## Failure and interruption

The command keeps going after an error: it reports each failure as
`rm: PATH: reason`, leaves a directory it could not empty, and exits 1 after
attempting every operand. A failed operand gets one `Not removed` line after the
failures themselves; the directories between are silent. If the contents were
removed but the directory itself was not, the one line names the path with
`(contents removed)`. There is no rollback. A name another process removes first
counts as removed.

If another process changes a directory while it is being emptied, `rm` starts
again from the beginning. It gives up on that directory after several changes in
a row with no removal between them. While entries keep being created and removed,
`rm` keeps going until Ctrl+C.

Ctrl+C ends `rm` at once through [foreground interruption](foreground-interruption.md)
and can leave a partly removed tree; run the command again to finish. Removals
join the filesystem's running transaction and are not durable when `rm` returns;
run [`sync`](everyday-commands.md) on the parent to request it. The native
interface cannot condition a removal on which object a name still means, so a
directory another process moves away mid-run is still emptied through the held
handle, and a name replaced after its directory was emptied is removed in its
place if it is empty. See [directory removal](../interfaces/directories.md#removal)
for the per-name contract and [filesystem mutations](../interfaces/filesystem-mutations.md)
for durability. The accepted limits are in
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1474).
