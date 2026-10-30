# Packages with pkg

Programs and development files come as packages. `pkg` lists, installs and
removes them, from the repositories your system is set up to use. The
examples show `BUILD` where your output shows the identifier of your Pyxis
build.

## See what is installed

```text
home://> pkg status
set 1: 38 packages
  base@1+BUILD
  acpidump@1+BUILD
  cksum@1+BUILD
  ...
```

`pkg list` prints the same list. Each change you make creates a new numbered
set of packages, and the number goes up each time.

## See what is available

`pkg list --available` reads configured package repositories and prints one
line per package ID, showing the build installation would select for your
system. Multiple sources appear as alternatives on that line; listing does not
choose one. The `releases` repository is reserved
for `pkg upgrade`:

```text
home://> pkg list --available
public/allocbench@1+BUILD program abi BUILD 152064 bytes
...
public/libpng-dev@1.6.59+BUILD dev abi BUILD 1306112 bytes
```

Each line is the repository, the package name and version, its kind, the
build it was made for and its size. Add `| grep NAME` to find one package.

To see older eligible builds, use `pkg list --available --all-versions`.
Add `| grep fonts` to narrow that output. Exact published versions remain
available with `pkg check fonts@VERSION` or `pkg install public/fonts@VERSION`.

To check one package without installing it:

```text
home://> pkg check libpng-dev
pkg: public/libpng-dev@1.6.59+BUILD is offered for this system, ABI BUILD
```

If no repository offers it, `pkg check` says so and exits with an error.

## Development packages

A package whose name ends in `-dev` holds what you need to compile programs
against a library: headers, static libraries and `pkg-config` files. Its files
are not run; they join `sdk://`, where the compiler finds them without extra
flags. `pyxis-dev` is the C library and headers, and the other `-dev` packages
add libraries such as `zlib-dev` and `libpng-dev`. List them with:

```text
home://> pkg list --available | grep -- -dev
public/pyxis-dev@1+BUILD dev abi BUILD 5323264 bytes
...
```

## Install

1. Install by name. `pkg` fetches the package and the packages it needs, then
   shows what it would change and asks before it does it:

   ```text
   home://> pkg install libpng-dev
   libpng-dev  fetch  1.2 MiB  ...
   libpng-dev  verify  1.2/1.2 MiB  100%
   libpng-dev  extract  11/11 files  100%
   zlib-dev  fetch  454.0 KiB  ...
   Proposed package set (new launches only):
     add libpng-dev@1.6.59+BUILD (public)
     add zlib-dev@1.3.2+BUILD (public)
   Switch to this package set? [y/N]
   ```

   `libpng-dev` needs `zlib-dev`, so both are added.

2. Type `y` and press Enter. Any other answer changes nothing.

   ```text
   Switch to this package set? [y/N] y
   pkg: active set 2 (40 packages)
   pkg: libpng-dev is read as pkg://libpng-dev in spaces with a pkg root
   pkg: zlib-dev is read as pkg://zlib-dev in spaces with a pkg root
   ```

3. Use the package right away. Its files are under `pkg://NAME`, and the
   headers and libraries of a `-dev` package join `sdk://`:

   ```text
   home://> ls sdk://include | grep png
   png.h
   pngconf.h
   pnglibconf.h
   ```

Add `--yes` before the command (`pkg --yes install NAME`) to skip the question.

## Remove

`pkg remove` refuses to leave a package without something it needs:

```text
home://> pkg remove zlib-dev
pkg: libpng-dev 1.6.59+BUILD depends on zlib-dev, which the proposed set lacks; remove libpng-dev first to drop it
```

Name both to remove them together:

```text
home://> pkg remove libpng-dev zlib-dev
Proposed package set (new launches only):
  remove libpng-dev@1.6.59+BUILD
  remove zlib-dev@1.3.2+BUILD
Switch to this package set? [y/N]
```

The `base` package keeps the standard programs in place. Remove `base` first if
you want to remove one of them.

## Check the installed files

`pkg verify` checks every installed file against its recorded hash:

```text
home://> pkg verify
pkg: verifying date@1+BUILD
...
pkg: selected package files and indexes verified
```

## Repositories

`pkg repository list` shows where packages come from. Each line is a name and
a URL:

```text
home://> pkg repository list
public https://packages.pyxis.nexus/public/
releases https://packages.pyxis.nexus/releases/
```

On a release image, `public` is the Pyxis package repository at
`https://packages.pyxis.nexus/public/`. Its index is signed, and `pkg` checks
the signature before it trusts a package list. `public` supplies the packages
above. `releases` supplies new Pyxis versions: `pkg upgrade` fetches one (see
Updating with pkg upgrade).

To add a repository, give it a name and its URL. Add `--unsigned` for a
repository whose index is not signed, such as a test repository on your own
network:

```text
home://> pkg repository add --unsigned lab http://192.168.0.10:8080/public/
pkg: repository configuration updated
home://> pkg repository list
...
lab http://192.168.0.10:8080/public/ (unsigned)
```

`pkg` fetches the repository's index first and refuses an address that is not a
repository, or that is already configured under another name. Without
`--unsigned` it also refuses an index published under a different name:

```text
pkg: not a usable repository: http://10.0.2.2:8099/lab/: no complete download; active set unchanged
pkg: not a usable repository: http://10.0.2.2:8099/lab/: the index was published as repository public, not lab2
pkg: repository configuration unchanged
```

Remove a repository by name:

```text
home://> pkg repository remove lab
pkg: repository configuration updated
```

If two repositories offer the same package name, `pkg install NAME` asks which
one to use. Write `pkg install lab/NAME` to choose.

## Updating packages

`pkg update` reviews newer builds of every installed package. `pkg update
NAME...` selects only those names and any dependency changes they need; both
validate the complete proposed set. If nothing changes, it reports `already up
to date` without creating another set. `--yes` still cannot approve removal of
an installed package or choose among several possible origins for an older
originless package. The review summarizes packages adopted from their only
available repository.

## Updating with pkg upgrade

On an installed 0.0.10 system, `pkg upgrade [VERSION]` stages the new Pyxis
release together with a complete compatible package set. It ends with
`Upgrade staged; type reboot to finish`. The running system keeps its old
package set until the first verified boot of the new release. The command
does not restart the machine, and a pending upgrade blocks other package
changes. There is no previous-version boot entry or `pkg rollback`; rescue
and installer Update are the recovery paths if a new release fails to boot.

To move from **0.0.9 to 0.0.10**, boot the 0.0.10 installer and select
**Update**. The 0.0.9 `pkg` cannot stage that release transition. The
[package-upgrade reference](../userland/package-upgrades.md) explains boot
ownership, signed-index checks and current qualification limits.
