# Init and session startup

Boot selects one trusted init for every workload CPU. On a multicore boot,
CPU 0 stays in Caelum's kernel role; CPUs 1 onward each have their own init,
space, terminal and input. A single-CPU boot runs the primary init on the BSP.
Children stay in their parent's space and on its CPU. This does not introduce
migration, supervision or a global PID 1.

Init also receives an explicit namespace-creation service. The packaged active
init scripts create a [service namespace](../interfaces/namespaces.md) before session handoff;
it is delegated independently of space membership.

Initial processes share the read-only `app://` archive and writable RAM-backed
`home://` tree. Home is not private per space and disappears on reboot. Each
init receives the full available bootstrap grants. Init scripts are trusted
setup policy; the session handoff delegates resources to ordinary applications.

## Boot selection

The generated Limine configuration carries these kernel command-line options:

```text
init=app://init-idle init.primary=app://init init.2=app://init-readonly
```

`init` supplies the default for workload CPUs. `init.primary` overrides it on
CPU 1, or CPU 0 on a single-CPU boot. `init.N` overrides either selection for
dense CPU index N, independently of option order. Numeric indices are not APIC
IDs. Selections for absent CPUs are logged and skipped, so one image can boot
with different CPU counts. Selecting CPU 0 explicitly on a multicore boot is
an error. No init is launched there otherwise.

Make exposes the selections separately from the host file used to stage `init`:

```sh
make run CPUS=4
make run CPUS=4 INIT_CPUS='2=app://init-readonly 3=app://init-readonly'
make run CPUS=4 INIT_CPUS='2=app://init-idle'
make run CPUS=4 INIT_PRIMARY=app://init-readonly INIT_CPUS=
make run INIT=/tmp/init.sh
make image INIT=build/userspace/shell.pxe
make image                         # restore the packaged init and selections
```

`INIT_DEFAULT` defaults to `app://init-idle`, `INIT_PRIMARY` to `app://init`,
and `INIT_CPUS` to `2=app://init-readonly`. `INIT_CPUS` is a whitespace-separated
list of `CPU=URI` entries; an empty value removes numeric overrides. Paths select
entries already in the boot archive, using letters, digits, `_`, `.`, `/`, `:`,
`+` and `-`. The kernel supports whitespace-separated options, without quoting
or escaping, in a command line of at most 4095 bytes. Missing default selection,
malformed/unknown options and duplicate selections for a present CPU fail boot.

`INIT` names one host file, relative to the repository root or absolute. It is
staged as `app://init`, replacing the packaged development script; it does not
change other CPUs' selections. It can be a native PXE executable or shebang
script. To run only that selection, also use `INIT_CPUS=`; other workload CPUs
then use the default idle script. Additional custom archive entries belong in
the [boot assembly manifest](../development/boot-archive.md).

Selection is checked on every image build. Changing only the CPU map regenerates
Limine configuration and the ISO without recompiling the kernel. Identical
configuration and archive contents retain their timestamps. Supply overrides on
each invocation; a later build without them restores the packaged defaults.
Kernel-only builds do not select or package init.

## Packaged scripts

The userland repository supplies four shebang scripts using `app://shell.pxe`:

- `init/development.sh`, installed as `app://init`, opens the optional host export
  with `mount --optional --read-write host` and hands off with
  `session app://session.pxe --configure-network --start-services`.
- `init/readonly.sh`, installed as `app://init-readonly`, opens the same optional
  export with `mount --optional --read-only host` and hands off with
  `session app://session.pxe --start-services`, leaving network settings alone.
- `init/services.sh`, installed as `app://init-services`, publishes the HTTP
  provider using the configured session environment, then starts a separate
  optional HTTPS provider with read-only trust grants and hands off to the
  interactive shell. A reported HTTPS setup failure leaves HTTPS unpublished
  and permits that handoff. It runs only when session selects `--start-services`.
- `init/idle.sh`, installed as `app://init-idle`, sets its title and exits. No process
  remains; the scheduler uses its ordinary interruptible halt when idle. The
  space and its terminal remain available. This is not a machine shutdown or
  permanent CPU stop.

