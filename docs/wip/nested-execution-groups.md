# Nested execution groups

Status: **parked REQUIRED direction, owner decision 2026-10-11.** Not implemented,
not a code assignment, and not a settled API or accounting design. Keep the first
[native command milestone](native-command-execution.md) on direct launch with no
descendant launcher. This direction is required before descendant execution in
the consumers below; it is not a prerequisite for their independent first slices.

## Required direction

- Any group member can create a subgroup **inside its own group**.
- A subgroup cannot outlive or escape its parent. Stopping the parent stops the
  whole subtree; parent cleanup completion covers the whole subtree too.
- A launched program receives a launcher bound to its own group by default,
  making child launch normal. The parent can deliberately withhold that launcher
  for a sandboxed profile, as the first command milestone does today.
- Authority follows the grants the child receives, always equal or narrower than
  those held/delegated by its parent. Permission to start children does not itself
  grant more directory, device, network, administrative or cross-space authority.

This is Pyxis-native containment and grant delegation, not fork, POSIX process
hierarchies or signals. Today's [execution groups](../interfaces/execution-groups.md)
are flat: bound launchers cannot create groups and members cannot receive a
launcher bound to another group. Stop/cleanup accounting has no subgroup tree.
Launch installs only explicitly supplied grants; it does not add a launcher
automatically. Those remain the implemented rules until a separately assigned
native subgroup design replaces them. Required default launcher delegation does
not settle whether each launch creates a new subgroup. Ancestry containment and
grant attenuation are separate protections. Tree cleanup must preserve honest
safe-stop/deferred-cleanup boundaries, without promising bounded retirement or
rollback of committed effects.

Owner accepted, 2026-10-11: packaged live Remote explicitly delegates same-group
LAUNCH to foreground children, enabling Neovim helpers while retaining session/
pane cleanup. This is consistent with the direction above; it implements neither
subgroups nor automatic launcher inheritance. Installed Remote entries can opt in
with the same `launch = true` setting.

## What this replaces

The outside-group executor and sibling-job-group pattern from #724's former
first decision is removed from that milestone. Nested containment is the required
later route for ordinary descendant execution, rather than requiring a broker to
create an unrelated supervision lifetime for every job. The
[remote-mux daemon pane broker](remote-multiplexer.md#private-broker-and-cleanup) could later
shrink once mux can create contained pane subgroups itself; its accepted current
implementation plan and terminal/provisioning ownership are unchanged here.

## Required consumers

- [Git CLI](git-on-pyxis.md#git-cli-restricted-profile): subcommands and remote helpers that themselves
  start children, beyond the direct no-descendant profile.
- [Hosted Clang](hosted-clang.md): the driver launching tools such as LLD or
  external cc1. The accepted single-source compile plus separately invoked LLD
  first completion remains unchanged.
- [Source builds](source-builds.md) and the [ports runner](../development/ports.md):
  native make→cc and recipes whose build tools launch children on Pyxis.
- [Libuv/Neovim](neovim-libuv.md): descendant jobs and their contained cleanup,
  beyond existing ordinary direct `uv_spawn` and first editor behavior.

## Open questions before design/implementation

No answers are selected by this direction:

- **Tree lifetime and cleanup accounting:** how are members, admitted launches,
  subgroup creation, sealing/stop races, observers, controlling grants and
  deferred/transitive cleanup counted? When is subtree completion published,
  and which references can retain storage without prolonging execution?
- **Depth/count bounds:** what limits and admission failures keep nesting and
  subtree operations bounded, and who owns/configures those limits?
- **CREATE_GROUP's remaining role:** how does creation by any member coexist
  with today's privileged top-level creation, CONTROL/WAIT and copied launchers?
  When/how is a launched program's own group selected or created?
- **Spaces:** how does subgroup containment interact with fixed group spaces,
  space creation and launch-capable administrative/service grants, without an
  escape from the parent's lifetime or an implicit authority increase?
- **Capability-valued environment:** how are bound launchers and supervision
  capabilities selected, attenuated, copied/inherited or withheld through
  [capability entries](capability-environment.md), configuration and namespaces?

Return with a concrete native authority/lifetime/accounting proposal for owner
review before implementation. Neither this direction nor a consumer dependency
link authorizes code or placeholder interfaces.
