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
truncating. Missing, unreadable or invalid keys use the boot configuration's
`hostname` with one fallback report on Caelum. Changing the file affects the next boot.
Before reboot, explicitly sync the written key through the ordinary `sync PATH`
command.

Boot init mounts `system` and reads the key before creating any space, including
rescue/default-config boots. The validated `hostname` in
`system://config/boot.lua` is only the installed fallback; the initial file uses
`pyxis`. A malformed value rejects that configuration as other invalid fields
do. Rescue still sets a name before its shell starts.

Live boots have no store and use `boot://config/live.lua`, without a store warning:

```sh
make -j16 image HOSTNAME=T14-Live
```

Only an explicit command-line `HOSTNAME=` overrides that live field; omitted
input preserves the value in `live.lua` and does not import the build
host/container's environment name. Empty or invalid overrides fail the build.
The installed configuration's initial `boot.lua` and source files are untouched.

## Reading the running name

```sh
hostname
```

The command accepts no arguments and prints the native
[`SYSTEM_INFO_HOSTNAME`](../interfaces/system-information.md#hostname) result.
All reads use the explicitly delegated `system_info` grant. They do not open the
store or require system-root authority. Boot init alone has the set-once right;
children receive READ. Applications see the same published name throughout the
boot.

For ports, libc declares `gethostname(char *name, size_t size)` in `<unistd.h>`.
Success writes the complete name and NUL. Insufficient room returns `-1` with
`ENAMETOOLONG`, leaving the buffer unchanged; missing/denied authority and native
errors also fail. There is no `sethostname()` or invented per-process name.
First-party tools use the native query.

The validating writer, installer naming, prompt/Fastfetch, remote beacon, UDP
log and DHCP are separate [milestone tasks](../wip/machine-settings.md#tasks).
`remote.beacon` and its current build option retain their existing behavior.

## Input direction schema

[Boot input policy](../interfaces/input-policy.md) uses `input/mouse/scroll-direction`
and `input/touchpad/scroll-direction` under the same machine tree. Exact values
are `natural` or `traditional`, with one optional final LF; the shared schema
validator is exported with the SDK. Missing/invalid keys preserve each archive
authored default. Installation is once before spaces, changes apply next boot,
and diagnostics require archive `input.diagnostics = true`. No settings writer
or live input-policy authority is added.

## Qualification

QEMU 10.2.2 (Q35, nested KVM, four CPUs, VirtIO) with ordinary `make -j16 image`
on 2026-10-09. An installed disk confirmed missing-key fallback with one report,
case and LF/no-LF handling, CRLF rejection, and that synced writes change the
running name only after reboot; a System Update then preserved the key.
`HOSTNAME=T14-Live` printed without a store warning, `HOSTNAME=bad.name` failed
assembly through the shared validator, and an omitted option ignored the
container's environment name. Set-once/child authority and libc short-buffer
behavior were source-reviewed, not forced at runtime. No native or power-loss
checks ran.
