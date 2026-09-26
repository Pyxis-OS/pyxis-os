# Reproducing the libc consumer probe

This is the manual source/compile investigation for
[libc task 1](../libc-portability.md#pinned-source-probe), not a build recipe for
installed applications or a test runner. It uses sbase
`c546c3a5724c81cee9a11d816a38ccdf17472129`, Pyxis
`54873691f7ee0ee00115e77c84b3153d102cfa5b`, userland
`1263b5c5081239deb1d9831462a07032adb96d49`, and the existing Pyxis GCC 16.2.0
compiler. No host libc is visible to the consumer builds.

Run these blocks in one Bash session. Set the first two paths to the local
Pyxis repository and this directory. The remaining paths are private scratch
storage. No compiler rebuild is needed.

## Source and SDK inputs

```bash
set -e
probe_pyxis=/absolute/path/to/pyxis-os
probe_docs=/absolute/path/to/docs/wip/libc-probe
probe_cc="$HOME/opt/pyxis-cross/bin/x86_64-unknown-pyxis-gcc"
probe_root=$(mktemp -d "${TMPDIR:-/tmp}/pyxis-libc-probe.XXXXXX")

# The detached checkout isolates the historical SDK inputs from current work.
git -C "$probe_pyxis" worktree add --detach "$probe_root/pyxis" \
  54873691f7ee0ee00115e77c84b3153d102cfa5b
git -C "$probe_root/pyxis" submodule update --init userspace
git -C "$probe_root/pyxis/userspace" fetch origin \
  1263b5c5081239deb1d9831462a07032adb96d49
git -C "$probe_root/pyxis/userspace" checkout --detach \
  1263b5c5081239deb1d9831462a07032adb96d49
make -C "$probe_root/pyxis" -j16 sdk CROSS_COMPILE="${probe_cc%gcc}" \
  > "$probe_root/sdk.log" 2>&1
probe_sdk="$probe_root/pyxis/build/sdk"
cat "$probe_sdk/manifest.txt"

# Fetch the exact sbase revision, independently of its current branch tip.
git init -q "$probe_root/sbase"
git -C "$probe_root/sbase" remote add origin https://git.suckless.org/sbase
git -C "$probe_root/sbase" fetch --depth=1 origin \
  c546c3a5724c81cee9a11d816a38ccdf17472129
git -C "$probe_root/sbase" checkout --detach -q FETCH_HEAD
mkdir "$probe_root/objects" "$probe_root/logs"
```

The SDK manifest reports the parent as modified because userland is at its
remote merge commit instead of the parent's pin. Their userland file trees
are identical. Ports and lwIP are not SDK inputs and need not be initialized
for this reproduction. The original investigation initialized them at their
remote merge tips too; their trees also matched the parent pins.

## Unmodified source failures

These are the exact raw-probe compiler options. Each translation unit is
compiled separately so a missing header in one does not hide the others.
Failures are expected; the logs are the evidence, not a passing executable.

```bash
probe_flags=(
  "--sysroot=$probe_sdk/sysroot"
  -nostdinc -isystem "$("$probe_cc" -print-file-name=include)"
  "-I$probe_sdk/sysroot/usr/include"
  -std=gnu23 -ffreestanding -fno-pie -fno-stack-protector
  -mno-red-zone -march=x86-64 -Wall -Wextra
)
for probe_src in cksum.c tee.c libutil/eprintf.c libutil/fshut.c \
                 libutil/ealloc.c libutil/writeall.c libutil/reallocarray.c; do
  probe_name=${probe_src##*/}
  probe_name=${probe_name%.c}
  "$probe_cc" "${probe_flags[@]}" -c "$probe_root/sbase/$probe_src" \
    -o "$probe_root/objects/$probe_name.o" \
    > "$probe_root/logs/$probe_name.raw.log" 2>&1 \
    || printf '%s: raw compile failed; see log\n' "$probe_src"
done
```

The commands stop at fcntl.h, eprintf/fshut/ealloc at sys/types.h via util.h,
and writeall at unistd.h. Reallocarray was also inspected and stops at
sys/types.h, but is not needed by either consumer and is excluded below.

## Isolated declaration and helper-header probe

[scratch.patch](scratch.patch) contains the complete original replacement
util.h and four declaration-only headers. It changes no command or helper
function body. The reduced util.h retains some unused helper declarations
exactly as in the investigation; only the listed objects are linked.

The fcntl/signal constants are scratch parsing values, not accepted ABI
assignments. The fake signal declarations implement no signal behavior.
The inttypes.h supplies only stdint.h and PRIu32; the SDK's uint32_t is unsigned
int. BUFSIZ is supplied as 8192 on the compiler command line. These files must
not be installed into an SDK or shipped as compatibility implementations.

```bash
git -C "$probe_root/sbase" apply --check "$probe_docs/scratch.patch"
git -C "$probe_root/sbase" apply "$probe_docs/scratch.patch"
probe_isolated_flags=(
  "--sysroot=$probe_sdk/sysroot"
  -nostdinc -isystem "$("$probe_cc" -print-file-name=include)"
  "-I$probe_root/sbase/probe-headers" "-I$probe_sdk/sysroot/usr/include"
  -std=gnu23 -O2 -ffreestanding -fno-pie -fno-stack-protector
  -mno-red-zone -march=x86-64 -Wall -Wextra -DBUFSIZ=8192
)
for probe_src in cksum.c tee.c libutil/eprintf.c libutil/fshut.c \
                 libutil/ealloc.c libutil/writeall.c; do
  probe_name=${probe_src##*/}
  probe_name=${probe_name%.c}
  "$probe_cc" "${probe_isolated_flags[@]}" \
    -c "$probe_root/sbase/$probe_src" \
    -o "$probe_root/objects/$probe_name.o" \
    > "$probe_root/logs/$probe_name.isolated.log" 2>&1
done

# The Pyxis driver supplies SDK startup, linker script, libc/libpyxis/libgcc.
"$probe_cc" "--sysroot=$probe_sdk/sysroot" \
  "$probe_root/objects/cksum.o" "$probe_root/objects/eprintf.o" \
  "$probe_root/objects/fshut.o" -o "$probe_root/cksum.elf" \
  > "$probe_root/logs/cksum.link.log" 2>&1 \
  || printf 'cksum: link failed; see log\n'
"$probe_cc" "--sysroot=$probe_sdk/sysroot" \
  "$probe_root/objects/tee.o" "$probe_root/objects/eprintf.o" \
  "$probe_root/objects/ealloc.o" "$probe_root/objects/writeall.o" \
  -o "$probe_root/tee.elf" > "$probe_root/logs/tee.link.log" 2>&1 \
  || printf 'tee: link failed; see log\n'
cat "$probe_root/logs/cksum.link.log" "$probe_root/logs/tee.link.log"
```

All six selected objects compiled in the recorded run. Cksum and tee retain
upstream signedness warnings. Cksum linking leaves open/read/close unresolved;
tee leaves open/read/write/signal unresolved. Diagnostic, formatting, shutdown
and allocation helpers resolve against the target SDK. Neither application
links successfully or has been booted; declaration-only compilation says
nothing about the missing functions' runtime semantics.

## Cleanup and provenance

After reviewing or copying the logs, remove only this disposable checkout and
its scratch files. Force is needed because this worktree initialized a submodule.

```bash
git -C "$probe_pyxis" worktree remove --force "$probe_root/pyxis"
rm -rf -- "$probe_root"
```

The patch's upstream util.h material is covered by [LICENSE.sbase](LICENSE.sbase).
The pinned checkout retains the full license and individual file notices,
including arg.h. No upstream application sources or executable stubs are added
to Pyxis by this record.
