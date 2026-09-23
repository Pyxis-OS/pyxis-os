# Host Lua port recipes and a first guest port

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

Guest Lua no longer needs to be first. Prefer Kilo after a focused requirements
audit, then investigate TCC toward the [edit/build/run loop](edit-build-run.md).
This order is a practical recommendation, not a finding that TCC is simpler than
Lua. Keep the [Lua milestones](lua-port.md) for when a guest interpreter is needed,
notably system-wide configuration. Host Lua recipe execution stays independent.

The initial Kilo source review suggests a narrower adaptation: replace its
termios/ioctl and file-descriptor assumptions with native terminal/file access.
Pyxis already exposes terminal size, cursor movement, clearing and file streams.
Check input escape handling, visible cursor support, saving and remaining libc
calls before implementation; do not introduce a full POSIX terminal subsystem.

## Completion boundary

The host runner consumes a pinned recipe and SDK, applies ordered patches and
stages build outputs. Kilo is the proposed first consumer; its actual port stays
a separate focused task. Use that real recipe as it becomes buildable rather
than adding a dummy port.

## Decisions before implementation

- Pin Kilo and verify the proposed first-port scope against that revision.
- Agree on the small metadata/recipe contract and required host dependencies.
- Define the required native adaptations and libc/terminal additions first.

Do not turn the candidate list into one implementation assignment. Full package
management, a sophisticated dependency solver and binary distribution are out
of scope.

## References

- [SDK prerequisite](../sdk-and-repositories.md).
- [Kilo upstream source](https://github.com/antirez/kilo/blob/master/kilo.c).
- [Lua embedding and standard libraries](https://www.lua.org/manual/5.4/manual.html).
