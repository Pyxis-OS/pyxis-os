# Package and release upgrades

Pyxis uses one active operating-system release or development build and one active package
set. Package changes can take effect for new lookups in the running system; a
release upgrade is staged and takes effect only after a verified reboot. The
[package format and installed-set records](../interfaces/packages.md) define
the on-disk data, while the [installer](installer.md) owns live-media Install
and Update.

## Package changes

`pkg update` with no arguments resolves every installed package from its
recorded repository origin. `pkg update ID...` selects the named packages and
any dependency changes they require, then validates the complete proposed set.
An unchanged plan prints `already up to date` and creates no new set. `pkg
install ID@VERSION` selects a specific package build; it does not roll back the
operating system.

An origin records the repository name, canonical URL and signed or unsigned
policy. A package set from before origin recording can adopt a source when
exactly one configured repository offers its ID. Review summarizes these as
`N packages adopted from NAME (only source)` per repository. Several possible
sources require an explicit choice, batched by repository; `--yes` cannot make
that choice. Proposed removals also need individual approval, including when
`--yes` was given. Failed preparation leaves the active set unchanged. After a
confirmed ordinary package change, new lookups see the new set; existing held
package handles keep their own lifetime.

On a boot-marked installation, a confirmed change records a durable transition
before switching the set, carries the same boot identity forward to the new
set's proof, and then clears the transition. If interrupted, the next verified
boot finishes that confirmed change before starting package consumers. An
uncertain result retains the writer lock and recovery evidence; reboot to
complete a pending transition, then clear an abandoned lock with `pkg unlock`
after the old client has stopped. A pending release transition still refuses
ordinary package mutations. Missing or conflicting proof is not repaired by
guessing: use rescue or installer Update.

## Sparse development repositories

A development build is named `BASE+REV`. From the 0.0.11 reader baseline, a
line-compatible build provides `{ BASE, BASE+REV }`: released packages remain
eligible, and packages built for this exact revision are eligible too. Before
the real 0.0.11 tag, and after an ABI break, only the exact dev ABI is provided;
released packages for the earlier base do not qualify. See the
[version and ABI contract](../interfaces/abi.md).

The dev repository is **sparse**: it contains only packages needed for a
particular development install or test, never a mirror of public. Configure it
as an ordinary separate source, for example:

```text
pkg repository add --unsigned dev http://192.168.0.213:5245/dev/
pkg install dev/ID
```

Use the signed form when that source has a reviewed signer. A plain `pkg install
ID` asks the existing per-package chooser when several configured repositories
offer an eligible build. Only the few packages that overlap the sparse dev
catalog need that choice. There is no repository-preference mechanism or
automatic dev-over-public source switch. `REPO/ID` makes the source explicit.
Within the chosen source, the rightmost eligible ABI in the provided list wins;
for `{ BASE, BASE+REV }`, this prefers the exact dev build. An explicit eligible
`ID@VERSION` selects that version instead.

The confirmed selection records its origin. `pkg update` follows that origin,
including its URL and signed/unsigned policy; a later offer from another source
does not change it. Use an explicit install/source review to change origins.

Exact dev packages become stale when REV changes, even when BASE is unchanged.
New installs cannot choose the old revision; an installed old dev package must
be replaced by an eligible build from its source or removed before a set that
keeps it can be verified. A compatible released-base package remains usable
without a dev rebuild. After an ABI break, the sparse dev catalog must contain
the builds/dependencies needed by that particular test/install; no released
base fallback is claimed until the next real release establishes the new base.

Keep `BASE+REV` entries out of stock `public` and `releases`: the old 0.0.10
reader rejects their ABI syntax before filtering the catalog, which would
strand its checkpoint route. Dev publication uses a separately named root.

## Upgrading the release

On an installed system with the configured upgrade authority, `pkg upgrade
[VERSION]` reads the signed `releases` repository, resolves a complete target
package set, and durably stages both that set and the new release's boot
payloads. It publishes only through the owned ESP view and its one-use issuer.
The operation ends with `Upgrade staged; type reboot to finish`; it does not
restart the machine or give `pkg` a general power grant. The running system
continues to read its old `packages/current`. Package mutations refuse while
the transition is pending.

An ABI change alone must never prevent an upgrade from a supported release to
the newest release. A supported origin is a retained release on the published
upgrade route, starting with 0.0.10. The route may require checkpoint releases
that supply a newer updater or an on-disk transition. Every hop resolves the
complete installed set against that hop's `provides`, rather than the running
ABI. Trust, dependency, ownership and actual-capacity failures still refuse;
incompatible old programs are never kept by pretending their ABI is supported.

The signed `releases` index declares the route in publication order. Its
`kind = "release"` entries use these IDs:

| ID | Meaning |
| --- | --- |
| `pyxis` | Historical releases and the permanent bootstrap checkpoint for the shipped 0.0.10 updater |
| `pyxis-checkpoint` | A required checkpoint after that bootstrap |
| `pyxis-release` | An ordinary release |

