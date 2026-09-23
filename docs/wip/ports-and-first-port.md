# Host Lua port recipes and a first guest port

Status: working milestone draft, not an implementation assignment. Agreed
direction is distinguished from proposals and open decisions below. See the
[planning index](boot-sdk-ports.md) for sequencing.

Agreed minimal shape: a ports.lua catalog naming available ports; each port
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

## Kilo requirements and focused tasks

Reviewed source pin: `antirez/kilo` commit
`323d93b29bd89a2cb446de90c4ed4fea1764176e`, BSD-2-Clause. Preserve the license
and upstream formatting, recording ordered local patches in its recipe.

1. **Terminal prerequisites — complete.** Delayed wrap in the TTY, a visible
   nonblinking cursor and timed console reads are implemented, with libterm
   helpers and updated shell editing. See [terminal.md](../terminal.md).
2. **Required libc additions — complete.** Libc provides `strstr` and ASCII
   `isspace`, `isdigit` and `isprint`. The Kilo adaptation must cast plain-char
   byte data to unsigned char before classification. Allocation, formatting
   and basic file streams already exist.
3. **Host recipes and Kilo adaptation — complete.** The
   [ports repository](https://git.internal/chronium/pyxis-ports) provides the Lua
   runner, pinned recipe and ordered patches. Kilo uses libterm and libc streams
   with a growable line reader, checked allocations and touched bounds fixes.
   Status messages persist until replaced. Saves remain non-atomic; limitations
   are recorded beside the recipe. Processes now receive a fixed, eager 64 KiB
   stack; the former one-page stack was insufficient for ordinary C file loading.
4. **OS integration.** Pin the ports repository, stage Kilo into the boot archive,
   and manually verify opening, editing, saving, reopening and return to shell.

Terminal dimensions remain fixed for the foreseeable future; no resize signals
or POSIX terminal subsystem are needed. Wall-clock support is wanted soon, but
is separate from this port. The [timekeeping note](../technical-debt.md#timekeeping-beyond-delivered-timer-ticks)
records the needed distinction from monotonic deadlines.

The ports repository is ready and may use `PYXIS_SOURCE_READ_TOKEN` for CI
checkout. Host Lua is installed. Each `metadata.lua` owns the source pin,
license, dependencies, ordered patches and installed outputs; `ports.lua`
lists recipe directories without duplicating metadata. `build.lua` receives
SDK/toolchain and source/build/staging paths. The runner must not modify the SDK.
Do not add workflow/dispatch integration beyond the explicitly assigned task.

## Completion boundary

The host runner consumes a pinned recipe and SDK, applies ordered patches and
stages Kilo and its license. OS integration remains before this milestone is
complete: pin and package those outputs through the ordinary build.

## Decisions before implementation

- Settle any remaining recipe field/runner command details before implementing
  the host runner. Record its actual host dependencies beside its usage.
- Discuss newly discovered runtime needs before expanding the port scope.

Do not turn the candidate list into one implementation assignment. Full package
management, a sophisticated dependency solver and binary distribution are out
of scope.

## References

- [SDK prerequisite](../sdk-and-repositories.md).
- [Kilo upstream source](https://github.com/antirez/kilo/blob/master/kilo.c).
- [Lua embedding and standard libraries](https://www.lua.org/manual/5.4/manual.html).
