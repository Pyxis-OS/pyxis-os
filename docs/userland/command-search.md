# Command search

After the interactive shell's [one-time alias substitution](shell.md#startup-settings),
bare command names use the ordered directory capabilities held in the exported
`commands` environment entry. The shell, Lua run/jobs and libuv/Neovim use the
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
execution. A native `uv_pyxis_spawn` resource named `commands` replaces the
inherited entry, including malformed CAP/TEXT, and is itself validated. Explicit
TEXT `commands` in the child environment still refuses. See the
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
