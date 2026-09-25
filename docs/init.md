# Init and session startup

Boot selects one trusted init for every workload CPU. On a multicore boot,
CPU 0 stays in Caelum's kernel role; CPUs 1 onward each have their own init,
space, terminal and input. A single-CPU boot runs the primary init on the BSP.
Children stay in their parent's space and on its CPU. This does not introduce
migration, supervision or a global PID 1.

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
the [boot assembly manifest](boot-archive.md).

Selection is checked on every image build. Changing only the CPU map regenerates
Limine configuration and the ISO without recompiling the kernel. Identical
configuration and archive contents retain their timestamps. Supply overrides on
each invocation; a later build without them restores the packaged defaults.
Kernel-only builds do not select or package init.

## Packaged scripts

The userland repository supplies three shebang scripts using `app://shell.pxe`:

- `init/development.sh`, installed as `app://init`, opens the optional host export
  and hands off with `session app://session.pxe --configure-network`.
- `init/readonly.sh`, installed as `app://init-readonly`, opens the same optional
  export and hands off with `session app://session.pxe`, leaving network settings
  alone.
- `init/idle.sh`, installed as `app://init-idle`, exits immediately. No process
  remains; the scheduler uses its ordinary interruptible halt when idle. The
  space and its terminal remain available. This is not a machine shutdown or
  permanent CPU stop.

Both interactive profiles explicitly mount with `--read-only`. Scripts can
select `--read-write` grants for regular-file creation, writes, resize and the
existing `mkdir`, `rm`, `rmdir` and `mv` commands on `host://`. The packaged
development profile's mount selection is unchanged. Direct host executable
loading remains pending. Both profiles keep writable access to the shared RAM
home.

The session launcher applies per-space [terminal/environment configuration](session-configuration.md)
and starts the interactive shell. Only the development profile requests global
network setup. Boot does not order init execution or wait for one init's setup
before running another; select a single network-setup owner. Other sessions may
start before networking is configured. Super+Left/Right switches the active tab.

## Startup grants and lifetime

An init path must name an exact `app://` archive entry. Native init receives
that URI as `argv[0]`. A script interpreter receives its own URI as `argv[0]`,
the selected init URI as `argv[1]`, and a READ resource named `script`.
Interpreter lookup stays inside the boot archive and does not recursively
interpret scripts. LF/CRLF and bounds follow the [script-launch contract](script-launch.md).

Each init receives its space's terminal, display and keyboard grants, private
memory, launch, clock, randomness, networking services and network configuration,
read-only app and writable home roots, an initial `home://` working directory and
the initial environment. When virtio-fs is present it also receives `host_mount`,
scoped to that export. No authority is chosen from a hard-coded CPU role.

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
bypasses mounting/configuration for recovery. See [later lifecycle work](wip/later-os-directions.md#execution-lifecycle)
for deferred supervision and process replacement.
