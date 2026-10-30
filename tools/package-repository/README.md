# Horse package repository setup

Slice 1 task 6 deployed the repository on horse on 2026-10-19 from `880ff590` in
`~/src/pyxis-pkgrepo` (luna preparation, owner enable/ufw), served read-only at
`192.168.0.213:5245`. Stage 2 task 4 republished it in the current format on
2026-10-21 ([record](../../docs/wip/packages.md#task-4-trees-on-horse-2026-10-21)).

## Trees

| URL path | Format | Contents |
| --- | --- | --- |
| `public/` | current | `fonts`, `clang-hosted` |
| `personal/` | current | `doom` (Ultimate Doom 1.9), `quake` (registered 1.06), `duke3d`, `diablo` |
| `0.0.7/public/` | 0.0.7 | the public tree as 0.0.7 and 0.0.7a clients read it |
| `0.0.7/personal/` | 0.0.7 | the personal data packages as 0.0.7 and 0.0.7a clients read them |

- **0.0.7 and 0.0.7a systems** must use `http://192.168.0.213:5245/0.0.7/public/`
  and `http://192.168.0.213:5245/0.0.7/personal/`. The live `public/` and `personal/` trees
  are refused there as `invalid repository index`. The frozen copies are never
  repacked and are removed at 0.0.8.
- **Distinct IDs (chronium, 2026-10-21):** until named repositories (stage 2
  task 2b) let `pkg` choose, the two live trees offer no common ID, because
  `pkg` refuses every command when two configured repositories offer one ID for
  the running system. So `public/` carries no games; the shareware `doom` and
  `quake` join it with 2b.
- **Release-tied:** every `program` and `toolchain` package is built and packed
  from the clean checkout whose revision its `abi` records, the same revision
  as the system that runs it. See [packing a revision](#packing-a-revision).
- **Private inputs:** the personal recipes live in
  `~/.local/share/pyxis-private/games` (mode 700) and name the owner's data by
  absolute path. They and the data never enter a checkout, log, report or
  shared directory.

The checklist below is the deployment/recovery procedure. Use the existing
installed dufs; it adds no download, compiler rebuild or change to the existing
file server.

The second dufs instance serves two independent repository trees beneath one
root, `public/` and `personal/`, plus the frozen 0.0.7 copies (see
[trees](#trees)). Only the host packer writes them, replacing the index last.
Serving is read-only and LAN-bound; neither the personal tree nor this listener
is published to the Internet.

1. **Check the checkout and installed server.** From the Pyxis checkout, record
   `git rev-parse HEAD`, then `command -v dufs && dufs --version`. Use the owner's
   existing server binary; do not install from upstream. Set the shell variables
   below for this session, using horse's actual LAN address and trusted LAN CIDR.
   Port 5245 is an authored choice for the new instance; check it is free first.

   ```sh
   repo_root="$HOME/.local/share/pyxis-packages"
   pkg_bind=LAN_ADDRESS
   pkg_lan=LAN_CIDR
   pkg_port=5245
   pkg_dufs="$(command -v dufs)"
   ss -ltn "sport = :$pkg_port"
   ```

2. **Pack the trees.** Create the root privately, then follow
   [packing a revision](#packing-a-revision) for both trees.

   ```sh
   install -d -m 700 "$repo_root/public" "$repo_root/personal"
   ```

3. **Install the separate user unit, without starting it yet.** These paths
   should contain no newline or double quote. The environment file contains
   deployment facts, not a public repository URL baked into an image.

   ```sh
   install -d -m 700 "$HOME/.config/pyxis" "$HOME/.config/systemd/user"
   printf 'PYXIS_PACKAGES_DUFS="%s"\nPYXIS_PACKAGES_ROOT="%s"\nPYXIS_PACKAGES_BIND="%s"\nPYXIS_PACKAGES_PORT="%s"\n' \
     "$pkg_dufs" "$repo_root" "$pkg_bind" "$pkg_port" > "$HOME/.config/pyxis/packages-server.env"
   chmod 600 "$HOME/.config/pyxis/packages-server.env"
   install -m 644 tools/package-repository/pyxis-packages.service "$HOME/.config/systemd/user/pyxis-packages.service"
   systemd-analyze --user verify "$HOME/.config/systemd/user/pyxis-packages.service"
   ```

4. **Owner action: enable the service and LAN firewall rule.** Inspect the chosen
   address/CIDR first. Do not bind `0.0.0.0`, open a WAN rule, change the existing
   dufs service, or add upload/delete/allow-all flags. The unit clears inherited
   dufs config/auth and disables mutation and external-symlink options explicitly.
   The [dufs CLI reference](https://github.com/sigoden/dufs#cli) documents its
   default read-only serving and bind/port controls. User-manager startup follows
   horse's existing login/linger policy; enabling linger is not required here.

   ```sh
   systemctl --user daemon-reload
   systemctl --user enable --now pyxis-packages.service
   sudo ufw allow from "$pkg_lan" to "$pkg_bind" port "$pkg_port" proto tcp
   systemctl --user --no-pager status pyxis-packages.service
   ```

5. **Check HTTP and byte ranges, then configure a client.** This reads only
   metadata and the first 512 bytes of the public font archive, without dumping
   payload data. A non-206 result fails the range check. Use `journalctl --user
   -u pyxis-packages.service` for server failures; keep personal request details
   out of shared logs. Clients use horse's LAN address, `192.168.0.213`: the LAN
   has no DNS record for `horse` (chronium, 2026-10-22), and there is no mDNS.

   ```sh
   curl -fsS -o /dev/null "http://$pkg_bind:$pkg_port/public/index.lua"
   test "$(curl -fsS -r 0-511 -o /dev/null -w '%{http_code}' "http://$pkg_bind:$pkg_port/public/packages/fonts/nerd3.5.1-plexsans1.1.0-plexserif2.0.0-noto2.015-notomono2.014.tar")" = 206
   ```

   In a package-management space, run `pkg repository add public
   http://192.168.0.213:5245/public/`, then `pkg list --available`. Add
   `pkg repository add personal http://192.168.0.213:5245/personal/` only for
   the wanted private tree. If step 1 chose
   another port, use it in both URLs. `pkg install fonts` and `font-min` provide a
   small consumer check.

6. **Report the deployment evidence.** Return the Pyxis revision, dufs version,
   packer checks, service state and HTTP/range result. Do not attach licensed
   payloads or private recipes. To withdraw this instance, use `systemctl --user
   disable --now pyxis-packages.service` and remove only its exact ufw rule;
   retained package archives are not deleted.

## Packing a revision

Release and deployment pack for one revision R: the tag at a release, or the
build explicitly being deployed to a real machine, such as chronium's stick.
Nothing else packs into these trees; QEMU checks of a branch use a temporary
local repository.

1. **One clean checkout at R.** Use a plain clone with the `fs`, `userspace`,
   `ports` and `third_party/lwip` submodules at R's pins. It must have no local
   changes from here to the end: `make game-packages` refuses otherwise, and
   every `abi` would name a revision whose bytes differ.
2. **Build in the builder container:** `make -j16 image`, `make font-assets`
   and `make build/os-release`. The image is the one deployed with
   these packages.
3. **Personal games.** Run this in the container, with
   `~/.local/share/pyxis-private` and the data directories the recipes name
   mounted read-only at the same paths:

   ```sh
   make game-packages REPOSITORY="$repo_root/personal" REPOSITORY_NAME=personal \
     GAMES="doom quake duke3d diablo" GAME_RECIPES="$HOME/.local/share/pyxis-private/games"
   ```

4. **Public packages.** Pack `fonts` when its pinned inputs change; the packer
   refuses an ID/version already published. Build the hosted toolchain from the
   same clone ([native hosted payloads](../../toolchain/README.md#native-hosted-payloads)).
   Keep the SDK path and the hosted WORKDIR stable across revisions and reuse the
   persistent Ninja trees; SDK changes refresh target checks and relink, and
   source changes rebuild affected files. The producer reports and cleans on
   compiler/tool or configuration changes; use `--clean` only to request a full
   rebuild explicitly. Hosted sources have their own pin, so hosted-only changes
   need no compiler-container rebuild. Never reuse an old payload for R: stage
   from R's SDK, then pack the newly generated recipe:

   ```sh
   python3 tools/pxpkg.py pack packages/fonts.lua "$repo_root/public"
   python3 tools/pxpkg.py pack --build build HOSTED/out/clang-hosted.lua "$repo_root/public"
   ```

5. **Check, then prune.** Every bound package must cover R:

   ```sh
   python3 tools/pxpkg.py check "$repo_root/public" --abi R
   python3 tools/pxpkg.py check "$repo_root/personal" --abi R
   ```

   `prune DIR --keep-abi R TAGGED...` then removes other ABI builds. The keep
   list must include every supported origin and required checkpoint ABI, not
   only the newest destination; otherwise an older installed system can no
   longer complete its route. Keep published release/checkpoint artifacts and
   their required builds intact. Prune writes the new index before deleting
   archives.

6. **Publish.** Clients read `current` and the `index/N.lua` it names, not the
   working `index.lua` ([repository](../../docs/interfaces/packages.md#repository)),
   so a tree serves what it last published. These LAN trees are unsigned, as
   development images expect:

   ```sh
   python3 tools/pxpkg.py publish --repository public "$repo_root/public"
   python3 tools/pxpkg.py publish --repository personal "$repo_root/personal"
   ```

   The `make` package targets already end with a publish; publish again after
   `pxpkg pack` or `prune`.

The format reference describes [prune and check](../../docs/interfaces/packages.md#host-tool).

