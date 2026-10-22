# ABI model, core archive and updater

Status: **design for chronium (2026-10-22).** It replaces the earlier
`pkg upgrade` plan: the release package with programs and the set staging of
pyxis-os #1095 and userland #338, and the self-contained archive of #1102.

- Choices marked *(chronium, 2026-10-22)* are agreed.
- [Open decisions](#open-decisions) have proposed defaults.
- Everything else is proposed.
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
   - Everything else is a package: Neovim, Links, TCC and the other tools.
   - An everything-included or offline install image is a **distribution**, a
     special case with its own local repository.
10. **Installs are online by default.** The installer sets up the core, then
    installs the base package set from the network repository. *(chronium,
    2026-10-22)*
11. **Releases come from a separate `releases` repository tree**, beside
    `public/` and `personal/`. Archives are copied verbatim. *(chronium,
    2026-10-22)*

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
  - A new job, `abi`, finds the last release tag reachable from HEAD and diffs
    `include/abi/` against it.
  - Removed or changed lines fail, unless the release is gone from
    `os-release`'s list. Added lines and added files pass.
  - The check is textual, so a comment edit in a listed header also counts as a
    change.
- **ABI rules**
  - The rules above go into a new `docs/interfaces/abi.md`.
  - The remaining fixed replies without a capacity (DESCRIBE, FORWARDER_CREATE,
    in technical debt) are fixed before 0.0.8 is tagged.

### Step 2: programs out of the archive

- **The core archive**
  - Keeps only the boot and recovery set ([open decision 1](#open-decisions)).
  - The installer stops filtering and repacking it; it is copied verbatim.
  - The pool binding moves to the owned entry's command line as `mount.pool=`, an
    accepted interim (see [revisit points](#revisit-points)).
- **Programs**
  - Every other program becomes a package, packed by `pxpkg` from the build and
    published per release ([open decision 2](#open-decisions)).
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
- **Update**
  - Replaces the kernel and the core archive.
  - Packages stay; newer builds come through ordinary `pkg` upgrades.
- **boot-init**
  - Binds `bin://` to the command view on installed boots, and to the archive on
    live boots.
  - The rescue entry and the archive-rescue fallback keep using the core archive
    alone.

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

The core archive keeps what boot, rescue and installation need. Everything else
becomes a package, and the base set is what an online Install puts on a new
system.

| Group | Programs (today's archive roots) |
| --- | --- |
| **Core archive** (proposed default) | the rescue list (boot-init, init-install, installer, init-shell, session, textfs, httpfs, http-stage, cat, cp, echo, hostname, log, lsusb, lspci, ls, mkdir, rm, rmdir, mv, sync, head, vi), plus shell, mux, pkg, net, wifi, wifi-supplicant, config and fat32ctl |
| **Base packages** | Neovim, Links, TCC, Lua 5.4 and 5.1, kilo, less, jq, fastfetch, the small tools (cksum, date, grep, ln, printf, sha256sum, sleep, sort, tail, tar, tee, tree, uniq, wc), the network tools (dig, ping, http, tcp, udp-send), remote-terminal and xfer, screenshot and screenshot-saver, acpidump |
| **Development packages, not in the base set** | allocbench, iobench, ipcbench, threadprobe, counter, mandelbrot, mousetest, pointer-min, pcm, client, server, ttcp, udp-echo, the fat32 test inits |

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
  ([open decision 3](#open-decisions)).
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

- **`mount.pool=` on the command line** (accepted interim, chronium dislikes it).
  Revisit when boot-init can identify the pool on `mount.disk` without a menu
  parameter, from its GPT type and pool marker. At the latest, revisit before a
  second pool on one disk is supported.

## Open decisions

1. **The core archive.**
   - Default: the rescue list plus shell, mux, `pkg`, net, wifi,
     wifi-supplicant, config and fat32ctl, as in the table.
   - Everything else becomes a package.
2. **Package granularity.**
   - Default: one package per program or bundle, named after it (`nvim`, `links`,
     `tcc`, `lua`).
   - The small tools form one `base-tools` package.
   - A `base` meta package lists the base set by dependency, so Install and
     `pkg install base` use one name.
3. **Install's repository.**
   - Default: the live image's configured repositories. Development images name
     horse's `public` tree; release images name the 0.0.8 public mirror once
     signing exists.
   - Without network, Install refuses before consent and points to an offline
     distribution.

## Implemented so far

- T1, the shared publisher library (#1065 / userland #328).
- T2, the previous menu entry (#1076 / userland #333).
- T3, the ESP-write authority (#1070 / userland #331).

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
   session and network settings on rollback.
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
