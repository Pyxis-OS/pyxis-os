# Edit, build and run inside Pyxis

The image includes [BusyBox vi](../userland/vi.md) for editing. The compiler is
hosted Clang, the `clang-hosted` package: install it once with
`pkg install clang-hosted` from a configured repository ([package installation](hosted-tools.md#package-installation)).
Then a C program can be created, compiled and run entirely inside Pyxis.

Build and boot with the normal [SDK and ports setup](ports.md#building-and-packaging):

```sh
make image DISTRIBUTION=full
make run DISTRIBUTION=full CPUS=4
```

Select the application space with Super+Right. From the shell's initial `home://`
directory, run `vi hello.c`, press `i` to enter insert mode and type:

```c
#include <stdio.h>

int main(void)
{
  puts("Hello from Pyxis");
  return 0;
}
```

Press Escape, type `:wq` and press Enter to save and quit, then compile and launch:

```text
clang hello.c -o hello.pxe
./hello.pxe
```

Reopen `vi hello.c`, press `i` to edit the message, then Escape and `:wq` to save
and quit. Repeat the same compiler and launch commands to see the updated
output. No ISO rebuild or host conversion is needed between edits. Use `cat hello.c` to inspect saved bytes as terminal
output; the TTY displays tabs at eight-column stops, while vi uses its configured
`tabstop` for display.

Clang reports compile and link errors through the terminal. Correct the source
and rebuild before running the output again: a failed build may leave an older
executable, or a partial file if output writing began. The shell reports nonzero
child exit status. vi uses [atomic replacement](../userland/vi.md#how-the-port-maps-onto-pyxis)
for saves; it requires CREATE and REMOVE authority on the containing directory.

On a live boot, `home://` and `tmp://` are RAM-backed and disappear on reboot;
an [installed system](../userland/installer.md) keeps `home://` on its npfs pool.
For persistence on live boots, use the [USB workflow below](#persistent-usb-development)
or a [writable host export](../devices/virtio-fs.md#persistent-development-walkthrough),
keeping source and output under the corresponding root. `boot://` is read-only.
The native `pkg` client installs packages such as `fonts` and `clang-hosted`
from a configured repository ([package formats](../interfaces/packages.md)).
Toolchain self-hosting remains unsupported. The host Clang continues to build
maintained OS/userland sources.

## Try the pointer example

The image ships a small graphics program as source in `boot://share/examples/`.
`pointer-min` draws a square that follows the mouse, turns green while the left
button is held and quits on Escape. Compile and run it from a local shell that
has the display, keyboard and pointer grants, such as the Development space:

```text
clang boot://share/examples/pointer-min.c -o pointer-min.pxe
./pointer-min.pxe
```

Copy it with `cp boot://share/examples/pointer-min.c home://` to edit it with
vi. The same source is built with Clang as the installed `pointer-min`
program. Its comments explain each Pyxis-specific step; the
[pointer reference](../interfaces/pointer.md) has the contract.

`font-min` is the same program with text added. It draws a line in each of the
nine styles from the installed `fonts` package and shows the pointer position as text.
It links `libfont`, named with `-lfont`:

```text
pkg install fonts
mkdir font-min.pxb
mkdir font-min.pxb/app
mkdir font-min.pxb/app/bin
cp boot://share/examples/font-min-manifest.json font-min.pxb/manifest.json
clang boot://share/examples/font-min.c -lfont -o font-min.pxb/app/bin/font-min.pxe
./font-min.pxb
```

See the [font reference](../userland/fonts.md) for the library contract.

See [hosted Clang](hosted-tools.md#compile-link-run), [vi controls](ports.md#editing-in-pyxis)
and [terminal behavior](../userland/terminal.md) for details.

## Persistent USB development

An explicitly configured USB-backed npfs volume can hold source, objects and
native executable output across boots. The compiler is the installed
`clang-hosted` package; `tmp://` stays RAM-backed. This QEMU walkthrough uses a private disk
copy and a separately booted ISO. Physical installation uses the
[native installer](../userland/installer.md); see
[USB installation](../devices/usb-installation.md#validation).

### Select and mount a private disk

Start with an existing [sample USB image](usb-image.md), assembled once before
storing work. While it is detached, copy it to a separate file:

```sh
usb_disk=/tmp/pyxis-usb-development.raw
cp --sparse=always build/pyxis-usb.img "$usb_disk"
LC_ALL=C sgdisk -p "$usb_disk"
```

Set the shell variable `usb_guid` to that file's displayed Disk identifier
(GUID). The builder generates it; do not reuse a GUID from a different image.
The sample has npfs in GPT entry 2, volume `usb-test`. Other prepared disks must
use their actual entry and volume. Attach only the selected copy, since duplicate
observed GUIDs prevent mounting. Keep this copy for later sessions: `usb-image`
replaces `build/pyxis-usb.img`, and copying a fresh image over the development
file would discard saved work.

Enable `CONFIG_XHCI=y` in `.config` or menuconfig. A build with `MOUNT_DISK`
reads the installed [boot configuration](../userland/init.md#boot-configuration),
so describe the USB volume and space in `userspace/config/live.lua`, which a
build with `MOUNT_DISK` reads because it names no `mount.pool`:

```lua
return {
  volumes = {
    usb = { kind = "npfs", partition = 2, volume = "usb-test" },
  },
  spaces = {
    { name = "usb", title = "USB", init = "boot://init", network = true,
      launch = true, roots = { usb = "read-write" } },
  },
}
```

`launch = true` matches the packaged Development space, so scripts and Lua
programs run in this space can start commands, and Clang can start LLD. With no `home` root, the space starts in `tmp://`.

Save this trusted script as `/tmp/init-usb-development.pxs`:

```text
#!boot://shell.pxe
service start text boot://textfs.pxe
session boot://session.pxe --configure-network --start-remote-services boot://init-remote-services
```

Build the separate ISO and existing remote client:

```sh
make -j16 image DISTRIBUTION=full INIT=/tmp/init-usb-development.pxs MOUNT_DISK="$usb_guid"
make -C tools remote
```

The mount requires configured GUID authority, known clear write protection and
successful blocking cache synchronization. A failed mount leaves the space
unstarted, with the reason on its tab; boot init does not silently select
another disk or fall back to RAM. The session receives
ordinary directory/file grants, with no mount or raw installer authority.
See the [USB storage contract](../devices/usb-storage.md).

### Launch and develop

Use matching OVMF files and the QEMU binary available on your host. For example:

```sh
QEMU=qemu-system-x86_64
OVMF_CODE=/usr/share/OVMF/OVMF_CODE.fd
OVMF_VARS=/usr/share/OVMF/OVMF_VARS.fd
cp "$OVMF_VARS" /tmp/pyxis-usb-vars.fd
"$QEMU" -machine q35 -accel kvm -cpu max \
  -smp cpus=4,sockets=1,cores=4,threads=1 -m 8G -rtc base=utc \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/pyxis-usb-vars.fd \
  -cdrom build/pyxis.iso -boot d -display none -serial mon:stdio \
  -netdev user,id=net,hostfwd=tcp:127.0.0.1:24567-10.0.2.15:2323 \
  -device virtio-net-pci,netdev=net,disable-legacy=on \
  -object rng-random,id=rng,filename=/dev/urandom \
  -device virtio-rng-pci,rng=rng,disable-legacy=on \
  -device qemu-xhci,id=usb,p2=1,p3=1 \
  -drive if=none,id=usb_disk,format=raw,cache=writeback,file="$usb_disk" \
  -device usb-storage,bus=usb.0,port=1,drive=usb_disk
```

The supplied `run-usb` launcher attaches its disk read-only; this manual command
explicitly permits writes to the private copy. No VirtIO disk or host filesystem
export is attached. Controller and device identities are discovered; the QEMU
port arguments describe this example's virtual wiring.

In another host terminal, connect with
`build/tools/pyxis-remote 127.0.0.1 24567`. In the first guest session:

```text
mkdir usb://work
cd usb://work
vi hello.c
```

Press `i` and enter the program above, using `Hello from USB, first build` as its
message. Press Escape, type `:wq` and press Enter to save and quit, then run:

```text
clang hello.c -o hello.pxe
./hello.pxe
vi hello.c
```

Press `i`, change the message to `Hello from USB, second build`, then press Escape
and enter `:wq` to save and quit. An object build exercises the same persistent
paths:

```text
clang -c hello.c -o hello.o
clang hello.c -o hello.pxe
./hello.pxe
sync usb://work/hello.c usb://work/hello.o usb://work/hello.pxe usb://work usb://
sha256sum hello.c hello.o hello.pxe
```

Check each compiler and sync result before proceeding. Successful save, close
or compilation alone does not establish persistence. Native file/directory
sync commits the current pool and completes the backend's ordered flush. A
successful sync reaches durable COMMITTED; background checkpointing later makes
the journal EMPTY. A failed compile may leave an older or partial output. vi's
atomic replacement does not establish persistence across a power loss; the
[atomic-save limits](https://git.internal/PyxisOS/pyxis-os/issues/1407) still apply.

### Restart and inspect read-only

After successful sync, exit the remote shell and stop QEMU with Ctrl-a x in its
serial terminal. Start a fresh process with the same disk file, creating fresh
OVMF variables again. Do not copy or format the disk again. In the guest:

```text
cd usb://work
cat hello.c
sha256sum hello.c hello.o hello.pxe
./hello.pxe
```

The saved source, hashes and second message should match the earlier session.
You can reopen vi and rebuild again without rebuilding the ISO or disk.

For a separate read-only session, stop QEMU, change the trusted script's mount
to `--read-only`, then rebuild only the ISO with the same `MOUNT_DISK` and init
selection. In the QEMU command replace the USB drive's `cache=writeback` with
`readonly=on`. Reads and saved executable launch remain available; saving edits,
creating files or compiling output onto `usb://` is denied. A read-only pool
requires an EMPTY journal and refuses a committed journal requiring replay.
Successful sync alone does not guarantee immediate read-only-open readiness.
If init reports replay required, stop QEMU and restore both the writable init and
writable attachment. Open the volume once for recovery, let replay/checkpoint
complete without new edits, then stop and retry the read-only session. See the
[native journal contract](../devices/filesystem-native-adapter.md#writeback-recovery-and-errors)
for the completion points. No host process may modify an attached disk.

The [C.2 qualification record](usb-storage-bringup.md#persistent-usb-development-loop-c2)
records the earlier manual editor/compiler, restart and read-only observations
at its recorded revisions. The walkthrough above now uses vi. An orderly QEMU
restart is not a physical cache or power-loss qualification. Restore the
checked-in `CONFIG_XHCI=y` and run `make image DISTRIBUTION=full` without
init/mount overrides to return to the checked-in boot defaults.

## Use a remote terminal

From the repository root, build the native host client and boot four CPUs with
networking and loopback forwarding:

```sh
make -C tools remote
make run DISTRIBUTION=full CPUS=4 VIRTIO_NET=1 TCP_FORWARD=2323:2323
```

Leave QEMU running. In another host terminal, connect from the repository root:

```sh
build/tools/pyxis-remote 127.0.0.1 2323
```

The same vi, Clang and execution commands above work in the remote shell.
An optional HOST export is writable from the default Remote space, subject to
host permissions. Remote and local sessions share the actual filesystem roots;
choose distinct output names when working concurrently.

For command tools without a controlling terminal, use the
[persistent machine-client workflow](../userland/remote-terminal.md#persistent-use-through-command-tools).
Wait for each typed completion event before sending the next shell command;
while vi or another foreground reader runs, input belongs to that program.
A completion's `kind` distinguishes `exited` with the exact `exit_status`,
launch failure, builtin status and rejected input. Keep compiler diagnostics and
check for `exited` with status 0 before executing an output file that may
predate a failed build. `--no-shell-echo` removes the shell's prompt and input
redraws from captured output. Program output is base64 JSON data, separate from completion events.

Existing benchmarks can report through that same connection, for example:

```text
iobench read boot://share/iobench-small.bin --bytes 32768 --rounds 1
allocbench heap --rounds 64 --profile
```

These examples exercise report collection; one sample is not a performance
baseline. See [I/O measurements](io-ipc-baselines.md) and
[allocation profiling](allocation-profiling.md) for measurement contracts.
Tools requiring a delegated launcher, such as the pipe/IPC coordinators, still
need their documented local setup. A remote `session` handoff exits the root
shell and causes the server to terminate its remaining group.

## Fetch source over HTTPS

A controlled HTTPS endpoint can supply source through the same file interface.
For this example, arrange DNS for `tls.pyxis.test`, serve a small C program such
as the one above at `https://tls.pyxis.test:8443/hello.c`, and provide a certificate
valid for that name and the guest's UTC date. Networking must be enabled in QEMU. The controlled
CA's PEM certificate must be available as a native file, for example
`home://custom-ca.pem`; it contains the public CA certificate, never a private key.

In the guest, replace HTTPS with an instance that augments packaged public trust:

```text
service replace --read-only https boot://httpfs.pxb --https --ca-bundle home://custom-ca.pem
cat https://tls.pyxis.test:8443/hello.c > home://hello.c
cat home://hello.c
clang home://hello.c -o home://hello.pxe
home://hello.pxe
```

Inspect the saved source before compiling and run only code you trust. HTTPS
authenticates the server and transfer; it does not establish that a program is
safe. Check each command's result before proceeding. A failed HTTPS open exposes
no partial snapshot, but output redirection can already have created or truncated
its destination. A failed build can leave an old or partial executable as above.

The resulting source and executable remain RAM-backed. Use writable `host://`
paths for persistence. A public-CA endpoint uses the default HTTPS instance
without custom augmentation. See [HTTPS trust and updates](../userland/https.md) and
[provider configuration](../userland/http-fetch.md#use-and-startup) for setup, failure and
replacement rules.
