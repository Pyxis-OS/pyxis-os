# ABI model, core archive and updater

The [0.0.10 upgrade milestone](pkg-upgrade-0.0.10.md) replaces this delivered
updater's previous-version boot entry, package predecessor and rollback policy.

Status: **ABI/core/program-package implementation delivered; updater
QEMU-qualified, native upgrade gate open.** This design for chronium
(2026-10-22) replaces the earlier
`pkg upgrade` plan: the release package with programs and the set staging of
pyxis-os #1095 and userland #338, and the self-contained archive of #1102.

- Choices marked *(chronium, 2026-10-22)* are agreed. No decisions are open.
- The delivery record below distinguishes implemented steps from remaining
  qualification and publication work; it does not authorize future tasks.
- T1–T3 are implemented and retained. Step 2 is complete; Step 3 records signed
  repositories, signing/hosting machinery, `pkg upgrade` and install-image
  generation. The ThinkPad upgrade check remains unchecked. Implemented hosting
  and image machinery alone does not establish deployment or publication.

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
70. **No Wi-Fi Install in 0.0.8:** Install uses wired or dock Ethernet only.
    A baked `network.lua` profile cannot join Wi-Fi: it has no SSID or key,
    only the interactive `wifi` command associates, and the install space has
    no shell. The interactive Wi-Fi join (0.0.9) comes as a step of the future
    curses TUI installer. *(chronium, 2026-10-23)* For that join, the install
    space already gets Wi-Fi READ and CONNECT, and install mode applies
    the world-domain channel policy: `WIFI_INSTALL_POLICY` accepts country
    `00`, wireless-regdb's world domain, the same rules
    [RTL8922AE](rtl8922ae-wifi.md) uses when no country is configured, and
    boot-init installs `00` with channels 1-13 on the Install entry only. On
    AX200 `00` is no country hint; the firmware LAR profile stays
    authoritative, so channels 12 and 13 transmit only where it marks them
    active. The installed system uses its own `wifi_policy`.
    *(chronium, 2026-10-23)*
71. **Signed repository indexes.** Each publish writes an immutable
    `index/N.lua` and `index/N.lua.sig` (DER ECDSA P-256 over SHA-256 of the
    exact bytes) beside a mutable `current` naming N. The index names its
    `repository`, `serial`, `signed` time (UTC) and `signer` certificate file.
    Archives are covered by the hashes the signed index holds. pkg trusts only
    the pinned Pyxis Software Signing CA, shipped in the core archive; the leaf
    must be CA:FALSE with Code Signing, and both certificates valid at the
    `signed` time. pkg refuses a lower serial or an earlier `signed` time than
    the repository's last accepted index, and on first contact a `signed` time
    before the image's `signed_floor` (its build commit time, in
    `os-release`). The signer script runs only on chronium's machine, with the
    key fetched by `op` through a pipe. *(chronium, 2026-10-23)*
72. **Unsigned repositories** need `signed = false` in their `packages.lua`
    entry, which only development images write. pkg refuses an unsigned
    index otherwise, and the `releases` repository is always verified.
    *(chronium, 2026-10-23)*
73. **One release package.** A release is `pyxis@VERSION` of kind `release` in
    the `releases` repository: the kernel, the core archive and that build's
    `os-release`, byte-for-byte. Its index entry carries `provides`, the
    release's ABI list, so the review knows which installed packages would not
    run before anything is downloaded. *(chronium, 2026-10-23)*
74. **Upgrade authority, source and staging.**
    - The installed `pyxis` space opts in with `package_upgrade = true`, and
      only pkg's manifest declares the boot ESP grant and the boot evidence.
      Installs made before this keep their own `boot.lua` and need the line
      added.
    - Every image lists `releases` at
      `https://packages.pyxis.nexus/releases/`, always signed;
      `RELEASE_REPOSITORY=` overrides it, and an empty value leaves it out.
      Only `pkg upgrade` reads it.
    - The fetched release is staged in `packages/.upgrade/`, removed after
      publishing and at the start of the next upgrade. The ESP's current and
      previous entries are the only retained copies.
    - `tmp://` carries FILESYSTEM_INFO, so pkg can confirm the publisher's
      workspace there is RAM-backed.

    *(chronium, 2026-10-23)*
75. **The install ISO is a release artifact.** Users cannot build Pyxis, so
    they install from it, in QEMU first.
    - It is published at `https://packages.pyxis.nexus/images/pyxis-VERSION.iso`,
      built from the release tag, in the same build as the release package.
    - Beside it is `SHA256SUMS`, which chronium signs with the release signer
      as `SHA256SUMS.sig`, using the sibling script `tools/sign-images.sh`.
    - `images/` also serves `signer-2026.pem` and `pyxis-signing-ca.pem`, and
      pyxis.nexus shows the CA fingerprint.
    - Every ISO stays published, with its line kept in `SHA256SUMS`.
    - The Worker caches the ISOs as immutable. `SHA256SUMS` and its signature,
      which change together, get 30 seconds, and the certificates an hour.

    *(chronium, 2026-10-23)*
