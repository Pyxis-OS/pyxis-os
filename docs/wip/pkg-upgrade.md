# ABI model, core archive and updater

Status: **design for chronium (2026-10-22).** It replaces the earlier
`pkg upgrade` plan: the release package with programs and the set staging of
pyxis-os #1095 and userland #338, and the self-contained archive of #1102.

- Choices marked *(chronium, 2026-10-22)* are agreed. No decisions are open.
- Everything else is proposed, and nothing is implemented until chronium has
  reviewed the whole document.
- T1–T3 are implemented and stay.

## Decided

1. **The kernel and libc are not tied by build revision.** Compatibility is the
   kernel ABI in `include/abi/`. *(chronium, 2026-10-22)*
2. **The ABI is stable only at releases** (0.0.7, 0.0.8, …). Between releases
   it changes in place, and only what a test needs is rebuilt. There are no ABI
   identities, levels, renumbering, reserved slots or compatibility shims during
   development. *(chronium, 2026-10-22)*
   - Example of what not to do: renumbering `HANDLE_INFO` from 5 to 9 (`9f9c29f3`).
     It stays at 9 with no further churn.
3. **A release's `include/abi/` at its tag is the contract.** The next release
   only adds to it, unless a break is deliberate and listed in the release notes.
   *(chronium, 2026-10-22)*
4. **`boot://os-release` names the release and lists the release ABIs the
   kernel supports**, for example `abi = { "0.0.7" }`. A development change that
   breaks compatibility with a listed release removes that release from the list
   in the same PR. *(chronium, 2026-10-22)*
5. **Packages name the release ABI they were built for.** `pkg` and launch
   accept a package when the running system lists that ABI. A package built from
   a development tree only promises to work on that exact build, which is today's
   rule, now limited to development. *(chronium, 2026-10-22)*
6. **CI compares `include/abi/` with the last release tag**, not with main. It
   flags removed or changed lines unless `os-release` no longer lists that
   release. Parallel PRs never collide on it. *(chronium, 2026-10-22)*
7. **Reply buffers carry their capacity**, so ABI structures grow by adding
   fields. *(chronium, 2026-10-22)*
8. **Full rebuilds and repacks are for permanent deploys only:** Install,
   Update and releases, never development testing. *(chronium, 2026-10-22)*
9. **Core and distributions.** *(chronium, 2026-10-22)*
   - Core Pyxis is the kernel, a small core archive (boot-init, shell, `pkg`,
     recovery tools) and `pkg`.
   - The live ISO holds just enough to install.
   - Everything else is a package: Neovim, Links, hosted Clang and the other tools.
   - An everything-included or offline install image is a **distribution**, a
     special case with its own local repository.
10. **Installs are online by default.** The installer sets up the core, then
    installs the base package set from the network repository. *(chronium,
    2026-10-22)*
11. **Releases come from a separate `releases` repository tree**, beside
    `public/` and `personal/`. Archives are copied verbatim. *(chronium,
    2026-10-22)*
12. **Inits and space definitions live on the pool**, in `system://config/`,
    owned and editable by the administrator. The core archive keeps only
    boot-init, a built-in rescue space and the tools rescue needs; the init
    scripts leave it. *(chronium, 2026-10-22)*
13. **What is on the pool is what runs.** The archive has no default spaces and
    nothing overrides them by name: today's merge of `installed.lua` defaults
    with `system://config/boot.lua` goes away. If the pool configuration is
    missing or broken, boot-init starts the built-in rescue space and says why;
    there is no stock-space fallback. *(chronium, 2026-10-22)*
14. **Install writes the initial space definitions and inits to the pool once**,
    from the release. After that they belong to the administrator, and Update
    never rewrites them. *(chronium, 2026-10-22)*
15. **Configuration changes are the administrator's.** When a release needs a
    space or configuration change, its release notes say so and the
    administrator makes it: Update, edit with Neovim, reboot. Update has no
    diff or delta machinery. If someone breaks their system, they did it;
    Polaris comes later. *(chronium, 2026-10-22)*
