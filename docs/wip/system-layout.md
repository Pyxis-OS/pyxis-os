# System layout and boot init

Status: **agreed direction, 2026-10-05**, for work after the
[runtime SMP milestone](scheduling-and-threads.md). Nothing here is implemented,
and it authorizes no code or placeholder APIs. Agreed choices and open questions
are listed separately below. Any decision can be revised by the owner.

## Goal

Pyxis is not meant to be a read-only OS. A small boot archive holds what is
needed to boot and rescue the system, and everything else lives in writable,
updatable volumes. Spaces and their inits must be easy to change, including
adding a space, without rebuilding boot media.

## Today

- The kernel creates spaces from `space.NAME=IMAGE` options on the kernel command
  line ([SMP task-1 decision 1](scheduling-and-threads.md#task-1-decisions)).
  An installed system always gets the single space the installer writes.
- All programs, configuration and shared files are in the boot archive, `app://`.
  `home://` is a RAM directory, and `system://` is the installed pool's `system`
  volume.
- [Update](../userland/system-updates.md) replaces only the ESP and never writes
  the pool.

## Agreed direction

### Boot init

- The kernel starts one trusted **boot init** from the boot archive. The kernel
  command line only names it and the trusted disk binding.
- Boot init reads the boot configuration and mounts each pool once. It creates
  the configured spaces through a native space-creation operation and starts
  each space's init with the handles and CPU ceiling that space needs. It then
  drops its own grants. It does not unmount: the spaces keep using what it
  mounted.
- The same space-creation operation later serves runtime space creation, such
  as the new-tab flow. Creating a space becomes an ordinary operation for a
  holder of that authority, not boot-only kernel configuration.
- This replaces the `space.NAME` command-line grammar once implemented. The
  space registry and CPU ceilings from SMP tasks 2 and 3 remain; only the caller
  of space creation changes.

### Configuration

- The configuration stays **Lua**, read by boot init. YAML waits until service
  monitoring and supervision exist.
- The boot archive carries a default configuration, which is also the rescue
  configuration. An optional configuration on the system pool adds or overrides
  spaces and mounts, so adding a space needs no Update.
- If the pool configuration is missing or invalid, boot init falls back to the
  archive default and says so.

### Rescue archive and writable system

- The boot archive contains what boot init needs plus a rescue set.
- Ordinary programs that boot does not need move to a writable system volume.
- Program lookup searches the system volume first, then the rescue archive.
  System binaries need no overlay.

### Two-stage Update

- Update gains a second stage that writes the system programs into the pool.
  This **reverses** the current rule that Update never writes the pool.
- **Order:** stage the new programs first and switch the ESP last, so a failure
  never leaves a new kernel running old programs. The rescue set must always be
  able to run Update again.

### Names

| Name | Meaning |
| --- | --- |
| `app://` | Reserved for future [application bundles](vfs.md#application-bundles). |
| `system://` | The writable system volume. |
| `home://` | Before users: a persistent home volume. With users, a virtual volume that resolves to the logged-in user's home. See [users and authority](users-and-authority.md). |
| `tmp://` | Today's RAM filesystem, which is now `home://`. |
| Raw volume access | Addressed by **volume name**, not by an index, and granted as a **capability** to rescue and administrative sessions only. |

## Open questions

- **Names:** the boot archive's new name, and whether ordinary programs live in
  `system://` or a separate `bin://` volume.
- **Rescue set:** its contents, for example shell, installer, fsck, mount tools
  and an editor.
- **Mounts:** how the configuration names pools and volumes, and what happens
  when a configured volume is missing at boot.
- **Program updates:** the second-stage update unit (the whole program set or
  individual programs), staging and the switch, and recovery after interruption.
- **ABI between stages:** the kernel/program ABI compatibility rule between the
  two Update stages.
- **Boot init lifetime:** one-shot, or a persistent space manager that serves
  later space creation.
- **Home before users:** whether one home volume is shared by all spaces.
- **Writes:** write, deletion and copy semantics wherever overlays are still
  used. These are deferred to that work at the latest.
- **Existing installs:** migrating an installation that has the current single
  `system` volume and archive-only programs.

## Suggested order

This is a proposal, not tasks.

1. Rename the archive scheme and `home://` to `tmp://`, reserving `app://`.
2. Add boot init and the Lua boot configuration, and move space creation out of
   the kernel command line.
3. Move ordinary programs to the system volume, with lookup order and two-stage
   Update.
4. Add the persistent home volume.

## Related

The [filesystems and namespaces draft](vfs.md) holds the older overlay and
publication ideas. Its read-only shared base no longer matches this direction.
See also [spaces](spaces.md), the [installer](../userland/installer.md) and
[system updates](../userland/system-updates.md).
