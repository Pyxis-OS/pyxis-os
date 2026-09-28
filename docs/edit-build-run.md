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
reboot. For opt-in persistence, use the [writable host export](virtio-fs.md#persistent-development-walkthrough)
and keep source and output under `host://`. `app://`, including `app://sdk`, is
read-only. Atomic Kilo saves, a package manager and toolchain self-hosting
remain unsupported. GCC continues to build maintained OS/userland sources.

See [TCC's contract and limits](tcc.md), [Kilo controls](ports.md#editing-in-pyxis)
and [terminal behavior](terminal.md) for details.

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
without custom augmentation. See [HTTPS trust and updates](https.md) and
[provider configuration](http-fetch.md#use-and-startup) for setup, failure and
replacement rules.
