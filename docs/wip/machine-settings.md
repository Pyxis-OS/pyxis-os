# Machine settings and the system hostname

Status: **task 1 implemented and qualified in QEMU**. The
owner accepted this direction and the task 1 contract on 2026-10-09. Tasks 2–7 need
separate assignment. The [implemented store/hostname reference](../userland/machine-settings.md)
and [native query](../interfaces/system-information.md#hostname) own task 1's
interfaces and limits.

## Accepted direction

1. **Plain store plus validating writer.** Machine settings are ordinary npfs
   files under `system://config/machine/`, outside the boot archive. A directory
   is a subtree and a file is a key. Values have a schema-defined type; the first
   entry is `network/hostname`. Later entries may be booleans, integers or enums.
   The writer is task 2; there is no provider or notification service now.
2. **One kernel hostname per boot.** Boot init selects the persistent override
   or archive default, sets it once, and programs read `SYSTEM_INFO_HOSTNAME`
   through explicit READ authority. Ports get libc `gethostname()`; first-party
   tools use the native query. Task 5 will use the same name in the UDP log.
3. **Beacon retirement in task 4.** Replace `remote.beacon=NAME` and
   `make image REMOTE_BEACON=` with a remote-space boolean selecting reverse
   connection under the hostname. Remove the old forms together; task 1 keeps
   them working as they do today.

The default is `pyxis`; no random machine name is invented. Machines left with
that name can collide when later log, DHCP or mDNS consumers use it. That cost
is accepted. User settings would be a separate `home://` tree, not this store.

## Later writer and authority

Task 2's writer validates through the same shared schema, creates a temporary
file beside the key, writes and syncs it, then renames it over the key. The
[directory-sync limit](https://git.internal/PyxisOS/pyxis-os/issues/1407) still applies:
a crash can lose the new name or leave a temporary file, which readers ignore
by opening exact key names. The writer emits one trailing LF.

Read authority is a directory grant attenuated from the relevant system subtree;
no general machine-settings API is added. Today the installed `pyxis` space
holds the whole system root read-write, so write restriction is convention.
Narrowing it to the config tool and installer is a separate task. The store
survives system updates; reinstall formats it away. Task 6 will ask for a name
and may offer the old readable value before reinstall.

Directory generations exist, but directory objects are not waitable today.
No setting currently needs live notification. Add it only with a concrete
consumer such as DHCP re-announcing a rename; do not silently add polling.

## Tasks

1. [x] **Store and hostname.** Implemented reference above. No writer, beacon,
   log, prompt, Fastfetch or DHCP changes.
2. [ ] **Config tool.** `config get`, `set` and `list`, shared validation and
   atomic writes. The owner sets the key, sees invalid input refused and the
   new value apply on the next boot.
3. [ ] **Prompt and Fastfetch.** Read the native name; show `NAME tmp://notes> `
   in the prompt and a hostname line in Fastfetch.
4. [ ] **Remote beacon.** Retire the old option/build setting together; a
   remote-space flag uses the hostname for `pyxis-remote --listen NAME`.
5. [ ] **UDP log.** Carry the name beside the hardware address; early packets
   carry an empty name and the receiver retains the last nonempty one. Update
   kernel/header/host receiver together.
6. [ ] **Installer.** Choose a name on install and preserve/offer the old name
   on reinstall when readable.
7. [ ] **DHCP option 12.** Offer the hostname through userspace policy; server
   registration remains the network's choice.

Narrow writer grants, directory notification, user settings,
`config validate` as Polaris's [configuration checker](installer-tui.md#configuration-checker),
mDNS `NAME.local` and Tailscale naming await their own proposals/assignment.
