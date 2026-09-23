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

Suggested first consumers are Lua and Kilo, to expose concrete libc, terminal
and file-operation gaps. Lua configuration evaluation and a full Lua program
need not receive identical libraries/authority. Current stdio formatting lacks
floating point; runtime/math and file support should be assessed against the
selected upstream revision. Other candidates remain in [later directions](later-os-directions.md). Each
needs its own platform contract review, not POSIX-shaped kernel syscalls added
by default. The first port and priority order remain undecided.

## Completion boundary

A pinned port builds through a host Lua recipe against the SDK, stages its
outputs and runs in Pyxis. Address only the concrete runtime gaps of the chosen
port. The host recipe runner does not depend on guest Lua being available.

## Decisions before implementation

- Choose the first port and pinned source revision; Lua and Kilo are candidates.
- Agree on the small metadata/recipe contract and required host dependencies.
- Review that port's libc, terminal and filesystem requirements.

Do not turn the candidate list into one implementation assignment. Full package
management, a sophisticated dependency solver and binary distribution are out
of scope.

## References

- [SDK prerequisite](sdk-and-repositories.md).
- [Lua embedding and standard libraries](https://www.lua.org/manual/5.4/manual.html).
