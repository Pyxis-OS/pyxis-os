# Machine settings and the system hostname

Status: **task 1 implemented and qualified in QEMU**. The
owner accepted this direction and task 1 details on 2026-10-09. Tasks 2–7 need
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

## Task 1 contract

Accepted 2026-10-09:

- One RFC 1123 label, case preserved. The reader accepts one trailing LF or
  none, and rejects CR/CRLF, whitespace, NUL and extra bytes. Missing, unreadable
  or invalid keys use the archive default with one report. A shared userspace
  validator owns the grammar for boot and host image assembly.
- `SYSTEM_INFO_HOSTNAME`: header-only READ returns a 64-byte record; a fixed
  64-byte payload needs the independent SET_HOSTNAME_ONCE right. Only stock boot
  init receives it, publishes before any space, and forwards READ alone. A
  second set refuses. The kernel checks only length and printable ASCII.
- `gethostname()` needs the full name plus NUL; otherwise it returns
  `-1`/`ENAMETOOLONG` with the buffer unchanged. Missing authority is an error.
  The print-only `hostname` command uses the native query.

## Later writer and authority

Task 2's writer validates through the same shared schema, creates a temporary
file beside the key, writes and syncs it, then renames it over the key. The
[directory-sync limit](../technical-debt.md#atomic-save-limits) still applies:
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
`config validate` as Polaris's [configuration checker](boot-configuration-checker.md),
mDNS `NAME.local` and Tailscale naming await their own proposals/assignment.

## Task 1 qualification — 2026-10-09

Ordinary `make -j16 image` built the kernel, SDK, runtime, ports and applications
at Pyxis `c5c5beb8e964`, userland `8cbd9f87bdc7` and ports `19fb10b05549`, with
the existing `pyxis-llvm23.1.3-49e2c1a` builder. No compiler rebuild was needed.
Interactive QEMU 10.2.2 used Q35, nested KVM, four host CPUs, 8 GiB, OVMF and
VirtIO block/RNG/network devices; the installed fixture was a separate 2 GiB disk.

Installed runs at the earlier kernel `ad9d0a897008` / userland `8fbe2ccc4202`
confirmed missing-key fallback with one hostname report, `ThinkPad-7` with LF,
`NoLf-7` without LF, preserved case and CRLF rejection with one report. Writes
were explicitly synced and changed the running name only after reboot. Updating
that disk to `c5c5beb8e964` / `8cbd9f87bdc7` completed the installer's byte checks;
after reboot, both `hostname` and the retained key read `ThinkPad-7`, without a
hostname fallback report.

The earlier live image built with `HOSTNAME=T14-Live` printed that name without
a store warning. `HOSTNAME=bad.name` failed assembly through the shared validator;
omitting the option ignored the container's environment hostname. Source review
covered set-once/child authority and libc short-buffer/error behavior; those
failure paths were not forced at runtime. No native or power-loss checks ran.
