# Horse package repository setup

Slice 1 task 6 was deployed on horse on 2026-10-19 from `880ff590` in
`~/src/pyxis-pkgrepo` (luna preparation, owner enable/ufw). The orchestrator
verified the active unit at `192.168.0.213:5245`, public index HTTP 200 and PUT
HTTP 403. Public offers fonts; personal offers Doom 1.9 and Quake 1.06 shareware.
`clang-hosted` was packed from a fresh producer build and published to the
public tree on 2026-10-20; the earlier manual payload was never packed.

The checklist below is the deployment/recovery procedure. Use the existing
installed dufs; it adds no download, compiler rebuild or change to the existing
file server. Current [task state](../../docs/wip/packages.md#task-6--horse-repository-complete-2026-10-19)
separates hosting from toolchain qualification.

The second dufs instance serves two independent repository trees beneath one
root: `public/` for fonts/toolchain, `personal/` for owner-local game data.
Only the host packer writes them, replacing the index last. Serving is read-only
and LAN-bound; neither the personal tree nor this listener is published to the
Internet. Each configured repository must offer distinct IDs: do not configure a
shareware and a personal repository both offering `doom-data` or `quake-data`.

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

2. **Pack the public tree and owner-selected personal inputs.** Create the root
   privately, prepare the pinned fonts, and select the existing host-produced
   `clang-hosted.lua` recipe described in
   [hosted tools](../../docs/development/hosted-tools.md). Do not substitute a
   recipe from a different payload tree. No native compiler run is involved.

   ```sh
   install -d -m 700 "$repo_root/public" "$repo_root/personal"
   make font-assets
   python3 tools/pxpkg.py pack packages/fonts.lua "$repo_root/public"
   python3 tools/pxpkg.py pack "${HOSTED_RECIPE:?set the generated clang-hosted.lua path}" "$repo_root/public"
   python3 tools/pxpkg.py check "$repo_root/public"
   ```

   Pack each wanted game-data recipe into `"$repo_root/personal"` using
   `python3 tools/pxpkg.py pack /private/RECIPE.lua "$repo_root/personal"`, then
   `python3 tools/pxpkg.py check "$repo_root/personal"`. Select data explicitly;
   keep private recipes and data outside the checkout and logs. Public shareware
   recipes from `packages/` can instead populate this personal tree where wanted.
   Do not configure its URL until it has an index. The packer refuses an existing
   ID/version: for an already prepared tree, use `check`, not replacement.

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
   out of shared logs. DNS for `horse` must resolve through the client's configured
   DNS server; there is no mDNS or numeric-address substitution.

   ```sh
   curl -fsS -o /dev/null "http://$pkg_bind:$pkg_port/public/index.lua"
   test "$(curl -fsS -r 0-511 -o /dev/null -w '%{http_code}' "http://$pkg_bind:$pkg_port/public/packages/fonts/nerd3.5.1-plexsans1.1.0-plexserif2.0.0.tar")" = 206
   ```

   In a package-management space, run `pkg repository add
   http://horse:5245/public/`, then `pkg list --available`. Add
   `http://horse:5245/personal/` only for the wanted private tree. If step 1 chose
   another port, use it in both URLs. `pkg install fonts` and `font-min` provide a
   small consumer check; the real toolchain and all games remain task 7.

6. **Report the deployment evidence.** Return the Pyxis revision, dufs version,
   packer checks, service state and HTTP/range result. Do not attach licensed
   payloads or private recipes. To withdraw this instance, use `systemctl --user
   disable --now pyxis-packages.service` and remove only its exact ufw rule;
   retained package archives are not deleted.
