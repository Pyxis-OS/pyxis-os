# DevilutionX sound bundle qualification

DevilutionX ships as the program bundle `devilutionx.pxb` with its sound built.
Its manifest declares the grants SDL's [audio](../../sdl2.md#audio) needs, so
the ordinary `devilutionx` command plays sound and music. The owner assigned
this on 2026-10-13, after the [EDuke32 bundle](../eduke32-sound/README.md).
These are manual nested-QEMU functional results; timing is measured natively,
as for EDuke32 (see its [timing](../eduke32-sound/README.md#timing) section).

## Change

- **Sound library.** DevilutionX 1.5.5 builds sound with SDL_audiolib at the
  commit it pins itself, `cc1bb6af8d4cf5e200259072bde1edd1c8c5137e`. The
  recipe takes that release archive through the existing download cache,
  pinned by SHA-256; upstream's MD5 matches. Only the WAV and MP3 decoders are
  built, as upstream selects. One patch includes `<fmt/format.h>` for fmt 12.
- **Licences.** SDL_audiolib is under the LGPL 3 or later, its bundled Opus
  resampler under a BSD licence, and dr_wav and dr_mp3 are public domain or
  MIT-0. Their notices are staged with the program and listed in
  `PORT-NOTICE`. The build stays personal-use only, as before.
- **Patches.** The platform file no longer sets `NOSOUND`. Patch 3, renamed
  `libcxx-fmt12`, drops its `thread_local` workaround, which assumed Pyxis had
  no thread-local storage.
- **Bundle.** The recipe stages `bin/devilutionx.pxb` with `manifest.json` and
  `app/bin/devilutionx.pxe`.
  - Required grants: `memory/manage`, `clock` read and sleep, `display/draw`
    and `keyboard/input`.
  - Optional grants: `pointer/input`, `audio/create`, `threads/manage`,
    `pipe/create`, and `clipboard_local` and `clipboard_shared` (`publish`,
    `paste`) for the text editor's Copy and Paste.
- **Build and docs.** `scripts/ports.mk` adds the manifest to the recipe's
  inputs, and `scripts/assemble-initrd.sh` checks for the bundle's executable.
  The [standalone bundle](../../../userland/devilutionx.md#standalone-bundle)
  now copies the whole `.pxb` and starts it by that path, so it keeps its
  grants.

## Configuration

Pyxis main `ad86d6e5` with this change and the EDuke32 change (ports
`4c19fe5`, userland `99b425e`), builder `pyxis-llvm23.1.3-4c23a20`. QEMU as in
the [EDuke32 record](../eduke32-sound/README.md#configuration), four CPUs, with
the Diablo shareware `spawn.mpq`. The pull requests were then rebased onto
later mains with no content change, and the rebased heads were rebuilt with the
game data.

## Results

- **Sound.** From launch to the title screen the capture held two stretches of
  sound: the intro from 0.25 s to 11.1 s, and the title music from 23.4 s until
  the quit at 50.7 s with no all-zero gap of 1 ms or more. The intro held 69
  short all-zero runs, 188 ms in all, every one in its last 0.3 s as the video
  ended; they were not attributed further. No one listened.
- **Direct path.** Starting `bin://devilutionx.pxb/app/bin/devilutionx.pxe`
  launches a plain program. The terminal shows "Failed to initialize audio
  (Aulib::init): Couldn't create audio thread", and the game reaches its menu
  silently.
- **Clipboard.** In the hero name entry, typing `abc`, Shift+Home, Ctrl+C, End
  and Ctrl+V gave `ABCABC`: the bundle received the local clipboard grant.
- **Quit.** GDB was attached to a debugger ELF relinked from the release link
  command (the staged executable is identical to the build output), with
  `may-call-functions off`. Choosing Exit Diablo stopped at
  `PYXIS_CloseDevice`, called from `close_audio_device` under SDL_audiolib's
  `Aulib::quit` and the game's `snd_deinit`. At `SDL_TimerQuit` and at
  `libc_thread_shutdown_ready` libc's sibling records were `THREAD_FREE`, and
  `exit(0)` reached `run_exit_handlers` and `stdio_finish`.

## Timing

Not measured in nested QEMU, for the reasons in the EDuke32 record. The native
session compares the title screen and play between unchanged main and this
change.

## Native run sheet

Part of the owner's single ThinkPad session for Chocolate Quake, EDuke32 and
DevilutionX; the working sheet is in `/shared/present/game-sound-native/`. On
AC power:

1. **Listening.** Run `devilutionx`, let the intro and title music play, then
   start the shareware game and walk around town for a minute. Check that
   music and effects play without clicks, gaps or stutter, and change the
   volume from the bar while it plays.
2. **Clipboard.** In New Hero's name entry, type a name, select it with
   Shift+Home, copy with Ctrl+C, press End and paste with Ctrl+V. The name
   should appear twice.
3. **Quit.** Exit Diablo from the main menu. The shell prompt returns. Then
   check `log | grep hda:` for the teardown trip.

Report whether sound was clean, any felt latency, the clipboard result and any
`hda:` lines.
