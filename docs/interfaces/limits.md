# Limits

How Pyxis treats fixed sizes and counts. These are chronium's decisions of
2026-10-29 and 2026-10-30, taken after an audit of every fixed cap in the kernel,
userland, `pkg`, the installer, the tools and the ports. A number chosen for
convenience must not decide what a user can do; a limit that remains is a
property of a device, a format or the memory the machine has, and says so.

## Rules

1. **Size from real capacity.** Where a capacity exists (free space on the
   volume, installed RAM, a device or format field, a wire width), a limit is
   derived from it, not from a constant. A constant stays only for a real field
   width, a recursion depth or a sanity bound on a small file read into a fixed
   buffer, and is named as such.
2. **Refuse before side effects.** A refusal that can be known early comes
   before anything is downloaded, extracted, written or confirmed by the user,
   not after.
3. **One status.** A size or count refusal is `CALL_LIMIT`, with a reason. No
   second status (`BAD_REQUEST`, `FILE_TOO_LARGE`, `IO`) stands for the same
   thing, and no new `E2BIG` status is added: libc maps `CALL_LIMIT` to `E2BIG`
   where POSIX callers need it. The reason field is defined with the launcher
   work that introduces it; until then the reason travels as text.
4. **Name the overflow.** The message says what overflowed, by how much and
   where the limit comes from:
   `<what>: <N> exceeds the limit of <M> (<source>)`.
5. **No silent truncation of user data.** A value that does not fit is refused
   or reported as truncated, never cut without a word.

## Consequences already decided

- The launcher limits (startup size, capture budget, image capture, batch
  size, stack) may break the ABI: the 0.0.x policy in the
  [release process](../development/release-process.md#policy) allows it.
- The live image's RAM pool is sized from installed RAM, not a fixed 2 GiB.
  Default ESP and Coexist pool sizes are decided with their reasons in the
  work package that changes them.
- Shell history on disk has no cap: each line is appended and only the tail
  is read ([shell](../userland/shell.md)).

## Package indexes and fetching

The second package. The package command index (`sets/N.commands`) and the `sdk://`
index (`sets/N.sdk`) are held whole in kernel memory. They were limited to 256
commands and 1 MiB each out of a fixed 4 MiB, which also capped the number of
installed packages with files at about 1,080, and the limits were reported only after
every archive was downloaded. They are now bounded by one thing: the kernel's package-view
memory, one 512th of installed RAM and at least 4 MiB, reported in
`package_command_info.budget`. `pkg` checks the two indexes against it before the review and
names the sizes and the limit; the file format is unchanged, so a set staged by an older
`pkg` stays valid. The size of a set's indexes depends on each package's manifest, which `pkg`
can read only after extraction; a refusal that needs earlier knowledge would put counts in
the repository index, which is not done here.

A fetch has an inactivity window, not a total time derived from its size: it fails after 30 s
without data (`--timeout SECONDS` changes the window and has no upper bound) and a transfer
that keeps arriving never expires. See [package formats](packages.md#command-projection-and-bootstrap)
and [HTTP transactions](http-transactions.md).

## Literal parser

The first package of that work. `userspace/lib/literal.c` limited a document to
8,192 values and entries and 1 MiB, which capped a repository index at 511
ABI-bound versions and an installed set at about 800 packages. It no longer
limits the number of values, entries or comments, finds duplicate keys in linear
time, and bounds work in proportion to the source. A repository index and set
record are read whole and parsed without a source limit; the remaining bounds are
memory, the table depth of 32 (recursion), and a 1 MiB limit on small documents
read into a fixed buffer. The details and the compatibility consequence for older
clients are in [package formats](packages.md#literal-data) and the
[release process](../development/release-process.md).

**Open follow-up (not in this change).** `pkg` parses a repository index before it
verifies the signature, because the parsed header names the signer. With no
parser cap, a hostile or broken server can make the client allocate for an
index of any size. Signature verification over the raw bytes should come before
parsing; the signer would then have to be named outside the signed text (for
example by the signature file) or checked after a first verification against the
pinned CA. This needs a design decision before any code.
