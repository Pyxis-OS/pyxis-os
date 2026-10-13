# EDuke32 sound bundle qualification

EDuke32 ships as the program bundle `eduke32.pxb`. Its manifest declares the
grants SDL's [audio](../../sdl2.md#audio) needs, so the ordinary `eduke32`
command plays sound effects and music. The owner assigned this on 2026-10-13,
after the [Chocolate Quake bundle](../chocolate-quake-sound/README.md). These are
manual nested-QEMU functional results. Timing is measured natively instead: the
owner decided on 2026-10-13 to time the game ports on the ThinkPad, because
nested runs on the shared host were unusable (see [timing](#timing)).

## Change

- **Ports recipe.** The recipe stages `bin/eduke32.pxb` with `manifest.json`
  and `app/bin/eduke32.pxe`, replacing the plain `bin/eduke32.pxe`.
  - Required grants: `memory/manage`, `clock` read and sleep, `display/draw`
    and `keyboard/input`.
  - Optional grants: `pointer/input`, `audio/create`, `threads/manage`,
    `pipe/create`, and `clipboard_local` and `clipboard_shared` (`publish`,
    `paste`) for the console's paste. Bundles can request the clipboard grants
    since the userland change accepted by the owner the same day.
- **Thread patch.** SDL's callback thread mixes beside the game, so patch 2,
  renamed `pyxis-thread-limits`, no longer turns upstream's thread-local state
  into plain globals. Before, the audio library's lock depth (`MV_Locked`) was
  shared between threads and would have raced with the callback. The patch now
  leaves out only loguru's signal handlers, stack traces and pthread thread
  names, and the audio library's async tasks, which only the omitted Vorbis and
  XMP decoders use.
- **Sound.** Upstream's SDL driver (`driver_sdl.cpp`) opens a 48 kHz stereo
  device, the native rate, so SDL converts nothing. Music is upstream's default
  AdLib OPL3 emulation, mixed in the same callback.
- **Command and build.** Bare `eduke32` selects the bundle, as `lua` selects
  `lua.pxb`. `scripts/ports.mk` adds the manifest to the recipe's inputs, and
  `scripts/assemble-initrd.sh` checks for the bundle's executable.
- **Unchanged.** The game data handling, the other patches and the SDL library
  are unchanged. EDuke32 stays an opt-in, personal-use build.

## Configuration

- Pyxis main `ad86d6e5` with this change (ports `d3bda66`, userland `99b425e`),
  builder `pyxis-llvm23.1.3-4c23a20`. Earlier runs used the same change on main
  `3455b3b2`. The pull requests were then rebased onto later mains with no
  content change, and the rebased heads were rebuilt with the game data.
- QEMU 10.2.2 with the local AHCI fix, nested KVM in Claude's Fedora VM, Q35,
  `-cpu max`, 2 GiB, standard VGA at 1280x800, `intel-hda` + `hda-output`
  writing a 48 kHz S16 stereo WAV, `-display none`. Input arrived through QMP.
  One and four CPUs. The owner's `duke3d.grp` (Atomic Edition).

## Results

- **Sound.** The log reports "Using SDL PYXIS driver on System audio output
  device" and "Initialized sound at 48.0 KHz stereo with 64 voices". Standing
  at the start of E1L1 for 25 s, the capture held 24 s of sound and music after
  the level load (77–81% of frames nonzero, peak 750–760) with no all-zero gap
  of 1 ms or more, on the base revision and again on the rebased head. No one
  listened.
- **Direct path.** Starting `bin://eduke32.pxb/app/bin/eduke32.pxe` launches a
  plain program. Sound startup fails with "Failed initializing sound: SDL Audio:
  error in OpenAudio" and the game runs silent.
- **`-ns`.** Upstream's flag only turns sound and music off; the device stays
  open and mixes silence. Use the direct path for a run without audio.
- **Scripts.** A bundle started from a shell script receives no threads or pipe
  grant, because the script's shell is itself a plain program. From a script,
  `eduke32` therefore starts without sound. Start it from an interactive shell.
- **Quit.** GDB was attached to the four-CPU image with a debugger ELF relinked
  from the same objects (only the build-date object differs), with
  `may-call-functions off`. Quitting through the console stopped first at
  `PYXIS_CloseDevice`, called from `close_audio_device` under `SDL_AudioQuit`
  and the game's `SDLDrv_PCM_Shutdown`. At `SDL_TimerQuit` libc's sibling
  records were `THREAD_FREE`, and `exit(0)` reached `run_exit_handlers` and
  `stdio_finish`. EDuke32 initializes SDL's timer subsystem but never adds a
  timer, so the Pyxis SDL port starts no timer thread; the audio callback
  thread was the only sibling.

## Timing

Not measured. Matched nested runs (bundle with sound, the same binary launched
directly, and unchanged main) were attempted with EDuke32's own demo profiler
(`-d1:1` on a locally recorded demo, uncapped through `autoexec.cfg`) and with
whole-QEMU CPU at the 60 Hz cap. Two owner container builds on horse, other
host load (VM steal up to about 54 s per 100 s run) and another agent's QEMU in
the same VM made the runs unusable: with-sound starts tripped the audio guards
in nearly every run. All of those numbers were discarded. The owner chose
native timing on the ThinkPad instead; its run sheet is at the
[end](#native-run-sheet).

## Nested-QEMU engine failures

The HDA engine's existing QEMU guards disable audio until reboot, logging "output
DMA commit exceeded clock limit", "output completion notification expired" or
"output observation exceeded service horizon". Three causes were separated:

- **Process teardown.** Quitting EDuke32 tripped the 20 ms service-horizon
  guard in every four-CPU boot, after the audio device had closed cleanly. The
  same trip followed quitting EDuke32 launched by its `app/bin` path, with no
  audio session at all, while the native `pcm` tool played in another space:
  three of three valid boots. EDuke32's private memory (about 160 MiB, backed
  eagerly) is freed at exit; by code inspection, process destruction and the
  audio worker both run on the BSP. This is recorded in
  [technical debt](../../../technical-debt.md#hd-audio-sustained-eight-session-playback)
  as a native check.
- **Screen capture.** QMP screendumps of the running game tripped the 1 ms
  commit guard; one boot tripped only after five screendumps. Runs with sound
  avoided screen capture until the game had exited.
- **Host load.** Stream starts tripped far more often while the host was
  loaded, as in the [SDL audio record](../sdl-audio/README.md#nested-qemu-engine-failures).

The guard bounds are unchanged.

## Native run sheet

The owner times and listens to Chocolate Quake, EDuke32 and DevilutionX in one
ThinkPad session with a matched PXE pair, unchanged main and this change with
the DevilutionX bundle. The working sheet is in
`/shared/present/game-sound-native/`. For EDuke32, on AC power:

1. **Timing.** Fetch the recorded demo and `autoexec.cfg` (`r_maxfps 0`) into
   `home://eduke32/`. Then run `eduke32 -d1:1 2> home://bench/FILE` three
   times with sound, and `bin://eduke32.pxb/app/bin/eduke32.pxe -d1:1` three
   times without. On the base image, run plain `eduke32` instead. Each run
   exits by itself and prints frames and times. After each run with sound,
   `log | grep hda:` must be empty; otherwise reboot before the next.
2. **Listening.** Run `eduke32 -v1 -l1 -s2` and play a minute: effects,
   music, and the volume bar while it plays.
3. **Quit.** `quit` in the console (the key under Escape). The shell prompt
   returns. Then check `log | grep hda:` for the teardown trip.

Report frames per second (frames divided by the wall time between "E1L1" and
the result lines), whether sound was clean, any felt latency and any `hda:`
lines.
