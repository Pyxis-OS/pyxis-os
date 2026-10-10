# Port sweep after the libc closure

After Neovim task 6's libc slices added `mktime`, `strcoll`, `getcwd`/`chdir`,
`setenv`/`unsetenv`, proved `realpath`, `fdopen`, `dup`, a bounded `iconv`,
`strtoimax`, `atol`, `strtok_r`, `trunc` and `isnan`/`isinf`, every port's
patches, local sources and configuration headers were searched for
workarounds that existed only because one of those functions was missing.
A workaround was removed only where the real function now gives upstream's
behaviour; adaptations that encode a Pyxis policy stay.

## Changed

| Port | Before | After |
| --- | --- | --- |
| lua (5.5) | `os.time(table)` refused; `l_strcoll` mapped to `strcmp` | upstream's table path through `mktime`, and `strcoll` |
| sdl2 | `SDL_config.h` declared `getenv` but not `setenv`, so `SDL_setenv` stored values that `SDL_getenv` never read; SDL's own `strtok_r` and `trunc` | libc's `setenv`, `strtok_r` and `trunc` |
| quake | atomic saves closed `mkstemp`'s descriptor and reopened the name | `fdopen` on that descriptor |
| eduke32 | `fopenfrompath` compiled out | upstream's function, using `fdopen` (only the unbuilt Mapster32 editor calls it) |
| links | a patch guard excluded Links' fallback `getcwd` | `HAVE_GETCWD` declared in `config.h`, so upstream excludes it itself |

## Kept, with reasons

- **lua, lua51:** `os.clock`, `os.execute`, `io.popen`, `file:setvbuf` and C
  modules: libc has no `clock`, `system`, `popen`, `setvbuf` or dynamic
  loading. The decimal point stays `.` (no `localeconv`). Lua 5.5 keeps its
  wall-time reader, which reports clock failures.
- **sdl2:** `SDL_iconv` stays SDL's own: libc's `iconv` lacks the UCS-2,
  UCS-4 and UTF-32 forms SDL converts text through.
- **links:** HTTP date parsing stays off (`HAVE_MKTIME` undefined). Without
  `timegm`, upstream's fallback measures the zone offset on 1980-01-01 and
  misreads dates across a DST change.
- **tcc:** output files use `fopen` instead of permission-mode `open` plus
  `fdopen` (Pyxis files have no modes); `#pragma once` compares native file
  identities rather than `realpath` spellings; debug information names the
  working directory through `pyxis_working_path`, which handles an unknown
  path where upstream ignores a `getcwd` failure.
- **chocolate-doom:** the video driver goes through SDL's hint, because libc
  deliberately has `setenv` but no `putenv`; `localeconv` and `EISDIR`
  remain absent.
- **busybox:** tar's `--to-command`, whose `putenv` went with it, needs a shell
  command runner, not a libc function.
- **eduke32:** output redirection stays off (no `freopen` or `setvbuf`).
- **fmt:** `FMT_OS` stays off; `fmt/os.h` also needs `dup2`, `pipe` and
  `fcntl`.
- **libuv, fastfetch, kilo, sbase, mbedtls, zlib, libpng, doom,
  chocolate-quake, devilutionx:** nothing tied to these functions. libuv and
  fastfetch already use the real working path and environment; mbedtls's
  time hooks use the native clock.

## Checks

QEMU 10.2.2 with the local AHCI fix, nested KVM, q35, 4 CPUs, standard VGA, on
main `b0d0bc07` with ports at the branch head, then rebased onto `d22cfc05` (#682,
kernel input only). Scratch scripts and programs were
never committed.

- **Lua 5.5:** a script ran under `bin://lua.pxe` with `TZ` set to UTC,
  Europe/Bucharest and America/New_York, and under host Lua 5.4.8 (glibc 2.43)
  reading the image's TZif files. It covered `os.time` tables (normalization,
  pre-1970 times, gaps, folds, `isdst`, missing and non-integer fields, an
  out-of-range year), `os.date` formats, `os.getenv`, comparison and sorting.
  51 of 57 lines matched. The six differences are the
  [calendar profile](../../../userland/timezones.md#c-interface): gaps and
  unsettled folds raise upstream's "cannot be represented" error where glibc
  normalizes, and UTC with `isdst=true` keeps the time where glibc assumes one
  hour.
- **SDL2:** a program called `SDL_setenv`, `SDL_getenv`, `getenv`,
  `SDL_strtokr` and `SDL_trunc`. Against main's SDL2, `SDL_getenv` and
  `getenv` returned NULL after a successful `SDL_setenv`; against this branch,
  the output matched host sdl2-compat 2.32.74 line for line.
- **Quake:** with the shareware data, `quake -writedir host://qsave +map
  start`, then `save s1` and `quit` in the console, wrote a complete `s1.sav`
  and `config.cfg`, with no temporary files left.
- **Links:** `links page.html` from `host://` opened the relative page.
- **EDuke32:** the recipe built with the restored function and no new
  warnings. It was not run: the function is unused and the game data is
  personal.