76. **Install consent paths.** Classic Install offers two paths, and both
    end in the same typed consent word.
    - **Blank or marked disks**, the default, admits a blank disk (no partition
      table), or an npfs disk whose every live volume has a regular
      `SAFE_TO_WIPE`.
    - **Any disk (may erase data)**, formerly Read the room, also admits the
      risky cases: a foreign or unrecognized GPT, GPT copies that disagree, and
      a damaged, empty or partially marked pool.
    - A readable pool with no markers stays refused on both paths.

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

### Step 2: programs out of the archive (implemented)

Complete (2026-10-23). The archive holds only the core; every other program is
a package, and Install fetches them.

- **Packages for every program** (#1112): one recipe per program in
  `packages/programs/`, `packages/base.lua` listing the base set, and
  `make base-packages` / `make development-packages` packing them from one build.
- **Configuration on the pool** (2a, #1119 / userland #346): boot-init reads
  `boot.lua`, `session.lua` and `network.lua` only from `system://config/` on
  installed boots, binding the pool through `mount.pool=` (an accepted interim,
  see [revisit points](#revisit-points)), and starts the built-in rescue space
  when they are missing or broken ([init](../userland/init.md#installed-configuration)).
- **The ESP configuration set retired** (2b): `config.record` keeps only boot
  state; earlier records are refused, so 0.0.8 installs fresh
  ([installer](../userland/installer.md#boot-publication)).
- **`pkg://` and `sdk://`** (2c, through #1147): the `dev` kind, `pyxis-dev`,
  the kernel-served views with stable identity, and hosted Clang on `sdk://`
  ([sdk-view.md](sdk-view.md), [packages](../interfaces/packages.md)).
- **Core archive and `bin://`** (3a, #1150 / userland #360): `DISTRIBUTION=core`
  is the default and its ISO offers Rescue and Install; installed `bin://` is
  the packages' command view; Install and Update copy the archive as built
  ([system layout](../userland/system-layout.md#programs)). Installs from
  before it repair from Rescue
  ([repair](../userland/system-updates.md#installs-from-before-the-core-archive)).
- **Online Install** (3b, #1159 / userland #362): the Install entry's install
  space, `installer-init`, fetches `base` with `pkg` before publishing the boot
  entries, over wired or dock Ethernet
  ([init](../userland/init.md#install-boot-selection),
  [installer](../userland/installer.md)). Natively checked on the T14 at
  6cf76415: the world Wi-Fi policy is accepted and `pkg check base` passes over
  wired.

### Step 3: the updater

Hosting is a B2 bucket behind a Cloudflare Worker at
`https://packages.pyxis.nexus/`, with `public/` and `releases/` trees whose
object keys equal the URL paths. Uploads and Worker deploys happen only after
chronium's OK for each publish. Tasks, one PR pair each:

- [x] **1. Signed repositories** (decisions 71 and 72): `pxpkg publish` writes
  the index pair and `current`; pkg reads
  `current`, verifies signed indexes and refuses rollbacks; `signed = false`
  for development repositories; `signed_floor` in `os-release`; the CA in the
  core archive. Validated in QEMU with a throwaway test PKI: mbedTLS accepts the
  non-self-signed intermediate CA as the only trust anchor, as openssl's
  `-partial_chain` does.
- [x] **2. The signer script**, `tools/sign-repository.sh`, which only chronium
  runs. Validated with a stub `op` and a throwaway test key against a local
  stand-in for the published host.
- [x] **3. The Worker** and its `wrangler.toml`
  ([hosting/worker](../../hosting/worker/README.md)); deployed after
  chronium's OK. Not deployed yet.
- [x] **4. `pkg upgrade` and `make release-package`** (decisions 73 and 74): the
  `release` kind and `provides`; refusals on
  live media, rescue and missing boot evidence (T3); review with the packages
  `provides` lacks; publish through T1 under T3's grant, previous entry by T2.
  - **Validated in QEMU** with two locally tagged test releases and a signed
    test `releases` repository: Install of the old release, `pkg upgrade`,
    then booting the new release, the previous entry and rescue.
  - **Evidence and staging:** the boot evidence gained a `pool` line, the
    boot entry's `mount.pool`. The verified payloads are copied into RAM for
    the publisher, because FILE_TRANSFER to the ESP takes RAM sources only.
- [ ] **5. Native check:** a publish, then `pkg upgrade` from the previous
  release on the ThinkPad.
- [x] **6. The install ISO** (decision 75): `make release-image`,
  `tools/sign-images.sh`, and `images/` in the Worker. It is not published
  yet; the first ISO is 0.0.8's.

Programs upgrade as ordinary packages; only at a deliberate ABI break does the
upgrade review list installed packages the new release no longer runs.

## Base package set

The core archive (decision 18) is what `boot/rescue.list` names: boot-init,
init-shell, session, textfs, the repair tools, pkg, net, wifi, wifi-supplicant,
httpfs, http-stage, installer and installer-init. Every other program is a
package; `packages/base.lua` lists the base set Install fetches, and the
development programs (benches, test inits and examples) are packages outside
it. Install writes the space definitions and init scripts to `system://config/`
once (decision 14). There is no Wi-Fi Install in 0.0.8 (decision 70).

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
