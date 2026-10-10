# Moving files

`mv [--] SOURCE... DESTINATION` renames files within a volume with libc
`rename`, which is the native atomic file replacement. It has no options other
than `--`.

```text
mv home://draft.txt home://final.txt
mv home://a.txt home://b.txt home://archive
mv -- -odd-name home://
```

- **Destination is a directory.** Each source moves into it under its own last
  component, so `mv FILE DIR` leaves `DIR/FILE`, and `mv A B DIR` moves both. A
  trailing `/` on a source is ignored. A name already in the directory is
  replaced if it is a file, as with a plain rename.
- **Otherwise.** With two operands the destination is the exact new path, and an
  existing file there is replaced. Several sources need a destination directory;
  a missing destination reports "Not found" and an existing file reports "Not a
  directory".
- **Failures.** Sources are moved in order and a failure does not stop the rest.
  The exit status is failure if any source failed. A missing source reports
  `mv: SOURCE: Not found`, a directory source `Directories cannot be moved`, and
  a scheme root cannot be moved. Other failures name both paths and libc's
  message. Nothing is deleted when a rename fails.
- **Limits.** Files only, within one volume. Directories and moves between
  volumes are unsupported, with no copy fallback. Whether the destination is a
  directory is read once before the first move.

The directory test needs only LOOKUP on the destination. The shell summary is in
[the shell guide](shell.md#commands-and-quoting) and the rename contract in
[filesystem mutations](../interfaces/filesystem-mutations.md).
