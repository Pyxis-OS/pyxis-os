# System layout and boot init

Status: **milestone, agreed 2026-10-05 and 2026-10-06.** It follows the
[runtime SMP milestone](../kernel/smp.md). Nothing here is implemented. Each task
starts when the owner says so, after its listed decisions are settled. Any
decision can be revised by the owner.

## Goal

Pyxis is not meant to be a read-only OS. A small boot archive holds what is
needed to boot and rescue the system, and everything else lives in writable,
updatable volumes. Spaces and their inits must be easy to change, including
adding a space, without rebuilding boot media.

## Today

- The kernel creates spaces from `space.NAME=IMAGE` options on the kernel command
  line ([boot selection](../userland/init.md#boot-selection)).
  An installed system always gets the single space the installer writes.
- All programs, configuration and shared files are in the boot archive, `app://`.
  `home://` is a RAM directory, and `system://` is the installed pool's `system`
  volume.
- [Update](../userland/system-updates.md) replaces only the ESP and never writes
  the pool.

## Decisions

Accepted by the owner on 2026-10-06:

1. **Archive name:** the boot archive is `boot://`.
2. **Programs:** ordinary programs live in a separate `bin` volume, `bin://`, not
   in `system://`.
3. **Program updates:** one program directory per revision, selected by the
   running kernel's revision. This is **interim**, until a more final update
   scheme replaces it.
4. **Boot init lifetime:** boot init exits after setup. When runtime space
   creation arrives, it can become a persistent space manager.

## Agreed direction

### Boot init

- The kernel starts one trusted **boot init** from the boot archive. The kernel
  command line only names it and the trusted disk binding.
- Boot init reads the boot configuration and mounts each pool once. It creates
  the configured spaces through a native space-creation operation and starts
  each space's init with the handles and CPU ceiling that space needs. It then
  drops its own grants and exits (decision 4). It does not unmount: the spaces
  keep using what it mounted.
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

- The boot archive, `boot://`, contains what boot init needs plus a rescue set.
- Ordinary programs that boot does not need move to the writable `bin` volume.
- Program lookup searches `bin://` first, then `boot://`. Programs need no
  overlay.

### Two-stage Update

- Update gains a second stage that writes the system programs into the pool.
  This **reverses** the current rule that Update never writes the pool.
- **Order:** write the new programs first and switch the ESP last. The rescue
  set must always be able to run Update again.
- **Revision directories (decision 3):** each revision's programs go in their
  own directory on the `bin` volume. Boot init selects the directory matching
  the running kernel's revision, so the ESP write is the only switch. An
  interrupted Update never pairs a kernel with another revision's programs, and
  the previous revision's directory stays until it is cleaned up.

### Names

| Name | Meaning |
| --- | --- |
| `boot://` | The boot archive: boot init, the default configuration and the rescue set. |
| `bin://` | The `bin` volume's directory for the running revision's programs. |
| `app://` | Reserved for future [application bundles](vfs.md#application-bundles). |
| `system://` | The writable system volume. |
| `home://` | Before users: a persistent home volume. With users, a virtual volume that resolves to the logged-in user's home. See [users and authority](users-and-authority.md). |
| `tmp://` | Today's RAM filesystem, which is now `home://`. |
| Raw volume access | Addressed by **volume name**, not by an index, and granted as a **capability** to rescue and administrative sessions only. |

## Tasks

- [ ] **1. Renames.**
  - The archive root becomes `boot://`, and today's RAM `home://` becomes
    `tmp://`. `app://` stays unbound and reserved. Shells start in `tmp://`
    until task 4.
  - Scope today: the initial roots in `kernel/user/launch.c`, the shell's
    bare-command lookup (`userspace/shell/directory.c`), the image check in
    `scripts/configure-boot.sh`, `limine.conf`, init shebangs, Update's
    installed-configuration check, about 30 userspace and 10 ports files, and
    the docs.
  - **Finish when:** a QEMU boot runs programs by bare name and by `boot://`
    path, `tmp://` works as the working directory, nothing binds `app://`, and
    Update moves a 0.0.2 installation to the renamed layout with `system://`
    preserved.

- [ ] **2. Boot init and space creation.**
  - **Kernel:** a native create-space operation, held only by boot init. It
    takes the name, CPU ceiling, init image, and the new init's roots and
    grants. The kernel command line keeps only the boot init and `mount.disk`;
    the `space.NAME` grammar and the `SPACES` build options are removed.
  - **Boot init:** reads the archive's default Lua configuration and the
    optional pool override, mounts each pool once, creates the spaces, drops its
    grants and exits. An invalid or missing override falls back to the default
    with a message.
  - **Decisions before starting:**
    - the configuration's shape: spaces, CPU sets, inits and mounts;
    - what happens when a configured volume is missing at boot;
    - the create-space operation's arguments and authority.
  - **Finish when:** the installed ThinkPad gains a second space by editing the
    pool configuration, with no Update, and a broken override boots the default.

- [ ] **3. Programs on `bin://` and two-stage Update.**
  - The installer creates the `bin` volume, and ordinary programs move out of
    the archive into a revision directory. Boot init binds `bin://` to the
    running revision's directory, and lookup searches `bin://`, then `boot://`.
  - Update writes the new revision's directory, then the ESP.
  - **Decisions before starting:**
    - **Adding a volume to an existing pool.** The installer writes only a
      `system` volume today, and nothing can add one later. The candidate is a
      journaled npfs create-volume operation, which task 4 would reuse.
    - **Mounted pool and raw ESP access.** Writable raw access is refused
      while a disk's pool is mounted (`kernel/storage/disk_access.c`), mounting
      is refused while the disk is claimed (`kernel/fs/npfs.c`), and there is
      no unmount. Update needs both, in order. Candidates are raw access scoped
      to the ESP partition, or a pool unmount.
    - **The rescue set:** for example the shell, installer, fsck, mount tools and
      an editor.
    - **Cleanup** of older revision directories.
  - **Finish when:** the 0.0.2 stick updates to the new layout, ordinary
    programs run from `bin://`, an Update interrupted after the program stage
    still boots the old revision, and the rescue set can run Update again.

- [ ] **4. Persistent home.**
  - A `home` volume mounted as `home://`, created by the installer and added to
    existing pools with task 3's operation.
  - **Decision before starting:** whether every space shares one home volume
    before users exist.

## Still open

- **Writes:** write, deletion and copy semantics wherever overlays are still
  used, deferred to that work at the latest.
- **A final program update scheme** to replace the interim revision directories.

## Related

The [filesystems and namespaces draft](vfs.md) holds the older overlay and
publication ideas. Its read-only shared base no longer matches this direction.
See also [spaces](spaces.md), the [installer](../userland/installer.md) and
[system updates](../userland/system-updates.md).
