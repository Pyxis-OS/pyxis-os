# Edit, build and run inside Pyxis

The normal image includes Kilo, TCC and the target headers/libraries needed to
create a C program, compile it and run it entirely inside Pyxis.

Build and boot with the normal [SDK and ports setup](ports.md#building-and-packaging):

```sh
make image
make run CPUS=4
```

Select the application space with Super+Right. From the shell's initial `home://`
directory, run `kilo hello.c` and enter:

```c
#include <stdio.h>

int main(void)
{
  puts("Hello from Pyxis");
  return 0;
}
```

Save with Ctrl-S and quit with Ctrl-Q, then compile and launch:

```text
tcc hello.c -o hello.pxe
./hello.pxe
```

Reopen `kilo hello.c`, change the message, save and quit. Repeat the same compiler
and launch commands to see the updated output. No ISO rebuild or host conversion
is needed between edits. Use `cat hello.c` to inspect saved bytes as terminal
output; the TTY displays tabs at eight-column stops, while Kilo expands them
for its own display.

TCC reports compile and link errors through the terminal. Correct the source
and rebuild before running the output again: a failed build may leave an older
executable, or a partial file if output writing began. The shell reports nonzero
child exit status. Kilo saves by truncating and rewriting, so a failed save can
also leave partial content.

Sources, objects and executables in `home://` are RAM-backed and disappear on
reboot. For opt-in persistence, use the [writable host export](../devices/virtio-fs.md#persistent-development-walkthrough)
and keep source and output under `host://`. `app://`, including `app://sdk`, is
read-only. Atomic Kilo saves, a package manager and toolchain self-hosting
remain unsupported. GCC continues to build maintained OS/userland sources.

See [TCC's contract and limits](../userland/tcc.md), [Kilo controls](ports.md#editing-in-pyxis)
and [terminal behavior](../userland/terminal.md) for details.

## Use a remote terminal

From the repository root, build the native host client and boot four CPUs with
networking and loopback forwarding:

```sh
make -C tools remote
make run CPUS=4 VIRTIO_NET=1 TCP_FORWARD=2323:2323
```

Leave QEMU running. In another host terminal, connect from the repository root:

```sh
build/tools/pyxis-remote 127.0.0.1 2323
```

The same Kilo, TCC and execution commands above work in the remote shell.
An optional HOST export is writable from the default Remote space, subject to
host permissions. Remote and local sessions share the actual filesystem roots;
choose distinct output names when working concurrently.

For command tools without a controlling terminal, use the
[persistent machine-client workflow](../userland/remote-terminal.md#persistent-use-through-command-tools).
Wait for each typed completion event before sending the next shell command;
while Kilo or another foreground reader runs, input belongs to that program.
A completion's `kind` distinguishes `exited` with the exact `exit_status`,
launch failure, builtin status and rejected input. Keep compiler diagnostics and
check for `exited` with status 0 before executing an output file that may
predate a failed build. `--no-shell-echo` removes the shell's prompt and input
redraws from captured output. Program output is base64 JSON data, separate from completion events.

Existing benchmarks can report through that same connection, for example:

```text
iobench read app://share/iobench-small.bin --bytes 32768 --rounds 1
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
service replace --read-only https app://httpfs.pxe --https --ca-bundle home://custom-ca.pem
cat https://tls.pyxis.test:8443/hello.c > home://hello.c
cat home://hello.c
tcc home://hello.c -o home://hello.pxe
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