16. **Granularity.** Every tool is its own package, installable on its own;
    there is no bundle of small tools. `base` is a real meta-package: a
    dependency list with nothing of its own. Removing `base` removes only the
    meta-package, and its members stay installed. *(chronium, 2026-10-22)*
    While `base` is installed, removing one of its members is refused under
    `pkg`'s exact-dependency rule; to drop tools, remove `base`, whose members
    stay, then remove them. *(chronium, 2026-10-22)*
17. **No design against the administrator bricking their own system.** Pyxis
    is very early. The simple built-in rescue path is the answer to a broken
    configuration; there are no validators, refusal layers or guards against
    administrator edits. *(chronium, 2026-10-22)*
18. **The core archive** is what rescue needs, as listed in the
    [base package set](#base-package-set): the rescue session (boot-init,
    `init-shell.pxe`, session, textfs), the repair tools, `pkg` with net, wifi,
    wifi-supplicant, httpfs and http-stage, and the installer with
    init-install. `init-shell.pxe` stays as a program, since it is the rescue
    init and the rescue shell; only the init scripts leave (decision 12).
    `shell.pxb` and `mux` become packages. *(chronium, 2026-10-22)*
19. **Install's repository** is the live image's configured repositories:
    development images name horse's `public` tree, and release images name the
    0.0.8 public mirror once signing exists. Without network, Install refuses
    before consent and points to an offline distribution. *(chronium,
    2026-10-22)*
20. **Development and live images.** `make image` builds the core archive,
    which releases, Install and the live ISO carry. An opt-in
    `DISTRIBUTION=full` image keeps every program, the init scripts and
    today's live spaces in the archive, for QEMU and stick work: a
    distribution in the sense of decision 9. Install copies whichever archive
    it booted from, verbatim. *(chronium, 2026-10-22)*
21. **The initial space definitions and init scripts** are files under
    `share/installer/system/` in the core archive. boot-init never reads them;
    Install copies them to `system://config/` once. *(chronium, 2026-10-22)*
22. **No migration for existing installs.** After an Update to step 2, an
    existing install boots the rescue space. The release notes give the rescue
    commands, `cp boot://share/installer/system/* system://config/` (the shell
    expands the wildcard) and `pkg install base`, or say
    to reinstall. *(chronium, 2026-10-22)*
23. **Shared data in step 2.** `sdk/` becomes the `pyxis-dev` package, the
    base of the [`sdk://` view](sdk-view.md) (amended by decisions 32 and 34,
    chronium, 2026-10-22; TCC is retired). `share/zoneinfo` stays in the core archive, since
    `session` reads it. `share/examples` becomes an `examples` data package,
    and `share/checks` with the iobench fixtures a `checks` development data
    package. *(chronium, 2026-10-22)*
24. **Session and network settings live on the pool too**, in
    `system://config/`, administrator-edited like spaces: configuration on an
    ESP that the running system cannot mount or modify is another assumption
    about the running system. The ESP configuration set is retired: its
    `boot.lua`, `session.lua` and `network.lua`, the record, catalog and
    readiness machinery for them, and the `config` editor's ESP publishing.
    The ESP `boot.lua` goes entirely, with no format-preserving leftover. This
    supersedes decision 4 for these settings. *(chronium, 2026-10-22)*
25. **`mount.pool=N`** names the pool by its one-based GPT partition number.
    The kernel parses it and passes `--pool N` to boot-init. *(chronium,
    2026-10-22)*
26. **A pool volume without a `partition`** in the pool configuration means
    "on the pool", so the files under `share/installer/system/` work on any
    partition layout. *(chronium, 2026-10-22)*
27. **Install writes the initial `session.lua` and `network.lua`** to
    `system://config/` once, like the space definitions (decision 21).
    *(chronium, 2026-10-22)*
28. **Machines installed before task 2b reinstall.** Task 2b slims
    `config.record` without reading the earlier format, so Update refuses on
    them. Back up `home`, reinstall, and the release notes say so. *(chronium,
    2026-10-22)*
29. **A disk bound without `mount.pool`**, as in `MOUNT_DISK` development
    builds, reads `boot://config/live.lua`, whose `npfs` volumes name their
    partitions. This keeps the USB and toolchain-disk workflows. *(chronium,
    2026-10-22)*
