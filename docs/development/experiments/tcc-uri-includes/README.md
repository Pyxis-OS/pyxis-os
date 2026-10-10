# TCC URI include qualification

QEMU checks for [TCC includes by URI](../../../userland/tcc.md#includes-by-uri),
TCC patch `0009-uri-includes.patch`, on main `4e7343cd`. Scratch sources and
servers were never committed.

## Configuration

- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM, q35, 4 CPUs, 8 GiB,
  standard VGA, live image, Development tab, `VIRTIO_NET=1`. That is QEMU's
  user-mode NAT: the guest reaches the internet and the host only through QEMU,
  not the host's LAN.
- **Remote:** GitHub over HTTPS through the packaged root store. A local Python
  server on the host, reached by the guest at `10.0.2.2:18080` over plain HTTP,
  served controlled headers, a 302 alias and an HTTP 500.
- **Sources:** a `host://` virtio-fs share.

## Demos

| Command | Result |
| --- | --- |
| `tcc host://tu/c1.c`, including `https://gist.githubusercontent.com/RabaDabaDoba/145049536f815903c79944599c6f952a/raw/fb1503af3caaf4d1188f3f9b6c356b219d6a06eb/ANSI-color-codes.h` | compiled; printed in bold green |
| `tcc <same revision URL>/testmain.c` | its `#include "ANSI-color-codes.h"` resolved beside the URL; printed the gist's coloured lines |
| `tcc host://tu/c11.c`, defining `STB_IMAGE_IMPLEMENTATION`, `STBI_ONLY_PNG`, `STBI_NO_LINEAR`, `STBI_NO_SIMD` and `STBI_NO_THREAD_LOCALS`, then including `https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h` (283 KB) | compiled and linked; decoding a 37×23 PNG printed `3 channels, first pixel 255 0 0, last pixel 0 200 0`, as written |

The gist's `ANSI-color-codes.h` has no include guard and uses `\e`, which TCC
accepts. stb needs `STBI_NO_THREAD_LOCALS`, because TCC on Pyxis has no TLS,
and `STBI_NO_SIMD`.

## Rules

Requests were checked in the local server's log.

| Case | Requests | Result |
| --- | --- | --- |
| `once.h` (`#pragma once`) included twice | one `GET /inc/once.h` | compiled, value 1 |
| `sub/a.h` including `"../b.h"` | `GET /inc/b.h` (the patch-less build sent `/inc/sub/../b.h`) | `A` = 3 |
| `once.h`, then `/alias/once.h` (302 to `/inc/once.h`), `inc/./sub/../once.h` and `HTTP://…/inc/once.h#again` | `once.h`, the alias and its redirect; nothing for the last two | compiled; the alias was skipped by its final URL |
| An explicit `missing.h` (404) | one request | `include file '…' not found` |
| A refused connection | none | one error: `could not open '…': Input/output error` (two lines before the patch) |
| A remote header including `"local-only.h"`, which the server lacks (404) and `-I host://tu` has | 404, then the local file | compiled |
| A remote header including `"err500x.h"`: HTTP 500 there, present in `-I host://tu` | one 500 | stopped: `could not open '…/inc/err500x.h': Input/output error` |
| `-I http://10.0.2.2:18080/inc` | none | `search directories must be local, not '…'; name remote headers in #include` |

Native behaviour is unchanged: `#pragma once` across `"nonce.h"` and
`host://tu/nonce.h` compiled one definition. `cat` compiled with
`-include stdbool.h` and ran.

## Limits

- QEMU only; native runs follow in an owner batch.
- A failed open reports errno, so an HTTP 500 reads as an input/output error,
  without the status.
- No `#sha256=` verification; the pin is the immutable URL.