The next hop is the first checkpoint after the running release and before the
requested destination, or the destination when none intervenes. Without a
version, the destination is the final release in the index. With a version,
the same checkpoint rules apply; an earlier release is refused. Versions are
identities, not strings sorted as numbers. Duplicate versions across these IDs
and an unknown running release refuse before staging.

For a generated dev build, `upgrade_from` identifies its actual release
ancestor. The router places it immediately after that release, independently
of the ABI base, and honors every checkpoint toward the requested/latest real
release. The anchor and earlier releases are not upgrade destinations. Missing
or ambiguous ancestry refuses; if no later real release exists, pkg reports
that there is no newer released destination. Dev identities are never inserted
into the permanent stock route.

Each invocation reviews and stages one hop. Reboot to verify and activate it,
then run `pkg upgrade` again for the newest destination, or repeat the version
printed by Review to retain that destination. There is no unattended reboot or
persisted whole-route authorization. Each hop uses the actual boot proof,
current repositories, package availability and consent at that time.

The shipped 0.0.10 client understands only `pyxis` and selects its last entry.
Exactly one later release retains that ID permanently: the bootstrap containing
the route-aware updater. All subsequent releases use the other two IDs. This
keeps old clients landing on the bootstrap even after more checkpoints ship.
On 0.0.10 use plain `pkg upgrade` for that first hop; explicit requests for
later releases under the newer IDs are unavailable to that older selector.

On the first boot of the new release, boot-init checks the retained boot and
package-set proof, then switches `packages/current` before installed package
views or consumers start. Missing or ambiguous proof enters the core rescue
path rather than showing a mixed release and set. ESP and npfs cannot commit
as one physical transaction. The independent static rescue pair and
interrupted-publication recovery copies help recover a failed publication;
neither selects an earlier release. There is no `pkg rollback`, predecessor
set binding or `Pyxis OS (previous)` menu entry.

**Historical 0.0.9 to 0.0.10:** boot the 0.0.10 installer and choose **Update**. The 0.0.9
`pkg` cannot stage this transition; no bridge client is supplied. Installer
Update prepares a compatible complete package set before changing boot
selection, and preserves administrator-owned pool configuration and volumes.
See [system updates](system-updates.md) for target eligibility, repair and
interrupted-publication handling. A pending record has an explicit completion
Review; uncertain state is refused, without an automatic retry.

## Boot ownership and trust

Classic Install owns its reviewed ESP layout. Coexist Install and Update
preserve foreign partitions and bootloader files, publish Pyxis's pinned Limine
at `EFI/pyxis/BOOTX64.EFI` with normal and static rescue entries, and register
one `Pyxis OS` UEFI boot option after verifying the publication. The option is
appended to `BootOrder` without reordering foreign entries by default; Install
asks whether to move Pyxis first. An existing unrecorded Coexist install needs
one installer Update to gain the owned loader, option and activation record.
Missing evidence causes a boot warning and a specific `pkg upgrade` refusal;
conflicting evidence enters rescue. Only a configured typed `uefi` grant can
request the bounded structured firmware writes; ordinary programs have none.
The installer never edits Fedora's GRUB, `EFI/BOOT` on a Coexist target or
another loader's configuration.

At the next publication, cleanup removes only Pyxis-created payload directories
under `EFI/pyxis/` that the committed current record does not reference. It
preserves the static rescue pair and refuses ambiguous ownership instead of
deleting an uncertain directory.

Stock signed `public` and `releases` roots must pass signature verification,
then both the immutable image's reviewed serial/time/index-hash floor and the
packages volume's accepted-index record. The image floor is frozen from
chronium's controlled signing trees against the published copies before a
release; a network response alone cannot advance it. A missing stock floor
refuses first contact. Explicit unsigned development roots remain possible.
A user-added signed root has no reviewed snapshot import and retains the
build-time first-contact floor. See [signed indexes](../interfaces/packages.md#signed-indexes)
and the [release freeze procedure](../development/release-process.md#checking-the-repository-trees).

## Limits and qualification

The no-rollback policy is deliberate for pre-alpha: a failed new boot can
require rescue, installer Update or reinstall ([debt #1421](https://git.internal/PyxisOS/pyxis-os/issues/1421)).
The manual reboot after staging is tracked in
[debt #1511](https://git.internal/PyxisOS/pyxis-os/issues/1511), and the weaker
first-contact floor for user-added signed roots in
[debt #1510](https://git.internal/PyxisOS/pyxis-os/issues/1510).
The typed UEFI grant does not yet expose boot-entry listing or `BootNext`;
that post-0.0.10 tool and authority decision are tracked in
[debt #1512](https://git.internal/PyxisOS/pyxis-os/issues/1512).

Disposable QEMU/OVMF checks cover Classic 0.0.9 migration, owned Coexist
Install/Update, first-boot set selection, refusal on damaged evidence, and the
signed-index floor. The complete installed `pkg upgrade` and owned Coexist
firmware behavior are not yet qualified natively. The release-day vela update
is the next native gate ([debt #1509](https://git.internal/PyxisOS/pyxis-os/issues/1509));
one native pass will not establish a power-cut or torn-sector guarantee.