The development profile delegates host write grants for regular-file creation,
writes, resize and `mkdir`, `rm`, `rmdir` and `mv`; the read-only profile delegates
only host read grants. Both still start in writable, shared RAM `home://` and
must explicitly address or enter `host://` to use the export. A host READ grant
can load a native executable with the existing launcher authority. An absent
device leaves either optional mount unbound, while an operational mount error
stops the script. The host daemon's `--readonly` and host file permissions
independently restrict mutations; a guest read-write grant only authorizes
attempts. See the [host setup and walkthrough](../devices/virtio-fs.md#start-the-host-service).

The session launcher applies per-space [terminal/environment configuration](session-configuration.md)
and starts the interactive shell. Only the development profile requests global
network setup. Boot does not order init execution or wait for one init's setup
before running another; select a single network-setup owner. Other sessions may
start before networking is configured. Super+Left/Right switches the active tab.

An explicitly selected init script can instead hand off with
`session app://session.pxe --configure-network --tcp-server ADDRESS PORT`,
optionally adding `--tcp-count COUNT`. The trusted launcher creates an exact
bound listener and starts the [concurrent TCP echo consumer](../devices/tcp.md#concurrent-echo-server)
with only that listener, memory, clock and output streams. Bootstrap init has
separate TCP LISTEN authority; ordinary session startup delegates only CONNECT.
This opt-in handoff replaces that init's shell and does not expose a remote shell.

Trusted init also receives a `terminal` service with CREATE authority for
[independent terminal sessions](terminal-sessions.md). Ordinary session startup
does not delegate that service or an attachment. Application terminal handles
use the same named input/output and standard-stream forwarding as framebuffer
consoles; there is no remote shell startup mode yet.

## Space titles

`title "Development"` sets the caller's tab label. Packaged init scripts set
`Development`, `Read-only` and `Idle`, respectively, before mounting or handing
off. They use `title --optional` so the single-CPU fallback, which has no title
capability, keeps the pinned `Caelum` name and continues startup. The optional
form ignores only a missing capability; malformed text and operation failures
still stop a script.

The initial `space` resource grants `SPACE_RIGHT_SET_TITLE`. It is bound to
that init's space, and the kernel also requires the caller to belong to the
same space. No title resource is issued for Caelum. The shell's `session`
handoff passes this grant to the session launcher, which passes it to the
interactive shell. Ordinary foreground/background commands receive no title
grant. An explicit native launcher can delegate it within its space using the
existing capability machinery.

The [space protocol](../../include/abi/space.h) accepts 1–63 printable ASCII bytes,
including spaces, and returns no payload. Invalid requests leave the previous
title unchanged. Libpyxis exposes `space_set_title(handle, text)` through
`<space.h>`. The space owns the copied text: process exit and closing the last
handle do not reset it. Titles may repeat; they are display labels, not names
for lookup, identity or authority. Longer labels are clipped visually to the
existing tab width, without altering the stored text. The presenter snapshots
the title under a short lock before drawing; updates do not allocate.

## Startup grants and lifetime

An init path must name an exact `app://` archive entry. Native init receives
that URI as `argv[0]`. A script interpreter receives its own URI as `argv[0]`,
the selected init URI as `argv[1]`, and a READ resource named `script`.
Interpreter lookup stays inside the boot archive and does not recursively
interpret scripts. LF/CRLF and bounds follow the [script-launch contract](script-launch.md).

Each workload init receives its space's title, terminal, display and keyboard
grants, private memory, launch, clock, randomness, networking services and
network configuration, caller-scoped [memory profiling](../development/allocation-profiling.md), explicit
[endpoint creation](../interfaces/endpoints.md) through the `service` resource,
read-only app and writable home roots, an initial `home://` working directory and
the initial environment. When virtio-fs is present it also receives `host_mount`,
scoped to that export. The BSP fallback omits the title grant. Workload CPU
selection chooses which trusted init runs, not an authority ceiling; no workload
authority is chosen from a hard-coded CPU role.

Mount authority stays with init; the mounted root travels through session
handoff. Network-configuration authority reaches the session launcher but not
its interactive shell. Ordinary commands do not inherit launch authority;
`session` delegates it explicitly. The kernel enforces capability rights, while
the trusted programs choose what to delegate. Neither URI names nor selecting
a different script creates authority beyond the supplied grants.

Failure to select or load an init is a boot error identifying its CPU/path.
A running script stops on its first failed command; EOF exits without opening
a prompt. Init and session are not restarted. Their space, terminal contents
and shared namespace roots survive exit. Selecting a native shell directly
bypasses mounting/configuration for recovery. See [later lifecycle work](../wip/later-os-directions.md#execution-lifecycle)
for deferred supervision and process replacement.

Trusted init also holds CREATE_GROUP on its ordinary launcher. The default session
program attenuates this to LAUNCH when starting the shell. An explicitly authorized
supervisor can create an [execution group](../interfaces/execution-groups.md) and
use its bound launcher while remaining outside that group.
