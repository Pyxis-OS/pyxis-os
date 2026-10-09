# SDL2 event-wait qualification

Manual matched QEMU qualification on 2026-10-09. The change is in the ports
SDL2 adapter and an ordered upstream-core patch; no kernel, SDK ABI, grant or
session rule changed. Ports must merge before the Pyxis gitlink/docs PR.

## Revisions and consumer

Both runs used Pyxis `26770a0c`, userland
`fe6f3efb0847cc500a68cb782304eab781bdff58`, the same SDK and compiler image
`pyxis-llvm23.1.3-49e2c1a`, and SDL 2.32.10 upstream commit
`5d249570393f7a37e037abf22cd6012a4cc56a71`.

- Before: ports `a642f07382e14bd233ac1be2b6a814e95c32d835`.
- After: ports `f2da003d42d608af302e801bc9c92771391c2f54`.
- Identical kernel SHA-256 in both ISOs:
  `5c7607aa4ea8f7a74283127dee24e24b5a4c06d37db43cf41e929104ce3edeef`.
- Before consumer SHA-256:
  `8a69107c58b6e6beba4988a0e7dfd41bc7df5a652f007e02564e886f804fba04`.
- After consumer SHA-256:
  `f6634715017bcf3f30fcfff9a570ed273e1622c591aa09ffba18409328536c77`.
- Before ISO SHA-256:
  `f25d7115897f54c93886f823ed387009d6febff3c3848a024e0154b53c2dff34`.
- After ISO SHA-256:
  `733a307365417c0cc31718651f6617bfe005311556e1ead5369383e7dacecb67`.

The consumer was upstream's existing, unmodified `test/checkkeysthreads.c`
(source SHA-256
`abc1f3f45eda12d199afe4f0680cda87f012e603041500fffe6e571ce252ebb3`).
It was compiled with the exported SDK flags and SDL headers, then linked with
`libSDL2.a` before the SDK libraries. The local qualification image added
`checkkeys.pxe` and SDL's license to the ordinary staged tree. The only changed
payload between the two qualification archives was that executable, relinked
with the changed SDL. The local fixture, ELF symbols and ISO are not shipped,
and no test recipe, test library or CI automation was added.

The program initializes video, creates a renderer, presents once and enters
`SDL_WaitEvent`. Its optional `SDL_CreateThread` fails in this threadless
profile, but the main keyboard/mouse loop continues. The thread-generated key
subtest is unsupported and was not qualified. DevilutionX menus and Quake's
SDL path call `SDL_PollEvent`, so they are not blocking-wait consumers.

The ports branch carries the previously approved SDL pointer migration forward
unchanged: ports #66 was merged into the Quake branch rather than ports main,
while Pyxis already pins its source. Merging ports main into the pinned source
produced no content changes. The new wait delta is relative to `a642f073`.

## Matched VM and method

QEMU 10.2.2, Q35, nested KVM in the Fedora VM, `-cpu max`, four CPUs
(one socket, four cores, one thread), 512 MiB, UTC RTC, fresh matching raw
OVMF CODE/VARS per boot, modern VirtIO GPU, modern VirtIO SCSI CD, modern
VirtIO RNG, no NIC/disk/USB/audio, `-display none`, serial log and HMP monitor.
PS/2 keyboard and relative mouse input came from manual HMP commands.
The display was 1280x800 with a 1280x768 fullscreen SDL content area.
Both runs launched the consumer from the Development shell with its inherited
display, keyboard, pointer and clock grants. Both input sessions and graphics
were acquired, and the first frame was presented.

Read-only GDB inspection used `set may-call-functions off`, the identical
kernel ELF and a matching consumer ELF. It confirmed the before consumer in
SDL's polling loop and the after consumer in the video wait hook with timeout
`-1`, display `WAIT_RESIZED` at generation 1, keyboard `WAIT_READABLE` and
pointer `WAIT_READABLE`. No guest function calls or memory writes were used.

## Idle CPU

