# Machine settings and hostname

Machine overrides live in ordinary npfs files under `system://config/machine/`,
outside the boot archive. System updates preserve that tree; reinstall formats
it away. A subtree is a directory and a key is a file; there is no provider,
notification service or writer yet. Programs with the existing read-write system
root can edit it by convention. Narrow writer authority is a later task.

## Hostname schema

`system://config/machine/network/hostname` contains one ASCII label: 1–63
letters, digits or hyphens, with no leading/trailing hyphen or dots. Numeric
labels are valid. Case is preserved; comparisons are case-insensitive. The
shared [validator](../../userspace/lib/machine_settings.c) is the authority for
this schema and is also exported in the SDK for image assembly.

The reader accepts either one final LF or none. CR/CRLF, whitespace, NUL,
multiple lines and extra bytes refuse. It probes beyond the maximum rather than
truncating. Missing, unreadable or invalid keys use the archive default with one
hostname fallback report on Caelum. Changing the file affects the next boot. Before reboot, explicitly sync the
written key through the ordinary `sync PATH` command.

Boot init mounts `system` and reads the key before creating any space, including
rescue/default-config boots. `system://config/boot.lua` remains a volumes/spaces
configuration and cannot rename the machine. The validated `hostname` in
`boot://config/installed.lua` is the installed fallback; stock archives use
`pyxis`. A malformed archive value rejects that configuration as other invalid
fields do. Rescue still sets a name before its shell starts.

Live boots have no store and use `boot://config/live.lua`, without a store warning:

```sh
make -j16 image HOSTNAME=T14-Live
```

Only an explicit command-line `HOSTNAME=` overrides that live field; omitted
input preserves the selected archive default and does not import the build
host/container's environment name. Empty or invalid overrides fail the build.
The installed default and source configuration files are untouched.

## Reading the running name

```sh
hostname
```

The command accepts no arguments and prints the native
[`SYSTEM_INFO_HOSTNAME`](../interfaces/system-information.md#hostname) result.
All reads use the explicitly delegated `system_info` grant. They do not open the
store or require system-root authority. Boot init alone has the set-once right;
children receive READ. Applications see the same published name throughout the boot.

For ports, libc declares `gethostname(char *name, size_t size)` in `<unistd.h>`.
Success writes the complete name and NUL. Insufficient room returns `-1` with
`ENAMETOOLONG`, leaving the buffer unchanged; missing/denied authority and native
errors also fail. There is no `sethostname()` or invented per-process name.
First-party tools use the native query.

The validating writer, installer naming, prompt/Fastfetch, remote beacon, UDP
log and DHCP are separate [milestone tasks](../wip/machine-settings.md#tasks).
`remote.beacon` and its current build option retain their existing behavior.
