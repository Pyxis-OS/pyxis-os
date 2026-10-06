# System layout and boot init

Status: **milestone, agreed 2026-10-05 and 2026-10-06.** It follows the
[runtime SMP milestone](../kernel/smp.md). Tasks 1 and 2 are implemented. Each task
starts when the owner says so, after its listed decisions are settled. Any
decision can be revised by the owner.

## Goal

Pyxis is not meant to be a read-only OS. A small boot archive holds what is
needed to boot and rescue the system, and everything else lives in writable,
updatable volumes. Spaces and their inits must be easy to change, including
adding a space, without rebuilding boot media.

## Today

- Boot init creates the spaces from the archive's Lua
  [boot configuration](../userland/init.md#boot-configuration). An installed
  system can add or replace spaces through `system://config/boot.lua`, and its
  rescue boot entry ignores that file.
- All programs, configuration and shared files are in the boot archive, `boot://`.
  `tmp://` is a RAM directory, and `system://` is the installed pool's `system`
  volume. `app://` and `home://` are unbound; programs that still default to
  `home://` fail until task 4 ([technical debt](../technical-debt.md#system-layout-renames)).
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
  as the new-space flow. Creating a space becomes an ordinary operation for a
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

- [x] **1. Renames.**
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

- [x] **2. Boot init and space creation.**
  - **Kernel:** a native create-space operation, held only by boot init. It
    takes the name, CPU ceiling, init image, and the new init's roots and
    grants. The kernel command line keeps only the boot init and `mount.disk`;
    the `space.NAME` grammar and the `SPACES` build options are removed.
  - **Boot init:** reads the archive's default Lua configuration and the
    optional pool override, mounts each pool once, creates the spaces, drops its
    grants and exits. An invalid or missing override falls back to the default
    with a message.
  - **Decisions, accepted by the owner on 2026-10-06** (revised after the #434
    review):
    - **Configuration shape.** Two archive defaults, `boot://config/live.lua`
      and `boot://config/installed.lua`. Boot init uses the installed one when
      `mount.disk` is bound. Each returns a table of named `volumes` and a list
      of `spaces`. An npfs volume names its partition and volume; a virtio-fs
      export is its own kind. A root is an access string, or a table when it
      is optional:

      ```lua
      -- installed.lua
      return {
        volumes = {
          system = { kind = "npfs", partition = 2, volume = "system" },
        },
        spaces = {
          { name = "pyxis", title = "Pyxis", init = "boot://init-installed",
            network = true,
            cpus = { 1, 2, 3 },                       -- omitted: every CPU
            roots = { system = "read-write" } },
        },
      }

      -- live.lua (excerpt)
      return {
        volumes = {
          host = { kind = "virtio-fs" },
        },
        spaces = {
          { name = "development", title = "Development", init = "boot://init",
            network = true,
            roots = { host = { access = "read-write", optional = true } } },
        },
      }
      ```

      The `host` volume replaces today's per-init host mounts. `SPACES` and
      `SPACE_CPUS` are removed; development builds edit `live.lua`. Only
      installed boots read the pool override, `system://config/boot.lua`. It
      merges by name: an entry with a default's name replaces it whole, new
      names follow the defaults, and nothing can be removed. The override
      cannot redefine the `system` volume it is read from; doing so makes it
      invalid (owner, 2026-10-06). Because the override can replace a default
      space with an unusable one, the rescue boot entry below, not the merge,
      is the guaranteed way back. Space inits stop mounting and receive the
      roots their entry lists.
    - **Missing volume.** A root can be marked optional. A missing or
      unmountable required volume leaves that space created but not started,
      with the reason on its tab, as an absent CPU does today; other spaces
      start. An invalid override (syntax, unknown key, bad CPU set) is ignored
      whole in favour of the default, reported on the Caelum tab and serial; a
      missing volume does not make it invalid.
    - **Boot init's space and rescue.** Boot init runs in the Caelum space,
      which the kernel still creates first, so its messages appear in the log
      view and on serial. If no configured space starts, boot init creates a
      `rescue` space with the factory and starts a shell there with only
      `boot://` and `tmp://`. It has no disk authority and cannot repair the
      pool.
    - **Create-space operation.** A `space_factory` resource with a CREATE
      right, issued by the kernel only to boot init, which closes it before
      exiting. Space inits never receive it. The call takes the space's name,
      title and CPU set, and then either an ordinary launch request (image and
      script, argv, named resources, roots and working directory) or an
      **unstarted reason**. With a reason, the kernel creates the space with no
      CPUs and shows the reason on its tab, as `report_unstarted()` does today.
      Absent CPUs and missing volumes are reasons boot init supplies, not
      kernel rejections. The kernel adds only the new space's own devices:
      console, keyboard, pointer, display and the space handle. Boot init
      forwards everything else and does not forward `native_mount` or
      `host_mount`, so mount authority stays with it. Spaces are still never
      destroyed.
    - **Network owner.** A space entry may set `network = true`; more than
      one makes the configuration invalid. Boot init forwards `net_config`
      WRITE and UDP broadcast authority only to that space, and READ with
      ordinary UDP to the others. Its init still runs `session
      --configure-network`, and the DHCP maintainer stays in that space. This
      turns today's single-owner convention into authority. A configuration
      without an owner is valid and leaves the network unconfigured (owner,
      2026-10-06). Starting setup
      from boot init in the Caelum space was rejected for now: it would leave a
      long-lived, unsupervised process there. If an override replaces the
      owner space with a broken one, the rescue entry restores networking.
      `network.lua` stays in the archive for now
      ([technical debt](../technical-debt.md#archive-only-network-configuration)).
    - **Command line.** Normal boot is `init=boot://boot-init.pxe
      mount.disk=GUID`. The install entry is `init=boot://init-install.pxe
      boot.install=1`; `init-install` becomes a boot init that creates the
      install space and forwards the raw-disk grants to it.
    - **Rescue entry.** Installed systems get a second Limine entry,
      `Pyxis OS (rescue)`, that ignores the pool override and boots the archive
      default. "Rescue" rather than "default" keeps it from reading as the
      usual choice. It follows the normal `Pyxis OS (Caelum)` entry, which stays
      first and is booted on timeout. The installer and Update write both
      entries. It restores the archive configuration, not the pool: if
      `system` cannot be mounted, the `pyxis` space stays unstarted and the
      fallback `rescue` space has only `boot://` and `tmp://`.
      - **Option:** `boot.default_config=1`. The kernel accepts it only once,
        with value 1, and passes it to boot init as an argument; boot init
        then skips `system://config/boot.lua`.
      - **Timeout:** 3 seconds, so the entry is reachable. Every installed boot
        waits that long at the menu unless a key is pressed.
      - **Update:** its installed-configuration check accepts exactly this
        two-entry form. A single-entry installation from before task 2 takes
        the existing rebuild path.
  - **Later:** write access to `system://config/boot.lua` chooses which inits
    run with forwarded grants on the next boot. The
    [users milestone](users-and-authority.md) should treat it as
    administrative.
  - **Finish when:** the installed ThinkPad gains a second space by editing the
    pool configuration, with no Update, and a broken override boots the default.
  - **Implemented:** see [init](../userland/init.md#boot-configuration) for the
    configuration and [space creation](../userland/init.md#space-creation). In
    QEMU, an installed disk gained three override spaces without an Update,
    and broken and unusable overrides, the rescue entry and Update were
    checked. On 2026-10-06 the owner ran the ThinkPad check with the PXE
    build of this PR (update from 0.0.2, spaces from the pool configuration)
    and reported that it works.

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

## After the milestone

- **Configuration checker** (owner idea, 2026-10-06; deferred until this
  milestone is done). A native shell command that reads the boot configuration
  and reports problems without blocking anything, for use while editing it:
  more than one network owner, invalid volumes, CPUs that are not present.
  Proposed, not yet agreed:
  - share parsing and validation with boot init, so the two cannot disagree;
  - separate static checks from checks against this machine's CPUs and
    volumes, and say "not checked" where it lacks the authority to look;
  - show the merged result: replaced entries, and which spaces would start.

## Still open

- **Writes:** write, deletion and copy semantics wherever overlays are still
  used, deferred to that work at the latest.
- **A final program update scheme** to replace the interim revision directories.

## Related

The [filesystems and namespaces draft](vfs.md) holds the older overlay and
publication ideas. Its read-only shared base no longer matches this direction.
See also [spaces](spaces.md), the [installer](../userland/installer.md) and
[system updates](../userland/system-updates.md).
