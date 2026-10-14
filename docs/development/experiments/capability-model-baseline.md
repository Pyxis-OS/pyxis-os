# Capability model: native baseline run sheet

Prepared 2026-10-14 against main `09a51b9a52d9fa1e3c9c0d7f7c33b75e9e008fa4`,
userland `077116c3`, ports `2296a06c`, fs `414be0ac`, lwIP `a1aadb91`.
**Owner accepted the existing-tool method and hands-free gate, 2026-10-14.**
The gate includes main's packaged Multiplexer tab; the pilot predates it.
The complete gate is **not run**; task 1 must not merge until its results are
recorded. A partial pilot below does not close that gate.

## Stage and conditions

Use the ThinkPad's **Remote space via `pyxis-remote --listen t14`**, plain machine
mode at 80 × 24, instead of the local Development session. Guest monotonic
intervals exclude host command-send round trips. Remote is a repeatable native
profile when used throughout, rather than an assertion of local-session
equivalence: grants, environment, launch grouping and background TCP/TTY work
can differ. Use AC, idle input and no competing workloads. Record the conditions
and actual CPU count (the inventory has 12); missing authority/tools fails setup.

The host files are in `/shared/present/capability-baseline/` (horse:
`$HOME/shared/present/capability-baseline/`): `build.sh`, `run.py`,
`startup-inspect.py` and the frozen `commands.txt`. No guest benchmark executable,
new grant or call-site authority wrapper is added. Raw JSONL, decoded output,
configuration, hashes, failures and summaries stay there, outside the repository.

### Luna builds: ordinary image and debug inspection image

Use a fresh checkout at the exact baseline revision with its exact submodule
pins. The existing compiler container is
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-e0801c2`; no rebuild is needed.
Run the helper in that builder environment, with the checkout and shared output
mounted at their same absolute paths. It refuses dirty/mismatched source or
existing output; it uses the ordinary `make image` path, without stale bundles.
The container installs the same host configuration dependency as CI, then runs
the builds as Luna's UID/GID; the compiler itself is unchanged.

```sh
BASELINE_REV=09a51b9a52d9fa1e3c9c0d7f7c33b75e9e008fa4
BASELINE_SOURCE="$HOME/src/pyxis-capability-baseline-A"
BASELINE_SHARED=/shared/present/capability-baseline
BASELINE_BUILDER=git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-e0801c2
git clone --no-checkout ssh://git@git.internal:2222/PyxisOS/pyxis-os.git "$BASELINE_SOURCE"
git -C "$BASELINE_SOURCE" checkout --detach "$BASELINE_REV"
git -C "$BASELINE_SOURCE" submodule update --init --recursive
podman run --rm --userns=keep-id --user=0 --security-opt label=disable \
  --env HOME -v "$BASELINE_SOURCE:$BASELINE_SOURCE" -v "$BASELINE_SHARED:$BASELINE_SHARED" \
  -w "$BASELINE_SOURCE" "$BASELINE_BUILDER" \
  sh -s -- "$(id -u)" "$(id -g)" "$BASELINE_SHARED" "$BASELINE_SOURCE" "$BASELINE_REV" <<'BUILD'
set -eu
apt-get update
apt-get install -y --no-install-recommends python3-pip
python3 -m pip install --break-system-packages -r requirements.txt
exec setpriv --reuid="$1" --regid="$2" --clear-groups --no-new-privs \
  "$3/build.sh" "$4" "$5" "$3/images-A"
BUILD
```

The helper executes both recipes sequentially; the debug flags are the only
build-option difference. Optional personal game data is omitted from both.

```sh
make -j8 image REMOTE_BEACON=t14 LOG_LEVEL=info LOG_UDP=1 \
  DEBUG_NET= DEBUG_WAIT=0 \
  DISPLAY_FLIP=0 DISPLAY_FLIP_METRICS=0 DISPLAY_TIMING=off DISPLAY_TIMING_METRICS=0 \
  INIT= NETWORK_CONFIG= MOUNT_DISK= PREBUILT= \
  DOOM_WAD= DOOM_DEMOS= QUAKE_DATA= DIABLO_DATA= DUKE3D_DATA=
# Save ordinary payload before regenerating the debug entry.
make -j8 image REMOTE_BEACON=t14 LOG_LEVEL=info LOG_UDP=1 \
  DEBUG_NET=t14 DEBUG_WAIT=1 \
  DISPLAY_FLIP=0 DISPLAY_FLIP_METRICS=0 DISPLAY_TIMING=off DISPLAY_TIMING_METRICS=0 \
  INIT= NETWORK_CONFIG= MOUNT_DISK= PREBUILT= \
  DOOM_WAD= DOOM_DEMOS= QUAKE_DATA= DIABLO_DATA= DUKE3D_DATA=
