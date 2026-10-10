# Directory trees

`tree [--ascii] [-L LEVELS] [--] [DIRECTORY...]` lists a directory and everything below it,
defaulting to the working directory. It uses the caller's own grants and
resolves each path as `ls` does.

```text
tree
tree -L 2 home://
tree boot://sdk -L 1
tree --ascii home://src | less
```

Each operand is printed as written, then its entries with UTF-8 box drawing:

```text
home://
├── docs/
│   └── notes.txt
└── build.sh
```

- **Branch encoding.** UTF-8 is the default for terminals, pipes and files.
  `--ascii` selects `|--`, `` `-- `` and `|   ` prefixes for byte-oriented
  consumers such as the current less, whose wrapping can split UTF-8 glyphs.
  It changes branch marks, not filename bytes. There is no automatic fallback.
- **Order and names.** Entries are sorted by unsigned byte order within each
  directory, dot names included, with `/` after directories. This is the
  [`ls`](ls.md) listing code, so names, kinds and colors match: blue
  directories, green `.pxe` programs, cyan scripts. On a terminal control and
  non-ASCII bytes show as `?`. Pipes and files get no colors and literal names.
- **Depth.** `-L N` shows N levels below each operand, from 1 to 64; `-L1` is the
  operand's own entries. Without it the walk is unlimited up to 64 levels. Deeper
  directories are listed no further and reported as a failure.
- **Summary.** A blank line and the counts of directories and files shown follow
  the last operand.
- **What is followed.** Only directories. Anything else, including a host
  symbolic link, is listed as a file and never entered.
- **Failures.** A directory that cannot be listed still appears, is reported,
  and the walk continues; the exit status is failure. A non-directory operand
  fails after its header.
- **Limits.** No timestamps, sizes, hidden-name filtering or file operands. Each
  level holds its listing while its subdirectories print, so memory grows with
  the depth times the widest directory. Entries are live observations, not a
  snapshot.

`tree` is in the normal image and, unlike `ls`, not in the installed rescue set.