30–62. **The package namespace and the `sdk://` view** — `pkg://NAME`, the
    view's layout, its packages and compilers, and TCC's retirement — are in
    [sdk-view.md](sdk-view.md). *(chronium, 2026-10-22 and 2026-10-23)*
63. **Install runs in a boot-init space.** The Install entry boots boot-init
    with the archive's `config/install.lua`: one install space with network
    (DHCP and Wi-Fi as today, and httpfs) and a live-only installer capability
    that delegates the disks, the boot kernel and archive, power and the space
    factory. The installer runs `pkg` against the target's `packages` volume to
    install `base`, fetches the index before consent and refuses without
    network. `init-install` goes. *(chronium, 2026-10-23)* The space factory
    and `config/install.lua` are superseded: the capability has no space
    factory (66), and boot-init builds the install space in, with
    `installer-init` as its init (69).
64. **The core ISO boots Install and Rescue only.** `live.lua`, its spaces and
    the init scripts exist only in `DISTRIBUTION=full` images.
    *(chronium, 2026-10-23)*
65. **The image's repository is built in.** `PACKAGE_REPOSITORY` is written into
    the archive's `config/packages.lua`: development images default to horse's
    `public` tree, and release images to `https://packages.pyxis.nexus/public/`
    once signing exists. A development image installs only if `base` for its
    exact ABI is published there; otherwise Install refuses before consent,
    naming the ABI. *(chronium, 2026-10-23)*
66. **The installer capability** is the `disks` resource with the install
    rights, `boot_kernel`, `boot_archive` and `power` narrowed to restart; the
    install space gets no space factory, since boot-init creates it.
    *(chronium, 2026-10-23)*
67. **The installer runs `pkg`.** `pkg check ID` reads only the index, needs
    no packages volume, and prints the version of ID that matches the running
    ABI or fails naming the ABI; Install runs it before Review. `pkg --yes`
    switches without asking. After the pool stage the installer launches
    `pkg.pxb` with the target's `packages` root, `boot://config` and the install
    space's HTTP and HTTPS providers, and publishes the boot entries only after
    `base` is installed. *(chronium, 2026-10-23)*
68. **The development repository** is `http://192.168.0.213:5245/public/`, a
    LAN address because local DNS is behind Tailscale. `PACKAGE_REPOSITORY=URL`
    overrides it and an empty value builds an image with no repository. It is
    one entry named `public` in the archive's `config/packages.lua`; step 3
    sets the release value. *(chronium, 2026-10-23)*
69. **`installer-init`.** The install space's init is a native `installer-init`
    holding the installer capability itself, so its rights never pass through
    a shell or session and selected sources keep their contract. It brings up
    the network with session's runtime, moved into a shared library, launches
    `httpfs.pxb` itself and receives its provider roots through a shared
    `provider_setup_receive()` that the shell also uses, then runs the
    selector and installer, granting them the providers, which the installer
    passes to `pkg`. It outlives the installer. Without network Install
    refuses before consent; Update is still offered. It replaces the kernel
    mode of `init-install`. *(chronium, 2026-10-23)*
70. **Wi-Fi in Install, 0.0.8:** only through a `network.lua` profile built
    into the ISO; otherwise wired or dock Ethernet. An interactive Wi-Fi join
    in the installer is 0.0.9. *(chronium, 2026-10-23)* Install mode applies
    the world-domain channel policy: `WIFI_INSTALL_POLICY` accepts country
    `00`, wireless-regdb's world domain, the same rules
    [RTL8922AE](rtl8922ae-wifi.md) uses when no country is configured, and
    boot-init installs `00` with channels 1-13 on the Install entry only. On
    AX200 `00` is no country hint; the firmware LAR profile stays
    authoritative, so channels 12 and 13 transmit only where it marks them
    active. The installed system uses its own `wifi_policy`.
    *(chronium, 2026-10-23)*

## Order of work

There are no interim layouts between these steps. Each step is one or more PR
pairs with QEMU checks.

### Step 1: the ABI model

**Today.** `abi` is the exact 12-hex build revision. `pkg` (`pkg/main.c`) and
launch (`lib/command_selection.c`) compare it with `system_info`'s
`build_revision`.

**After step 1:**