make -C tools remote gdb-bridge
```

Each output profile contains `caelum.elf`, `initrd.cpio`, generated `limine.conf`,
`kernel.config`, bundle provenance, full revision, pins, compiler image and SHA-256
manifest. Existing Linux host remote/bridge tools are copied to `images-A/tools/`. In particular, do not reuse an older menu's `debug.image` token.

### Horse runner

Start with the ThinkPad already running Pyxis and reachable through its stock
Remote beacon, with reboot authority. Luna stages the helper's image directories;
the runner stages checked payloads under a private PXE subdirectory, atomically
selects their generated menu, and remotely reboots. It preserves the original
menu byte for byte and leaves existing root payloads and `BOOTX64.EFI` untouched.
No other batch may stage or reboot the target concurrently.

```sh
BASELINE_SHARED="$HOME/shared/present/capability-baseline"
python3 "$BASELINE_SHARED/run.py" \
  --images "$BASELINE_SHARED/images-A" --label A \
  --remote "$BASELINE_SHARED/images-A/tools/pyxis-remote" \
  --bridge "$BASELINE_SHARED/images-A/tools/pyxis-gdb-bridge" \
  --pxe "$HOME/shared/pxe/boot" --cpus 12 \
  --conditions 'ThinkPad T14 Gen 1 AMD; AC; idle input; no competing workloads'
```

`--beacon-address IPv4` is optional for the existing remote/debugger LAN recipes.
The runner verifies the ordinary kernel revision with `fastfetch`, checks
`text://welcome`, performs three invocations of every timed command and three
pipe handoffs, then three debug inspection boots. Every debug boot is driven
from horse: start the bridge and GDB, select the debug image, send `reboot`, inspect,
confirm the bridge's release notice, then boot ordinary again. Host timeouts are
bounded; work is not automatically retried. Menu restoration happens on success
or failure. A disconnect alone never proves a reboot or a successful sample.

Keep commands, order, warmups, sample counts, geometry, options, power/load and
process selector identical after A and C. Build their exact revisions with the
same helper into `images-after-A`/`images-after-C`, and change the matching image/tool paths and
`--label`; record the tools' hashes too. Preserve script hashes with each result. Kernel-layout adjustments to
the read-only extractor must be recorded explicitly, without changing its
measurement boundary or process role. Timed boots have no debugger or profiler.

## Timed batch

Run each OPEN or launch command three times. Each Lua invocation performs one
untimed warmup and ten samples, printing nanoseconds. Close, verification and
printing are outside its OPEN interval. The runner sends these commands to the
existing interpreter; they are not an installed measurement framework.

Native descriptor OPEN, using the existing verified 1 MiB fixture:

```text
bin://iobench.pxe read boot://share/iobench.bin --bytes 1048576 --buffer 4088 --rounds 10
```

Keep its OPEN samples and clock-loop overhead separately from payload and
complete times. OPEN includes path lookup and descriptor setup; payload/EOF/close
are later. Its warmup verifies the fixture. This is warm boot-archive FILE
latency, not cold-cache or npfs disk latency.

Matched Lua FILE creation on native and provider targets:

```text
lua -e 'local p="boot://share/iobench.bin"; local f,e=io.open(p,"r"); assert(f,e); assert(f:close()); local now=pyxis.monotonic_ns; for i=1,10 do local a=now(); local f,e=io.open(p,"r"); local b=now(); assert(f,e); assert(f:close()); print(i,b-a) end'
lua -e 'local p="text://welcome"; local f,e=io.open(p,"r"); assert(f,e); assert(f:close()); local now=pyxis.monotonic_ns; for i=1,10 do local a=now(); local f,e=io.open(p,"r"); local b=now(); assert(f,e); assert(f:close()); print(i,b-a) end'
```

These include Lua dispatch/userdata, libc stream creation and clock boundaries;
the provider case includes discovery, OPEN IPC and export installation. No
payload is read inside timing. Check `cat text://welcome` separately. Do not use
`iobench read text://welcome`: its deterministic-byte warmup rejects that text.

Small-program launch **through exit**, accepted method:

```text
lua -e 'local args={"bin://echo.pxe","-n"}; local run=pyxis.run; local now=pyxis.monotonic_ns; assert(run(args)==0); for i=1,10 do local a=now(); local s=run(args); local b=now(); assert(s==0); print(i,b-a) end'
lua -e 'local now=pyxis.monotonic_ns; local a=now(); for i=1,1000 do now() end; local b=now(); print("clock-wrapper-loop ns/call",(b-a)/1000)'
```