With no input, builds or active breakpoints, manual host `/proc/PID/stat`
endpoints recorded aggregate QEMU user + system CPU ticks. `CLK_TCK` was 100.
Each percentage is `(CPU seconds / host monotonic elapsed seconds) * 100`;
100% is one host core. Durations differ because endpoints were entered
manually. This is whole-QEMU CPU, including the unchanged kernel display
presenter and device work, not SDL-only CPU, guest wake counts or native power.

Raw host monotonic timestamps are nanoseconds; CPU columns are cumulative
ticks. The gap between before intervals 1 and 2 contains debugger/input work
and is excluded.

| Run | Interval | Start ns; user/system ticks | End ns; user/system ticks | Elapsed s | CPU s | CPU % |
| --- | --- | --- | --- | ---: | ---: | ---: |
| Before | 1 | 64830571677380; 1196/558 | 64866178243351; 4120/2053 | 35.607 | 44.19 | 124.11 |
| Before | 2 | 64981239346044; 8427/4041 | 65006450617722; 10432/5105 | 25.211 | 30.69 | 121.73 |
| Before | 3 | 65006450617722; 10432/5105 | 65035273767193; 12733/6296 | 28.823 | 34.92 | 121.15 |
| After | 1 | 65457373957864; 1336/586 | 65508970640302; 2460/1074 | 51.597 | 16.12 | 31.24 |
| After | 2 | 65508970640302; 2460/1074 | 65540854398367; 3283/1439 | 31.884 | 11.88 | 37.26 |
| After | 3 | 65540854398367; 3283/1439 | 65575356901776; 3994/1760 | 34.503 | 10.32 | 29.91 |

Median CPU was **121.73% before and 31.24% after**, a **74.3% reduction** in
these matched nested runs. There was one boot per revision, with three warm
idle intervals per boot. This does not establish a native or universal saving.

## Input delivery

Four manually injected `A` presses and four relative `mouse_move 20 0`
reports per revision were observed using the QEMU HPET counter
`0xfffffe80402020f0` (10 ns ticks). Intervals run from native
`keyboard_route_event` / `pointer_queue_input` entry to
`SDL_SendKeyboardKey` / `SDL_SendMouseMotion` entry. The consumer stayed in
its event loop and received the events. Debugger stops pause the VM clock;
scheduling and debugging still perturb these small samples.

| Input | Before ms | After ms | Median before / after ms |
| --- | --- | --- | --- |
| Keyboard | 3.64535, 3.12224, 1.48355, 2.84487 | 1.64953, 1.97449, 1.67655, 1.26678 | 2.98356 / 1.66304 |
| Pointer | 6.37214, 2.55153, 4.38370, 2.76843 | 2.57624, 5.63641, 4.01823, 2.42154 | 3.57607 / 3.29724 |

These samples showed no regression in native-queue-to-SDL delivery. They
exclude PS/2 decoding, the rest of the consumer handler, rendering and
packet-to-screen latency. They are not native latency qualification.

An initial local version performed another readiness poll after the blocking
wait. Its keyboard/pointer samples were slower; it was replaced by consuming
the successful wait observation once in the next pump. The table and after
revision above describe the final version only.

The infinite wait was also observed returning native `CALL_TIMED_OUT` (17)
while SDL's timeout remained `-1`, then re-arming all three interests without
returning to the consumer. Consecutive native deadlines were 264368391500 and
294369636190 ns, a 30.001244690-second advance including scheduling overhead.
A later left click delivered the button event, ended the existing consumer loop
and returned to the shell, releasing its sessions normally.

## Contract review and limits

Source review checked zero (nonblocking core path and hook), all negative
(infinite), finite deadlines, checked clock arithmetic, long waits re-arming
at 30 seconds without resetting the final deadline, readiness before expiry,
optional/unacquired pointer omission, and wait errors selecting the upstream
fallback. Finite/zero timeout return timing and forced error/refusal cases
were not separately exercised by this infinite-wait consumer; no synthetic
faults or guest calls were injected.

The core exception requires both the Pyxis driver and disabled threads;
other SDL drivers retain upstream gating. Threads or asynchronous producers
will need a real wakeup sender and review of the one-pump readiness cache.
SDL's existing joystick enumeration polling interval remains when initialized.
Ordinary full default image builds passed with the existing compiler; submitted
exact-head CI is reported on the parent PR. No compiler rebuild is needed.
