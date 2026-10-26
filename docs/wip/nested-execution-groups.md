# Nested execution groups: broader direction

Status: **the minimum subgroup foundation is implemented** (#788, #789, userland
#248; owner exception to the planning freeze, 2026-10-13), and so is one slice of
the direction below: shell-script launch parity (owner, 2026-10-15), in which a
script in a `launch = true` space delegates a contained launcher to its foreground
commands ([script mode](../userland/shell.md#script-mode)). The rest of the broader
required direction remains parked. Scoped subgroup creation from LAUNCH, launcher
ancestry checks, subtree stop and cleanup, and the depth and count bounds are
specified in [execution groups](../interfaces/execution-groups.md). The
[native command core](../userland/native-jobs.md) and the
[Clang driver](../development/hosted-tools.md#process-boundary-and-driver-contracts)
use it for contained jobs. Qualification is in the
[driver record](../development/experiments/hosted-clang-driver/README.md#native-subgroup-foundation-2026-10-13).

## Required direction

- Any group member can create a subgroup inside its own group. A subgroup cannot
  outlive or escape its parent: stopping the parent stops the whole subtree, and
  parent cleanup completion covers it too.
- A launched program receives a launcher bound to its own group by default, making
  child launch normal. The parent can deliberately withhold it for a sandboxed
  profile, as the command core and the Clang LLD leaf do today. Launch installs only
  explicitly supplied grants; nothing adds a launcher automatically.
- Authority follows the grants the child receives, always equal to or narrower than
  its parent's. Permission to start children confers no extra directory, device,
  network, administrative or cross-space authority. Ancestry containment and grant
  attenuation are separate protections.
- This is Pyxis-native containment and delegation, not `fork`, POSIX process
  hierarchies or signals. Tree cleanup keeps the honest safe-stop and
  deferred-cleanup boundaries, without promising bounded retirement or rollback.

Packaged live Remote delegates same-group LAUNCH to foreground children (owner,
2026-10-11), enabling Neovim helpers while keeping session and pane cleanup;
installed Remote entries opt in with `launch = true`. The outside-group executor
and sibling job-group pattern once proposed for HTTP are removed; nesting is the
required route for ordinary descendant execution. The
[remote-mux pane broker](../userland/remote-terminal.md#pane-broker-and-cleanup)
could later shrink once mux creates contained pane subgroups itself. Surviving
private `job_work` cleanup belongs with [Continuum](spaces.md#asterism)
(owner, 2026-10-12), revisiting the
[caller-failure RAM retention limit](https://git.internal/PyxisOS/pyxis-os/issues/1329).

## Consumers waiting on it

- [Git on Pyxis](../userland/git.md): any Git tool or remote helper that starts
  children, beyond the direct no-descendant profile.
- [Source builds](source-builds.md) and the [ports runner](../development/ports.md):
  native make to cc and recipes whose tools launch children.
- [Neovim](later-os-directions.md#neovim-follow-ups): descendant jobs and their
  contained cleanup, beyond ordinary direct `uv_spawn`.
- Hosted Clang beyond one LLD job, such as external cc1.

## Broader questions still parked

Return for owner review before implementing any of these; consumer links do not
extend the foundation's scope.

- **Default grouping:** when an ordinary launched program's group is selected or
  created beyond explicit subgroup creation, and whether each launch creates one.
- **Spaces:** how the direction interacts with space teardown and cross-space
  services. The foundation fixes the space and refuses grouped space creation.
- **Capability-valued environment:** how bound launchers and supervision
  capabilities are selected, attenuated, copied, inherited or withheld through
  [capability entries](capabilities-and-providers.md), configuration and namespaces.
