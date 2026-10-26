# Chocolate Quake sound bundle qualification

Chocolate Quake ships as the program bundle `chocolate-quake.pxb`. Its manifest
declares the grants SDL's [audio](../../sdl2.md#audio) needs, so the ordinary
`chocolate-quake` command plays sound effects. The owner assigned this on
2026-10-13 as the follow-up to the SDL audio task. These are manual
nested-QEMU results; the native check uses the [run sheet](#native-run-sheet)
below.

## Change

- **Ports recipe.** The recipe stages `bin/chocolate-quake.pxb` with
  `manifest.json` and `app/bin/chocolate-quake.pxe`, replacing the plain
  `bin/chocolate-quake.pxe`.
  - Required grants: `memory/manage`, `clock` read and sleep, `display/draw`
    and `keyboard/input`.
  - Optional grants: `pointer/input`, `audio/create`, `threads/manage` and
    `pipe/create`.
  - Licences stay in `share/licenses/chocolate-quake/`.
- **Command.** Bare `chocolate-quake` finds no `bin://chocolate-quake.pxe` and
  selects the bundle, as `lua` selects `lua.pxb`.
- **Build inputs.** `scripts/ports.mk` adds the manifest to the recipe's
  inputs.
- **Unchanged.** Game data handling is unchanged: the shareware pak or local
  `QUAKE_DATA`, never committed. The executable, patches and SDL library are
  unchanged.

## Revisions and configuration

- Pyxis main `4a09711e`, ports `c7e4bc3` before the change, builder
  `pyxis-llvm23.1.3-4c23a20`. Both images carry the same kernel (`caelum.elf`
  SHA-256 prefix `f74da10d`).
- QEMU 10.2.2 with the local AHCI fix, nested KVM in Claude's Fedora VM, Q35,
  `-cpu max`, 2 GiB, standard VGA at 1280x800, `intel-hda` + `hda-output`
  writing a 48 kHz S16 stereo WAV, `-display none`. Input arrived through QMP.
  Shareware data. One and four CPUs.
- The baseline image is the unchanged shipping image (plain program). The
  changed image adds local probe bundles that Quake does not use.

## Results

`+timedemo demo1` (969 frames) frames per second, five runs each, median in
bold. Starts that failed the HDA engine were rebooted and repeated (see below).

| Configuration | 4 CPUs | 1 CPU |
| --- | --- | --- |
| Before: plain program, silent | 300.5, 316.5, 430.5, 434.1, 446.9 → **430.5** | 438.8, 440.7, 441.4, 442.9, 444.5 → **441.4** |
| Bundle, `-nosound` | 454.1, 459.7, 462.3, 464.6, 471.9 → **462.3** | 393.2, 457.8, 459.5, 459.5, 465.2 → **459.5** |
| Bundle, with sound | 384.2, 400.9, 417.8, 424.5, 433.5 → **417.8** | 371.6, 372.4, 377.0, 379.0, 385.7 → **377.0** |

- **Sound initialization.** Every with-sound run logged "SDL audio driver:
  pyxis" and a 44.1 kHz, 1,024-sample stereo spec. SDL converts this to the
  native 48 kHz.
- **Without sound.** The bundle is not slower than the plain program.
- **Cost of sound.** With sound, the uncapped timedemo ran about 10% (four
  CPUs) and 18% (one CPU) below `-nosound`. Quake's own mixing, SDL's
  conversion, the callback thread and native writes are included, and not
  attributed further.
- **Before.** Every plain run logged "Couldn't open SDL audio: Couldn't create
  audio thread".
- **Captured output.** Thirty seconds of the ordinary demo loop with sound
  produced 35.9 s (four CPUs) and 36.2 s (one CPU) of HDA stream with game
  sound throughout: 93% of frames nonzero, peak 637 and 646 at the boot master
  volume of 50%.
  - Both captures have the same 154 all-zero runs of 1 ms or more, with the
    same lengths apart from the first, which is stream start.
  - The silences therefore follow the deterministic demo's content, not
    timing; no CPU-count-dependent gap appeared.
  - No one listened to these runs.

## Grants, refusal and quit

- **Direct path.** Starting the executable by its
  `bin://chocolate-quake.pxb/app/bin/chocolate-quake.pxe` path launches it as a
  plain program. It logged "Couldn't open SDL audio: Couldn't create audio
  thread" and played the timedemo silently.
- **Quit.** GDB was attached to the four-CPU image with a debugger ELF relinked
  from the same objects (`-Wl,--oformat=elf`; the staged `.pxe` is identical to
  the build output), with `may-call-functions off`.
  - Quitting through the console and end screen stopped first at
    `PYXIS_CloseDevice`, called from `close_audio_device` under Quake's
    `SNDDMA_Shutdown`.
  - At `libc_thread_shutdown_ready`, libc's sibling records were `THREAD_FREE`.
  - `exit(0)` then reached `run_exit_handlers` and `stdio_finish`, libc's
    orderly path.
  - Chocolate Quake on Pyxis starts no SDL timer thread (its port skips
    `SDL_INIT_TIMER`), so the audio callback thread was the only sibling.

## Nested-QEMU engine failures

The HDA engine's existing QEMU guards again disabled audio until reboot in
some runs, logging either "output DMA commit exceeded clock limit" or "output
completion notification expired". With sound, 5 of 10 four-CPU timedemo
attempts and 2 of 7 one-CPU attempts were followed by a failed engine. Those
runs were repeated after a reboot. `-nosound` and plain runs open no audio.
The guard bounds are unchanged.

- **When the guard tripped.** The runner checked the log only after quitting,
  so these counts do not show whether the guard tripped at stream start or at
  quit. A later recheck during the
  [EDuke32 qualification](../eduke32-sound/README.md#nested-qemu-engine-failures)
  logged before and after the quit in four fresh four-CPU boots. Three were
  clean; one tripped during the quit and none earlier.
- **Quit trips.** Trips during the quit come from process teardown stalling the
  audio worker, which the EDuke32 record shows. They do not invalidate the
  timedemo that ran before.

## Native run sheet

For the owner's ThinkPad check, on AC power. Build and stage the PXE image as
usual, from this PR's head or after merge, then in the Development space:

1. **Sound plays.** Run `chocolate-quake`, let the demo loop play for a
   minute, and use the menu to hear menu sounds. Check that effects stay in
   step with the action and that there are no clicks, gaps or stutter. Change
   the volume from the bar while it plays.
2. **Quit.** Open the console with the backquote key, type `quit`, answer `y`,
   then press any key on the end screen. The shell prompt should return.
3. **Timedemo, three runs each.** Note the frames-per-second line of each:
   - `chocolate-quake +timedemo demo1`;
   - `chocolate-quake -nosound +timedemo demo1`.
4. **Plain refusal.** Run `bin://chocolate-quake.pxb/app/bin/chocolate-quake.pxe`
   and quit. It should be silent; `grep -i audio
   home://chocolate-quake/id1/qconsole.log` after `-condebug` shows "Couldn't
   create audio thread".
5. **Engine health.** Run `log` (or read the serial/UDP log) and report any
   `hda:` line, especially "engine unavailable until reboot". The native
   profile uses different bounds from QEMU.

Report the FPS values, whether sound was clean, any felt latency between
action and sound (calculated worst case about 140–160 ms; tuning is
[deferred](https://git.internal/PyxisOS/pyxis-os/issues/1397)), and any `hda:` lines.

## Native results so far

Owner session, 2026-10-13, ThinkPad on AC power, image `54a02657` (#803).
Chocolate Quake played with sound. Its quit tripped "output observation
exceeded service horizon" and disabled audio until reboot. In the
trace-logging diagnostic boot (`e83ae1fc`, not merged), its quit's process
teardown took **39.7 ms** on CPU 0 with interrupts off, where the audio worker
runs; that boot's audio had already failed at codec setup, so it shows no trip.
This is the teardown cause recorded for EDuke32. Pyxis-os #807 restarts the
stream after such faults instead of failing; bounded teardown follows. The
timedemo comparison and listening steps above were not run.
