# Nested execution groups

Status: **minimal Clang-driver prerequisite accepted and assigned, 2026-10-13**,
a bounded planning-freeze exception
([#788](https://git.internal/PyxisOS/pyxis-os/pulls/788), implementation draft
[#789](https://git.internal/PyxisOS/pyxis-os/pulls/789)). No subgroup implementation
is on main yet. Broader nesting, automatic group selection and Continuum remain
parked. The [delivered command core](../userland/native-jobs.md) still supports
direct launch without descendants; the accepted minimal slice supplies the
contained lifetime required for Clang to launch LLD.

## Required direction

Owner direction (2026-10-12): surviving private `job_work` cleanup belongs with [Continuum and its family](spaces.md#asterism), alongside this direction, revisiting the [caller-failure RAM retention limit](../technical-debt.md#private-job-context-cleanup-after-caller-failure).

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
[remote-mux daemon pane broker](../userland/remote-terminal.md#pane-broker-and-cleanup) could later
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

The minimal slice is settled in #788; these broader questions remain parked:

- **Automatic group and launcher selection:** the minimal slice provides explicit
  caller-scoped creation through LAUNCH, subtree containment/cleanup and bounded
  fixed-space admission. Automatic per-launch groups and synthesized/default
  launcher grants remain outside it.
- **Spaces:** how does subgroup containment interact with fixed group spaces,
  space creation and launch-capable administrative/service grants, without an
  escape from the parent's lifetime or an implicit authority increase?
- **Capability-valued environment:** how are bound launchers and supervision
  capabilities selected, attenuated, copied/inherited or withheld through
  [capability entries](capability-environment.md), configuration and namespaces?

The minimal slice alone is assigned. Broader design and implementation still
require a separate owner go; consumer links grant no additional scope.