`echo -n` emits no payload. `pyxis.run` still includes argument allocation/copy,
image lookup, grant preparation, launch, child startup/execution, WAIT and cleanup.
This is not isolated launcher-return or child-entry latency. Record the clock
reference without subtracting it; Lua GC and scheduling remain included.

Pipe transfer, three fresh Remote connections on the same ordinary boot:

```text
session bin://iobench.pxe pipe --buffer 4096 --rounds 10
```

**Direct `bin://iobench.pxe pipe` is not equivalent.** An ordinary launch lacks
pipe-create and service grants. `session` retains the stock declared authority
and replaces the root shell. The remote root's waiting handoff propagates the
successor's exit and drains output before FINAL
([remote lifecycle](../../userland/remote-terminal.md#framing-and-lifecycle)).
Each handoff ends that connection, not the remote daemon; the runner reconnects
for the next pass. It requires successor exit 0 and complete FINAL drain rather
than accepting a builtin acknowledgement alone.

This uses a 64 KiB byte pipe and a verified 1 MiB transfer, with fresh workers per
pass and one warmup. Launch, allocation, preparation, readiness, verification and
reporting are outside timing. Keep acceptance and completion intervals; headline
completion throughput includes the consumer acknowledgment. No failed pass has
an accepted throughput result.

## Startup allocation: separate inspection boot

Use the existing [native checkpoint debugger](../../kernel/network-debugger.md#use)
with Luna's exact ELF and `DEBUG_NET=t14 DEBUG_WAIT=1`. This boot supplies no timing
samples. Running breakpoints, Ctrl+C and injected functions are unavailable.

The current fixed header is 216 bytes by source inspection. The full startup
allocation includes separately page-rounded metadata and argv and depends on the
workload's grants, roots, cwd, environment and arguments. Do not report 216 as
the measured allocation or claim that a boot process is the short-lived `echo`.

The host GDB extractor identifies the unique live process in the fixed
`development` space whose observed startup `argv[0]` is `boot://session.pxe`.
This is the network-configuring session at the first checkpoint, not a claim
that a Remote shell or short-lived `echo` remains alive. It discovers tasks from
bounded scheduler/service/readiness lists and control waiters, validates the
complete nonterminal checkpoint and CPU count, and refuses locked, retired,
ambiguous or unavailable metadata. Only the selected scalar metadata and bounded
`argv[0]` are read; no environment, capability contents or other arguments are
dumped. Startup spelling identifies the observed role, not authenticated image
bytes.

The measured value is the matching startup VM range's `pages * 4096`, only when
its base equals `startup_address`, state is backed, and `backed_pages == pages`.
The selector and allocation must agree across three inspection boots. If the
role cannot be found or any guarded read fails, the result is **unavailable** and
the gate remains open. Never choose an arbitrary process or substitute the
216-byte header. The extractor uses read-only memory/physical monitor reads,
never injected calls or writes; ordinary `continue` is confirmed by the bridge
before the runner terminates its own host GDB/bridge jobs. A terminal stop cannot
be resumed and needs owner recovery.

## Partial native pilot, not the gate

Delta's 2026-10-14 hands-free Remote pilot used main `3b714754` with userland `ec234db4` (the other pins
unchanged), AC and idle input, but enabled flip metrics and included game data. Pipe and debugger
inspection were not run. Medians below are recomputed from its raw `pilot.log`,
three invocations × ten samples; retain that distinction from the controlled
complete gate. Raw files: `/shared/present/capability-baseline-pilot/`.

| Workload | Invocation medians (µs) | Range, all 30 samples (µs) |
| --- | --- | --- |
| Native descriptor OPEN | 3.932 / 4.038 / 4.0625 | 3.877–4.759 |
| Lua boot FILE creation | 4.063 / 8.5505 / 8.6065 | 3.527–9.277 |
| Lua text FILE creation | 17.223 / 23.5745 / 22.4575 | 12.333–29.826 |
| Lua echo through exit | 891.512 / 862.8685 / 866.450 | 834.946–1199.980 |

These intervals do not isolate a BSP reaper-queue delay; they cannot be compared
directly with the loaded audio/game teardown workload. They establish a useful
pilot, not all required baseline evidence.

## Complete gate result

**Pending Luna's staged hands-free run.** After it, record configuration, exact
revisions, sample counts, medians/ranges, startup allocation and limits here.
Review every failure and retain slow successful samples. No complete native gate
result, host/nested-QEMU measurement or physical-device read-back is claimed.
