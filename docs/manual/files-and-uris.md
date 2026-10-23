# Files and URIs

Pyxis names a file with a URI: a root name, `://` and a path, such as
`home://notes/a.txt`. Every command takes these names, and `cd` accepts a root
on its own, so `cd tmp://` moves you into it.

## The roots

| Root | What it is | Written by you |
| --- | --- | --- |
| `home://` | Your files. Kept on the disk across reboots. | Yes |
| `tmp://` | Scratch space in memory. Emptied at every reboot. | Yes |
| `system://` | The system volume. Its `config/` directory holds the configuration files. | Yes |
| `boot://` | The boot archive: the boot and rescue programs, the installer, `os-release` and shared data. | No |
| `bin://` | The installed commands. | No |
| `pkg://` | The files of each installed package, one directory per package. | No |
| `sdk://` | Headers and libraries from the installed `-dev` packages. | No |

A shell starts in `home://`.

## Look around

1. List a root with `ls`:

   ```text
   home://> ls system://
   .pyxis-classic  SAFE_TO_WIPE  config/
   home://> ls system://config
   boot.lua  init-installed  init-services  network.lua  packages.lua  session.lua
   home://> ls pkg://
   acpidump/  cksum/  config/  date/  dig/  fastfetch/  ...
   home://> ls sdk://
   include/  lib/
   ```

   Directories are shown with a trailing `/`. The listing of `bin://` is
   the commands you can run by name.

2. Read a file from any root:

   ```text
   home://> head sdk://include/stdio.h
   #ifndef LIBC_STDIO_H
   #define LIBC_STDIO_H

   #include <stdarg.h>
   ...
   ```

## Copy between roots

`cp` works across roots, so it is how you take a copy of a read-only file or a
configuration file:

```text
home://> cp boot://os-release tmp://
home://> cat tmp://os-release
return {
  revision = "BUILD",
  ...
}
home://> cp system://config/network.lua home://network-copy.lua
home://> cat home://network-copy.lua
return {
  wired = { auto = true, exclude = {} },
  ...
}
```

After `cd tmp://`, `ls` lists `tmp://` and the prompt reads `tmp://>`.

## Read-only roots

You cannot write to `boot://`, `bin://`, `pkg://` or `sdk://`:

```text
home://> echo hi > boot://x.txt
shell: redirect: boot://x.txt: Permission denied or path escapes its boundary
```

To change what is in `pkg://`, `bin://` or `sdk://`, install or remove a
package ([Packages with pkg](packages.md)).

## What survives a reboot

Files in `home://` and `system://` stay. Files in `tmp://` do not:

```text
home://> reboot
...
home://> ls tmp://
home://> ls home://
.history  network-copy.lua
```

Copy anything you want to keep from `tmp://` into `home://` first.
