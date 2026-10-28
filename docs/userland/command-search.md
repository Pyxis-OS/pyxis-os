# Command search

After the interactive shell's [one-time alias substitution](shell.md#startup-settings),
bare command names use the ordered directory capabilities held in the exported
`path` environment entry. The shell, Lua run/jobs and libuv/Neovim use the
same owned resolver. Finding code supplies no launcher, network, writable root
or package-management authority. See the [native interface](../interfaces/command-search.md)
for construction, bounds, capture and lifetime rules.

## Defaults and lookup

Trusted startup supplies read-only `bin://` and then a distinct `boot://` entry.
On an installed boot `bin://` is the live package-command view, so installed
packages' commands come first and the core archive's after; live bin and boot
are one entry, plus the live package-command view when a packages volume is
mounted. The built-in rescue space searches the archive alone. No home, cwd, development catalog or writable directory is
searched implicitly. A parent may narrow, reorder or replace its list using
held directories, or explicitly append a project command directory.

Missing and empty lists search nothing, even when independent bin/boot roots are
held. Paths containing `/` or `:` use their own held authority; `bin://NAME` is
an exact path, not a suffix or catalog alias. Search narrowing does not revoke
explicit path access.

First match wins across entries. Only genuine absence continues to the next
entry; inaccessible, malformed, revoked or incomplete targets fail visibly.
Conflicting plain `.pxe` and bundle `.pxb` exports within one conventional
directory refuse. Shell `which NAME` reports the selected entry, target and
package identity without executing. A later independent launch can select a
newer version.

## Configuration and runtime edits

The persistent `path` key in [session.lua](session-configuration.md) is one exact
ordered list of held directory locations, for example:

```lua
path = { "home://tools", "bin://", "boot://" },
```

Absent uses the existing boot defaults; an explicit empty list searches nothing.
The configured list does not append defaults or add a fixed personal directory.
Locations resolve from the session's existing roots/cwd and become read-only
native directory CAPs. User order wins, including shadowing ordinary command names.
On live boots, explicitly selecting only bin and boot also omits the separate
live package-command entry from the default list. Installed bin is itself the
live package-command view. These are the existing root meanings.

Runtime changes use ordinary shell values, not a builtin or textual PATH:

```text
path=(home://tools $path)
export path=(home://project/bin $path)
```

Literal/expanded path strings resolve through held native directories. Directory
CAP members are attenuated to held read rights; a held list such as `$path`
splices its entries at that position in the same order. An empty `()` creates an
empty list. The old publication remains unchanged if construction fails.
`path=...` is private and `export path=...` is inherited, like other environment
values. Existing processes retain their captured list. Labels are bounded
diagnostics, so `which` identifies entries by ordinal as well as label.
The old environment name `commands` has no lookup or inheritance meaning.

## Packages and retained selections

[Packages](../interfaces/packages.md) explicitly declare their command exports.
Undeclared files export nothing; duplicate names within one set refuse
publication. Each immutable set includes its verified command projection before
`current` switches. No per-program catalog grant or wrapper is needed.

A resolution captures each originating package volume once, including misses.
Image, bundle command, stack, manifest grants, resources and provenance use that
same selection. Later resolutions in an already running client follow `current`;
existing children and held resource views keep the old selection. The view
exposes indexed code and bundle resources, never its private packages root.
Other package-data grants require separately held authority from that volume.

Neovim reads server resource views from the process selection it actually
launched. Open header buffers retain their selected resources after server exit
or package replacement. Conflicting active server roots refuse as ambiguous.
Package/toolchain consumer delivery remains in the [packages milestone](../wip/packages.md).

## Ports and limits

A PATH-aware spawn accepts absent `PATH` or its exact diagnostic label projection;
other text refuses rather than creating directory authority. Native Lua command
APIs do not interpret child PATH text as parent lookup policy. Explicit native
job profiles remain explicit, and withholding launch authority still prevents
execution. A native `uv_pyxis_spawn` resource named `path` replaces the
inherited entry, including malformed CAP/TEXT, and is itself validated. Explicit
TEXT `path` in the child environment still refuses. See the
[adapter notes](../../ports/libuv/README.md) and
[replacement checks](../development/experiments/command-search-adapters/README.md#review-correction).

Libc does not implement process replacement or `execvp`; spawn-and-wait is not
substituted. Native compiler/build-tool descendant chains retain the separate
[nested-execution gate](../wip/nested-execution-groups.md). General textual PATH
translation is deferred; command labels remain diagnostic text.

## Qualification

The milestone completed on 2026-10-19 with builds and four-CPU nested QEMU/GDB
checks: [list ownership](../development/experiments/command-search/README.md),
[live package views](../development/experiments/package-command-view/README.md)
and [launcher/editor adapters](../development/experiments/command-search-adapters/README.md).
These records distinguish fixture coverage from native hardware and native
build-chain qualification.
