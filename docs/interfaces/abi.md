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

The list starts empty: main had already changed lines of 0.0.7a's `include/abi/`
when the list was introduced.

## boot://os-release

Archive assembly writes `build/os-release` and places it at `boot://os-release`:

```lua
return {
  release = "0.0.8",
  revision = "1a2b3c4d5e6f",
  abi = { "0.0.8" },
}
```

- `release` is the tag at HEAD when the build is of this repository's own
  checkout, and absent otherwise. Several tags at HEAD are refused.
- `revision` is the build revision, as the kernel reports it. It is empty for a
  build without Git.
- `abi` is the source list.

The running system provides the `abi` list plus its own `revision`. `pkg` and
command launch read it from `boot://os-release` and accept a `program` or
`toolchain` package whose `abi` is one of those names
([packages](packages.md)). A package built from a development tree names that
build's revision and only promises to work on that exact build.

## CI

The `abi` job (`scripts/check-abi.sh`) finds the last release tag reachable from
HEAD. If `boot/os-release.lua` lists it, every line of that tag's `include/abi/`
must still be there: added lines and files pass; removed or changed lines fail.
The comparison is textual, so a changed comment counts as a change. If the list
does not name that release, the job reports so and passes. It compares with the
tag, never with main, so parallel changes do not collide on it.
