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

`rm -r` removes a file operand as plain `rm` does. For a directory operand it
opens the directory with LOOKUP, ENUMERATE and REMOVE, empties it from the held
handle, then removes its name from the parent. Each child is reached by lookup
from its parent's handle, so the walk never uses `..`, never leaves the named
tree and never follows a link. It needs REMOVE on every directory in the tree and
on the operand's parent; it adds no authority. A directory whose rights lack one
of these is reported and left in place.

These operands are refused before anything is removed:

- a root such as `home://`, and an operand whose final component is `.` or `..`;
- the current directory or any of its ancestors, compared by object identity
  ([file metadata](../interfaces/file-metadata.md#identity-and-lifetime)); an
  operand that cannot be told apart from them is refused too;
- an operand that cannot be opened as a directory with the rights above.

Entries that are neither files nor directories, such as a host symbolic link or
special file, are reported as not removed and skipped, never followed. A directory
nested deeper than 64 levels is reported and left in place.

## Failure and interruption

The command keeps going after an error: it reports each failure as
`rm: PATH: reason`, leaves a directory it could not empty, and exits 1 after
attempting every operand. A directory that could not be emptied also gets one
`Not removed` line. There is no rollback. A name another process removes first
counts as removed. If another process keeps changing a directory, the removal
gives up on that directory after a few passes.

Ctrl+C ends `rm` at once through [foreground interruption](foreground-interruption.md)
and can leave a partly removed tree; run the command again to finish. Removals
join the filesystem's running transaction and are not durable when `rm` returns;
run [`sync`](everyday-commands.md) on the parent to request it. See
[directory removal](../interfaces/directories.md#removal) for the per-name
contract and [filesystem mutations](../interfaces/filesystem-mutations.md) for
durability.
