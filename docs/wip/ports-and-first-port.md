# Lua port recipes and a first port

Status: working milestone draft, not an implementation assignment. Agreed
direction is distinguished from proposals and open decisions below. See the
[planning index](boot-sdk-ports.md) for sequencing.

Proposed minimal shape: a ports.lua catalog naming available ports; each port
has a metadata table, build.lua and an explicit patch order. Metadata includes
source URL, exact commit (or release archive plus checksum), license, dependencies
and installed outputs. Separate host build dependencies from Pyxis libraries.
Choose one source of truth for version/dependency data rather than duplicating
it in the catalog and recipe.

Use a host Lua interpreter for recipes; this does not depend on first porting
Lua into Pyxis. Start with a small runner supplying toolchain/sysroot, source,
build and destination directories. Recipes can invoke the upstream build system.
Build into a staging directory rather than overwriting the consumed SDK. Package
format, dependency resolver sophistication and binary distribution are separate
questions; do not invent them all for the first port.

Lua is the agreed first port. Its runtime work is split into the smaller
[milestones in the Lua worklist](lua-port.md), rather than one large port PR.
Host recipe execution is independent of those guest milestones. Kilo and the
other candidates remain in [later directions](later-os-directions.md).

## Completion boundary

The host runner consumes a pinned recipe and SDK, applies ordered patches and
stages build outputs. The Lua milestones provide the first working guest port;
do not make the recipe infrastructure PR responsible for completing all of Lua.
Use that real recipe as it becomes buildable rather than adding a dummy port.

## Decisions before implementation

- Pin the Lua source revision as part of the first Lua milestone.
- Agree on the small metadata/recipe contract and required host dependencies.
- Keep guest runtime work in the Lua milestones below.

Do not turn the candidate list into one implementation assignment. Full package
management, a sophisticated dependency solver and binary distribution are out
of scope.

## References

- [SDK prerequisite](sdk-and-repositories.md).
- [Lua embedding and standard libraries](https://www.lua.org/manual/5.4/manual.html).
