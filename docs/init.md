# Init and session startup

Boot loads the `init` entry from the read-only boot archive. It starts on CPU 1
when available, otherwise on the BSP; Caelum keeps its existing kernel role.
Init is a native PXE executable or a script with a native interpreter in that
same archive. This does not define a global PID 1 or per-space supervisor.

The default [userspace/init.sh](https://git.internal/chronium/pyxis-userland/src/branch/main/init.sh) uses `app://shell.pxe` as
its interpreter and hands off with `session app://shell.pxe`. Init then exits;
the interactive shell retains its own resource references and launch authority.
See [script execution](shell.md#script-mode) and
[session handoff](shell.md#session-handoff) for command and failure behavior.

## Selecting init

```sh
make image                          # default userspace/init.sh
make image INIT=/tmp/init.sh         # host script, staged as app://init
make run INIT=/tmp/init.sh CPUS=4     # build and boot that selection
make image INIT=build/userspace/shell.pxe  # native PXE init, built first
make image                          # restore the default, without cleaning
```

`INIT` names one host file, relative to the repository root or absolute. It
selects contents, not an archive path or kernel command line. The build stages
it at `build/userspace/init`; the source is not modified. Both native and script
selections keep the diagnostic identity `app://init`.

Selection is checked on every archive build, after building userspace. Content
changes are picked up even if the selected file has an older timestamp; identical
contents preserve the staged file's timestamp. The archive is repacked and only
replaced if its bytes changed, avoiding an unnecessary ISO rebuild. Missing or
unreadable input fails the build instead of booting stale contents.

Pass `INIT` to each `image`, `initrd`, `run` or `debug` invocation that should use
the override. It is not remembered: a later invocation without an override uses
the default again (unless `INIT` is set in the environment). Kernel-only `make`
and `make userspace` do not select or package init.

## Startup contract and limits

Native init receives `argv[0] = "app://init"`. A script interpreter receives its
URI as `argv[0]`, `app://init` as `argv[1]`, and a READ resource named `script`.
Boot interpreter lookup accepts `app://` followed by an exact archive entry
name; it does not resolve general paths or recursively interpret scripts.
LF/CRLF and shebang bounds follow the shared [script-launch contract](script-launch.md).
The native format is PXE, not the ELF used as converter input.

Init explicitly receives terminal input/output, private-memory management,
[display authority](graphics.md), launch authority, read-only `app` and writable RAM-backed `home` roots, an
initial `home://` working directory and the initial environment. Ordinary shell
commands do not inherit launch authority; `session` delegates it explicitly.
URI names do not supply authority independently of those grants.

Failure to select or load init is a boot error. A running script stops on its
first failed command; EOF exits without opening a prompt. Neither init nor its
session is automatically restarted. Their space, terminal contents and namespace
roots survive process exit. No mount setup, supervision or process replacement
is implemented; [later lifecycle work](wip/later-os-directions.md#execution-lifecycle)
remains separate from this startup contract.