- **Archive assembly**
  - Writes `boot://os-release` from a source file, `boot/os-release.lua`, which
    holds the maintained list:

    ```lua
    return { release = "0.0.8", revision = "1a2b3c4d5e6f", abi = { "0.0.7", "0.0.8" } }
    ```

  - `release` is the tag name when HEAD is exactly a release tag, and absent
    otherwise.
  - `revision` is the build revision. The `abi` list is the only part a PR edits.
- **`pkg` and launch**
  - Read `os-release` instead of `system_info`.
  - The provided ABIs are the `abi` list plus the build's own revision, which
    serves development packages.
  - The existing hidden-build count and refusal lines stay.
- **The packer**
  - `pxpkg pack --build DIR` takes `abi` from the build's `os-release`: the
    release name for a tagged release build, otherwise the build revision.
- **CI**
  - A new job, `abi`, diffs `include/abi/` against the tag of every release
    `os-release`'s list names.
  - Removed or changed lines fail, and so do renamed headers. Added lines and
    added files pass.
  - The check is textual, so a comment edit in a listed header also counts as a
    change.
- **ABI rules**
  - The rules above go into a new `docs/interfaces/abi.md`.
  - The remaining fixed replies without a capacity (DESCRIBE, FORWARDER_CREATE)
    take one, changed in place; FORWARDER_CREATE's request stays fixed until it
    grows (chronium, 2026-10-22).

### Step 2: programs out of the archive

Tasks, one PR pair each:

- [x] **1. Packages for every program.** One recipe per program in
  `packages/programs/`, and `packages/base.lua` listing the base set.
  `make base-packages` and `make development-packages` pack them from one
  build. A recipe with a variant is versioned `VARIANT+LABEL` for that build; a
  dependency may name only an ID, which any version satisfies, so `base` never
  pins its members' versions; a package may have no files. The archive still
  carries the programs.
