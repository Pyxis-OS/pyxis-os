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
output; literal tabs currently disappear because the TTY ignores them, while
Kilo expands them for its own display.

TCC reports compile and link errors through the terminal. Correct the source
and rebuild before running the output again: a failed build may leave an older
executable, or a partial file if output writing began. The shell reports nonzero
child exit status. Kilo saves by truncating and rewriting, so a failed save can
also leave partial content.

Sources, objects and executables in `home://` are RAM-backed and disappear on
reboot. `app://`, including `app://sdk`, is read-only. This workflow does not yet
provide persistent storage, atomic saves, a package manager or toolchain
self-hosting. GCC continues to build maintained OS/userland sources.

See [TCC's contract and limits](tcc.md), [Kilo controls](ports.md#editing-in-pyxis)
and [terminal behavior](terminal.md) for details.
