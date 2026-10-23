# Kernel ABI and release ABIs

The kernel ABI is `include/abi/`. Compatibility between the kernel, libc and
programs is by that ABI, not by build revision
([decisions](../wip/pkg-upgrade.md#decided), chronium, 2026-10-22).

## Stability

- The ABI is stable only at releases (0.0.7, 0.0.8, …). A release's
  `include/abi/` at its tag is the contract. The next release only adds to it,
  unless a break is deliberate and listed in the release notes.
- Between releases the ABI changes in place, and only what a test needs is
  rebuilt. There are no ABI identities, levels, renumbering, reserved slots or
  compatibility shims during development.
- Reply buffers carry their capacity, so ABI structures grow by adding fields.

## Keeping a release's ABI

A release's ABI is its numbers and layouts, not only its text. Changes that keep
a listed release follow two conventions, because the [CI check](#ci) sees only
removed or changed lines:

- **Explicit values.** Every enum member and constant that crosses the ABI has
  an explicit value. Inserting a member into an implicitly numbered enum
  renumbers every later member without changing their lines.
- **Append at the end.** New enum members go after the last member, before any
  `*_COUNT`, and new structure fields go after the last field. A field inserted
  in the middle moves every later field without changing their lines.
- **Growing a reply.** Today a reply's minimum size is its whole structure, and
  a smaller capacity fails BUFFER_TOO_SMALL. The release change that first
  grows a reply keeps the old size as the minimum and writes
  `min(capacity, size)` bytes, so a caller built against the older release
  still receives the prefix it knows.

The check does not catch a renumbered enum or a moved field. It does catch
removed and changed lines, including changed comments, and renamed or removed
headers.

## The release ABI list

`boot/os-release.lua` holds the releases whose ABI the tree still provides:

```lua
return {
  abi = { "0.0.8" },
}
```

It is the only part of `os-release` that a change edits:

- A change that breaks a listed release's ABI removes that release from the list
  in the same change.
- A release adds its own name when it is tagged.

**Letter releases** (chronium, 2026-10-22). A letter release such as 0.0.8a
shares its base release's ABI name: its list keeps `"0.0.8"`, and packages built
on it name `"0.0.8"`. Its `release` and the packer's version label stay
`0.0.8a`. The list never names a letter release; generation refuses one.

The list starts empty: main had already changed lines of 0.0.7's and 0.0.7a's
`include/abi/` when the list was introduced.

## boot://os-release

Archive assembly writes `build/os-release` and places it at `boot://os-release`:

```lua
return {
  release = "0.0.8",
  revision = "1a2b3c4d5e6f",
  abi = { "0.0.8" },
  signed_floor = "2026-10-23T12:00:00Z",
}
```

- `release` is the tag at HEAD when the build is of this repository's own
  checkout, and absent otherwise. Several tags at HEAD, or a tag that is not an
  ABI name, name no release; the packer refuses to pack such a build.
- `revision` is the revision of the tree the archive was assembled from. It is
  empty for a build without Git. The kernel reports its own build revision
  through `system_info`; the two differ when only the kernel is replaced.
- `abi` is the source list.
- `signed_floor` is the UTC commit time of `revision`, absent without Git. pkg
  accepts no signed index from before it on first contact with a repository
  ([signed indexes](packages.md#signed-indexes)).

Readers refuse an `os-release` with any other field or an invalid name: it is
written and read by the same archive. The running system provides the `abi` list
plus its own `revision`. `pkg` and
command launch read it from `boot://os-release` and accept a `program` or
`toolchain` package whose `abi` is one of those names
([packages](packages.md)). A package built from a development tree names that
build's revision and only promises to work on that exact build.

## CI

The `abi` job (`scripts/check-abi.sh`) compares `include/abi/` with the tag of
every release `boot/os-release.lua` lists, so it includes the last release
whenever that is listed. Every line of each tag's `include/abi/` must still be
there:

- Added lines and files pass.
- Removed or changed lines fail, including changed comments, and so does a
  renamed or removed header.
- A listed release without a tag yet, as in the change that adds it, is
  reported and skipped.
- An empty list passes.

It compares with tags, never with main, so parallel changes do not collide on
it.