- [x] **2a. Configuration on the pool.** The kernel passes `mount.pool=N` as
  `--pool N`; boot-init reads `boot.lua`, `session.lua` and `network.lua` only
  from `system://config/`, and starts the built-in rescue space when they are
  missing or broken; Install writes them from `share/installer/system/` once.
  - Implemented in #1119 / userland #346
    ([init](../userland/init.md#installed-configuration)): the
    pool's `boot.lua` is the whole configuration, including hostname fallback,
    input defaults and the boot network profile; `system://` inits such as
    `system://config/init-installed` open from the pool. The rescue space
    holds the pool's `system://` read-write and owns the network with the
    archive's profile; package management waits for `pkg` in the core
    archive (task 3). Install and Update no longer read the pool file.
  - A disk bound without `mount.pool` reads `live.lua` (decision 29).
  - Installs from before 2a already need decision 28's reinstall: their menu
    entries lack `mount.pool`, so Update does not recognize them as its own
    and refuses or leaves activation pending.
- [x] **2b. Retire the ESP configuration set** (decision 24): its files, the
  catalog and readiness, the `config` editor's ESP publishing, and the
  configuration parts of `config.record`; `config validate` checks the pool
  files. Dead since 2a and removed here: the installer's pool-override
  handling in `installer/configuration.c` (`pool_override`, `pool_valid`,
  `pool_disposition`, `migrate_pool`, `pool_conflict` and the legacy migration),
  the record's always-absent `legacy_override` and `legacy_disposition`, the
  `override` argument of `boot_plan_build`, `libconfig/selection.c`, and
  `config/installed.lua`.
  - Implemented in this task's PRs ([installer](../userland/installer.md#boot-publication),
    [`config validate`](../userland/configuration-editor.md)). `config.record`
    keeps its path and `PYXIS-CONFIG 1` header and holds only the state, the
    transaction, the current and previous boot bindings and the menu's
    previous and obsolete recovery copies. An earlier record is refused, so
    Update refuses and those machines reinstall (decision 28). The installer's
    `--scratch-publication` front end went with the set; pending completion
    stays.
  - 0.0.8 is a fresh install: its [release check](../releases.md) installs
    fresh with no update step, and its notes say to back up `home` and
    reinstall. *(chronium, 2026-10-22)*
- [ ] **2c. `pkg://` and the `sdk://` view** ([sdk-view.md](sdk-view.md),
  decision 33): the `pyxis-dev` package from `boot://sdk`, the `dev` kind, the
  kernel-served view and hosted Clang's default paths. Beside it, delta's
  pkgconf port ([pkgconf](sdk-view.md#pkgconf)).
- [ ] **3. Core archive and `bin://`.**
  - **3a, core archive and `bin://`: implemented.** `DISTRIBUTION=core` (the
    default) builds the core archive from `boot/rescue.list`, which gains `pkg`,
    `net`, `wifi` and `wifi-supplicant`; its ISO offers Rescue and Install only.
    `DISTRIBUTION=full` keeps every program, the init scripts and the live
    spaces. Installed boots bind `bin://` to the packages' command view;
    programs that opened `bin://NAME.pxe` use command search; session names
    its services script, and Install seeds `init-services`. The rescue space
    runs the archive alone and, on an installed boot, gets package management
    and HTTP staging on the pool. Install and Update copy the archive as built;
    Update removes old `bin/REVISION` copies except the previous entry's
    ([system layout](../userland/system-layout.md#programs),
    [init](../userland/init.md)). An install from before task 3 that is
    updated has no shell until reinstalled or repaired from Rescue; chronium
    chose no compatibility path (2026-10-23,
    [repair](../userland/system-updates.md#installs-from-before-the-core-archive)).
  - **3b, online Install: implemented.** The Install entry boots boot-init,
    which starts one built-in `install` space whose init, `installer-init`,
    holds the installer capability (decision 66), applies the archive's
    network profile with the network runtime now shared with session, and
    launches `httpfs.pxb` itself (decision 69). Install runs `pkg check base`
    before choosing a target and `pkg --yes install base` into the new pool
    after the pool stage, publishing the boot entries only after that
    (decision 67). Without network it refuses and returns to the actions,
    where Update and Replay remain. The image names its repository through
    `PACKAGE_REPOSITORY` (decision 68). `init-install` is gone
    ([init](../userland/init.md#install-boot-selection),
    [installer](../userland/installer.md)).

- **The core archive**
  - Keeps only boot-init, the built-in rescue space and its tools
    (decision 18). The init scripts (`init`,
    `init-installed`, `init-services`, `init-readonly`, `init-remote`,
    `init-remote-services`) leave it.
  - The installer stops filtering and repacking it; it is copied verbatim.
  - The pool binding moves to the owned entry's command line as `mount.pool=`, an
    accepted interim (see [revisit points](#revisit-points)).
- **Programs**
  - Every other program becomes a package, packed by `pxpkg` from the build and
    published per release, one package per tool (decision 16).
- **`bin://`**
  - Becomes the command view over the installed packages. That view already
    exists as the set's command index; the core archive stays `boot://`.
  - `bin/REVISION` on the pool disappears.
  - Update deletes `bin/` once the previous attempt no longer reads it: an attempt
    from before step 2 still binds `bin/REVISION`.
- **Install**
  - Writes the core to the ESP and creates the pool.
  - Then installs the base set by running `pkg` with the target's `packages`
    volume as its management source.
  - It fetches the index before consent and refuses without network
    ([installer networking](#installer-networking)).
  - Writes the initial space definitions and init scripts from the release to
    `system://config/` once (decision 14). The release carries them as a
    package or files of their own, not as archive defaults.
- **Update**
  - Replaces the kernel and the core archive.
  - Packages stay; newer builds come through ordinary `pkg` upgrades.
  - Never touches `system://config/` (decision 15).
- **boot-init**
  - Reads spaces only from `system://config/` on installed boots. The
    archive-default merge, the override-by-name and the stock-space fallback
    go (decision 13).
  - A missing or broken pool configuration starts the built-in rescue space,
    with the reason printed.
  - Binds `bin://` to the command view on installed boots, and to the archive on
    live boots.
  - The rescue entry and the built-in rescue space use the core archive alone.

### Step 3: the updater

- **A release** is a `release`-kind entry in the `releases` tree. Its archive
  holds exactly the kernel and the core archive.
- **`pkg upgrade`**
  - Refuses on live media, a rescue boot or missing boot evidence (T3).
  - Takes the last release, or the one named, and refuses if it is the one
    running (`os-release`).
  - Fetches it and verifies the index hash.
  - Shows a `[y/N]` review.
  - Admits the booted ESP once, and publishes the kernel and core archive
    verbatim through the T1 publisher. The new entry becomes current and the
    current one becomes previous (T2).
  - Ends with `upgrade staged: reboot to run RELEASE`.
- **Programs upgrade as ordinary packages.** Only at a deliberate ABI break does
  the review list the installed packages whose ABI the new release no longer
  provides, and therefore need new builds.
- **Trust.** Until signing, every repository is unsigned by configuration and the
  review says so; REFUSED stays final.
- **Code.** This is the small T4: the release kind in `pkg` and `pxpkg`, the
  `releases` tree, and `pkg upgrade` calling the publisher under T3's grant.

## Base package set

The core archive keeps boot-init, the built-in rescue space and what rescue
needs. Everything else is a package of its own (decision 16); `base` lists the
base set, which an online Install puts on a new system.

What rescue needs today, from code inspection:

- **Start a rescue session:** boot-init; `init-shell.pxe`, which is both the
  rescue space's init (`RESCUE_INIT` in boot-init) and the session's
  interactive shell when no init script is given (`session/main.c`);
  `session.pxe`; `textfs.pxe` for the text service.
- **Inspect and repair files:** cat, cp, echo, head, hostname, log, ls, lspci,
  lsusb, mkdir, mv, rm, rmdir, sync and vi, the current rescue list.
- **Reinstall packages and install online:** `pkg`, `net`, `wifi` and
  `wifi-supplicant`, plus `httpfs` and `http-stage`. The rescue list has no
  network tools today, so rescue cannot fetch.
- **Install from the live ISO:** `installer.pxe` and `installer-init.pxe`. The
  live ISO and installed systems share one core archive, copied verbatim.

| Group | Programs (today's archive roots) |
| --- | --- |
| **Core archive** (decision 18) | boot-init, init-shell, session, textfs, the repair tools above, pkg, net, wifi, wifi-supplicant, httpfs, http-stage, installer and installer-init |
| **Base set** (listed by `base`, one package each) | shell, mux, config, fat32ctl, Neovim, Links, Lua 5.4 and 5.1, kilo, less, jq, fastfetch, cksum, date, grep, ln, printf, sha256sum, sleep, sort, tail, tar, tee, tree, uniq, wc, dig, ping, http, tcp, udp-send, remote-terminal, xfer, screenshot, screenshot-saver, acpidump |
| **Space definitions and init scripts** | written to `system://config/` by Install (decision 14) |
| **Development packages, not in `base`** | allocbench, iobench, ipcbench, threadprobe, counter, mandelbrot, mousetest, pointer-min, pcm, client, server, ttcp, udp-echo, font-min, fat32-inspect, fat32-write, the fat32 test inits |

## Installer networking

- **Wired DHCP works today:**
  - the T14's built-in RTL8111;
  - QEMU's virtio-net;
  - USB [CDC-NCM](../devices/usb-cdc-ncm.md) adapters, including the RTL8156
    2.5G USB-A adapter, which has run on vela under Pyxis since mid-October.
- **vela** has no built-in Ethernet and its RTL8922AE Wi-Fi is unsupported, so
  it installs online over that RTL8156 adapter.
- **Wi-Fi:** the T14's AX200 has WPA2.
- **The repository** comes from the live image's configured repositories
  (decision 19).
- **Without network**, Install refuses before consent, says which step failed
  (no link, no address, repository unreachable), and points to an offline
  distribution. It writes nothing.

## Rollback

- **The previous menu entry** boots the old kernel and core archive with the
  current packages. That is valid while the old release's `os-release` lists the
  ABI the installed packages name.
- **After a deliberate break**, packages rebuilt for the new release no longer
  launch on the previous entry; launch refuses them by name. The core archive's
  programs still run there.
- **The rescue entry** stays the fallback when that is not enough.

## What this drops

- **From #1095 / userland #338 (T4a):**
  - the release package that carried programs;
  - moving `bin/` into the store;
  - retention by boot record;
  - dirty and local release versions;
  - the release inventory rules;
  - `libpkgstore`.

  The provenance check on packed releases carries over to step 3's packing.
- **From the earlier T5:**
  - the `next` marker;
  - init promotion;
  - set selection by release;
  - `pkg rollback` across releases.
- **From #1102:** the archive that carries every program. Kept from it:
  `os-release`, the verbatim copy, `mount.pool=` and the T1–T3 reuse.
- **From today's code:**
  - the exact-revision ABI gate (step 1);
  - the installer's archive filter and repack, `bin/REVISION` and the program
    stage (step 2).

On closing: #1102, #1095 and userland #338 close when chronium has read this.
The userland Makefile fix that tracks `pkg`'s header dependencies goes in as its
own small PR.

## Revisit points

- **Install onto existing volumes.** A later installer option installs the core
  and the configuration while keeping an existing pool's `home` and other
  volumes, so a format change such as decision 28's does not force a `home`
  wipe. Not part of step 2.

- **`mount.pool=` on the command line** (accepted interim, chronium dislikes it).
  Revisit when boot-init can identify the pool on `mount.disk` without a menu
  parameter, from its GPT type and pool marker. At the latest, revisit before a
  second pool on one disk is supported.

## Implemented so far

- T1, the shared publisher library (#1065 / userland #328).
- T2, the previous menu entry (#1076 / userland #333).
- T3, the ESP-write authority (#1070 / userland #331).
- Step 1, the ABI model: the release ABI list, `boot://os-release`, `pkg` and
  launch reading it, the packer's `abi`, and the `abi` CI job (#1105 / userland
  #342, merged; [ABI](../interfaces/abi.md)). The list starts empty. DESCRIBE
  and FORWARDER_CREATE replies carry a capacity, and the ABI enums have
  explicit values (#1109 / userland #343), which finishes step 1.

The booted-ESP proof and the grant contract are in
[FAT32 mounts](../devices/fat32-mounts.md) and [init](../userland/init.md).

### Earlier decisions (T1–T3)

Accepted by the owner, 2026-10-20, all as proposed:

1. **Previous entry in the menu.** The shared publisher adds a third owned entry,
   `Pyxis OS (previous)`, for the retained previous attempt, written and removed
   with the same ownership rules as normal and rescue, for Install, Update and
   `pkg upgrade`. It is what makes "boot the previous entry" a rollback.
2. **ESP-write authority.** A new one-use `UPGRADE_BOOT_ESP` issuer right on the
   boot-provenance issuer, bound to the reviewed identity facts, with no raw disk
   reach. Rejected: delegating `DISKS` and `MOUNT_READ_WRITE` to `pkg`, which gives
   a package manager the whole-disk inventory.
3. **Release-tied packages the release does not list.** Each is upgraded to the
   same-ID build for the new release from the repository recorded at install. A
   package with no such build is listed as a removal that needs explicit
   acceptance, and declining cancels the upgrade. Superseded: programs upgrade as
   ordinary packages (decision 5 below).

Accepted by chronium, 2026-10-21:

4. **Configuration on a previous-entry boot.** boot-init uses the committed ESP
   set when the loaded kernel and archive match `previous_boot` and the previous
   archive's trusted catalog equals the selected catalog. Otherwise it uses the
   archive defaults and prints why. Rejected: archive defaults always, which loses
   session and network settings on rollback. Superseded by decision 24
   (chronium, 2026-10-22): these settings live on the pool, which every menu
   entry reads.
5. **Scope of the RW grant.** `ADMIT_RW` returns the kernel's existing owned-ESP
   confinement, as `DISKS_OPEN_OWNED_ESP` does: `EFI/pyxis`, `boot/limine` and
   `boot/limine.conf`. `pkg` never reaches `EFI/BOOT` or other loaders' files.
   Rejected: a whole-ESP root.
6. **Recognizing the replaced previous entry.** `config.record` keeps its
   format. Pending completion and recovery own a marked previous entry in the
   replaced menu when that whole menu equals the digest-recorded previous
   `limine.conf`. This relaxes "markers alone do not establish ownership" for that
   entry, and existing installs need no manual step (chronium, 2026-10-21).
   Rejected: a format 2 record with a retiring binding, which an older attempt
   booted from the previous entry could not read.
