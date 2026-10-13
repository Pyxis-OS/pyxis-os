# Nested execution groups

Status: **minimum subgroup foundation implemented in this PR, 2026-10-13;
broader REQUIRED direction remains parked.** The owner explicitly excepted native
subgroup creation, ownership and subtree cleanup from the planning freeze as the
prerequisite for Clang driver linking. Its native contracts are in
[execution groups](../interfaces/execution-groups.md). Keep the first
[native command milestone](../userland/native-jobs.md) on direct launch with no
descendant launcher. This direction is required before descendant execution in
the consumers below; it is not a prerequisite for their independent first slices.

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
hierarchies or signals. [Execution groups](../interfaces/execution-groups.md) now have scoped subgroup
creation, launcher ancestry checks and transitive stop/cleanup. Launch installs
only explicitly supplied grants; it does not add a launcher automatically.
Required default launcher delegation does
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

## Accepted minimum foundation (owner, 2026-10-13)

The [Clang driver contracts](hosted-clang.md#driver-invoked-lld-accepted-and-assigned-2026-10-13)
authorize the minimum foundation first, with Daybreak's adversarial kernel review
before merge, then a separate driver integration PR:

- LAUNCH permits scoped CREATE_GROUP: a member creates within its own group and
  fixed space; an ungrouped local caller creates an owned root in its own space.
  Retire the redundant privileged CREATE_GROUP right. The creator stays outside
  the subgroup and owns CONTROL|WAIT plus its bound LAUNCH handle atomically.
- Launch/capability/COPY/IPC checks admit only own-group or descendant launchers;
  ancestor, unbound and foreign escapes refuse. Grouped space creation refuses
  before effects. Own-group launcher delegation remains explicitly declared,
  with sandbox withholding; no implicit kernel grants or automatic per-launch
  subgroup creation.
- Parent stop and cleanup cover the whole subtree, including admitted creation
  and deferred cleanup. Storage-only references do not prolong execution.
  Tunable bounds are depth 16 and 256 allocated groups per space, including
  retiring/retained storage; admission returns LIMIT before creation effects.
- A contained job's creator retains sole non-forwarded CONTROL. Normal job wait
  observes the process result and subgroup cleanup. PROCESS_WAIT remains
  process-only, so caller fault observation may precede child deferred cleanup;
  parent-group completion is transitive.

After the foundation the owner can create and inspect a contained native job
subtree without changing Clang. This assigns no broader Continuum, space teardown,
capability-environment or default-grouping redesign.

## Broader questions still parked

The accepted fixed-space foundation does not settle the broader direction:

- **Default grouping:** when/how should an ordinary launched program's group
  be selected or created beyond explicit subgroup creation? The accepted slice
  deliberately retains explicit grouping and declared launcher grants.
- **Spaces:** how does the broader direction interact with space teardown and
  cross-space services? The accepted slice fixes space and refuses grouped
  space creation rather than assigning a new lifetime/authority family.
- **Capability-valued environment:** how are bound launchers and supervision
  capabilities selected, attenuated, copied/inherited or withheld through
  [capability entries](capability-environment.md), configuration and namespaces?

The minimum foundation is authorized above. Return for owner review before
implementing the remaining direction; consumer links do not extend its scope.
