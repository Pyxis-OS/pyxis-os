# Kernel ABI and build versions

The public kernel ABI is `include/abi/`. Its numbers and layouts determine
binary compatibility; a build revision alone does not. Release and development
package identities are generated from the real release ancestry and the shared
[ABI comparison](../../scripts/abi-contract.sh).

## Release and development identities

A real tagged release provides only its canonical release ABI. For example,
0.0.11 has `release = "0.0.11"` and `abi = { "0.0.11" }`. An existing letter
release such as 0.0.11a keeps the 0.0.11 ABI while retaining 0.0.11a as its release
and package-version label.

A clean development build is named `BASE+REV`, where REV is its 12-hex Git
revision. The latest actual 0.0.x release tag on the first-parent ancestry is
the baseline. If the current headers preserve every line of that tag, the build
provides its released base and its exact development ABI, in that order:

```lua
return {
  version = "0.0.11+abcdef012345",
  revision = "abcdef012345",
  abi = { "0.0.11", "0.0.11+abcdef012345" },
  upgrade_from = "0.0.11",
  signed_floor = "2026-10-30T12:00:00Z",
}
```

This example applies after the real 0.0.11 tag exists. When the comparison
finds removed/changed lines, the next numeric patch base in the 0.0.x series is
used and only the exact development ABI is provided: a break from 0.0.11 gives
`0.0.12+REV`, without claiming the unreleased 0.0.12 ABI. This is recomputed from
the current source; restoring the baseline can restore released compatibility.
There is no hidden sticky version state or manually maintained ABI list.

**The reader baseline is 0.0.11.** Released 0.0.10 shell, Lua and Neovim/libuv
launch paths contain static readers that reject new os-release fields and `+`
ABI names. Preserving kernel-header lines cannot repair those copies. Before
the real 0.0.11 tag, new-model builds therefore use `0.0.11+REV` with only that
exact ABI. Full released-base fallback begins with 0.0.11's rebuilt readers.
The permanent [checkpoint bootstrap](../development/release-process.md#declaring-release-checkpoints)
delivers that transition to installed 0.0.10 systems.

## Keeping a released ABI

The comparison is textual, including comments; it does not establish layout or
semantic compatibility. Changes that preserve a released ABI also follow these
conventions:

- Give every cross-ABI enum member and constant an explicit value. Inserting
  an implicitly numbered member can renumber others without changing their lines.
- Append structure fields and enum members rather than inserting them into an
  existing prefix. Preserve the existing sizes and offsets callers depend on.
- When growing a reply, keep the old size as the minimum and write
  `min(capacity, size)` bytes, so an older caller still receives its known prefix.

An intentional break is recorded in the release notes. The next real tag
freezes that release's ABI. The owner chooses a different numbering progression
when the project moves beyond the current 0.0.x series.

## Generated os-release

`make` generates `build/os-release` and archives it as `boot://os-release`.
There is no handwritten `boot/os-release.lua` input and no local tag substitute.
The same comparison drives generation and the existing `abi` CI report.
Fetch actual release tags/history; missing ancestry or ambiguous nearest release
tags refuse generation rather than inventing a base. A shallow checkout is
sufficient when the needed release and first-parent path are present.

Real-release records retain `release`, `revision`, `abi` and `signed_floor`.
Development records use `version` and `upgrade_from` instead of `release`:

- `version` is the full development identity, not a release declaration.
- `revision` remains raw provenance; it is never implicitly added to `abi`.
  The kernel's `system_info` build revision retains that meaning.
- `abi` is ordered fallback first, preferred last. Eligibility compares exact
  names. Within a selected package repository, the rightmost eligible ABI wins;
  equal-ABI builds retain last-publication order.
- `upgrade_from` is the actual release ancestor, independent of the ABI base.
  The [release router](../userland/package-upgrades.md#upgrading-the-release)
  places a dev build immediately after that entry and honors later checkpoints.
- `signed_floor` remains the source commit's UTC time. Repository signature,
  image-floor and accepted-index checks are unchanged.

Dirty local images are visibly named `BASE+REV-dirty` and provide no exact dev
ABI. They may provide only a line-compatible released base at or above 0.0.11;
otherwise the list is empty. Commit the source before packing/testing exact
dev packages. Packing requires the clean checkout and the current generated
record; edited records and missing history are refused.

The kernel command-view validator, SDK readers and host packer accept `+` in
ABI strings. Record sizes, syscalls and filesystem formats are unchanged.
Rebuild the kernel, SDK and static consumers of the package library together;
no compiler-container rebuild is needed. These changes do not make old 0.0.10
readers compatible with the new metadata.

Development catalogs stay separate from stock catalogs: old clients parse the
entire stock index before filtering ABI offers. A new field or unsupported ABI
syntax there could prevent an old client from reaching its checkpoint. See
[package upgrades](../userland/package-upgrades.md#sparse-development-repositories)
for the sparse repository and recorded-origin flow.
