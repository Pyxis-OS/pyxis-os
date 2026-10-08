# Audio task 2: sessions, mixing and IRQ refill

Owner assignment **2026-10-08**, after [#553](https://git.internal/PyxisOS/pyxis-os/pulls/553)
merged. Task branch **`audio/sessions-mixer`** starts from fresh main
**`780f5d22dce25aaa79fe9a0d842db6cd9e9883f3`**. This assignment combines the
[periodic refill and session/mixing steps](../../../wip/hda-playback.md#proposed-task-sequence-and-review-gates)
of the accepted milestone. The private engine is already merged; this task adds
its public per-space grant, at most eight process-owned sessions, copied queues,
kernel mixing and owned interrupt delivery. SDL2, Quake and native AMD binding
remain separately assigned work.

## Accepted task-specific policies

Accepted by the owner through the orchestrator on **2026-10-08**, before
implementation. Hidden-space playback also carries forward the proposal policy.

1. Continue playback in hidden spaces independently of keyboard/pointer focus.
   Provision a named audio grant alongside each space's display/keyboard grants;
   shell delegation is independent of stdin/input focus. Initial controls remain
   PCM/session-only, without volume or pause UI.
2. Start on the first queued frames without a 20 ms priming threshold. Stop
   hardware after queued and mixed PCM is consumed, then park without idle IRQs.
   Preserve acquired sessions for subsequent writes and count producer starvation
   once per empty episode. The public interface still makes no audible-drain promise;
   internal stopping must account for the codec/backend tail and be qualified.
3. Fail closed until reboot when refill progress is ambiguous or unsafe, or a
   FIFO/descriptor fault occurs: stop the engine, report WAIT_ERROR and
   CALL_UNAVAILABLE, and retain DMA. Ordinary producer starvation supplies zeros
   and is not terminal.

The already accepted ACQUIRE/WRITE/STATUS/RELEASE, atomic copied writes of at
most 4096 bytes, maximum-write writable waits and admission errors remain the
[session contract](../../../wip/hda-playback.md#accepted-session-call-contract).
No new call semantics, resampler or consumer backend is proposed here.

## No-playback baseline

Captured before task 2 implementation from clean main **`780f5d2`**, with the
merged task 1 engine prepared and parked. Ordinary `make -j16 image` passed in
the existing LLVM 23.1.3/49e2c1a builder, rebuilding the pinned sources. The
streaming-era userspace pin is **`a5a48b4b1e1cbff8131915244dbfb6018729789f`**;
older task 1 bundles were not substituted. [Kernel](baseline-kernel.txt),
[SDK](baseline-sdk.txt), [userspace](baseline-userspace.txt),
[ports](baseline-ports.txt) and [configuration](baseline-kernel.config) record
provenance. Existing vendor build warnings are not a kernel warning-free claim.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `561e0df274bd422a66b2264cbe353d719d3cad78b08c240a0563c15137d02a64` |
| ISO | `9fe9346e1490e33447628bcb3c5d3540616bf01864ac5c2e5d9f2beff86f0cb8` |
| Initrd | `acc72d85e0bdebe6e7336065f4eaf64ec233908c836163c07408c9a1337c25fb` |

Stock QEMU 10.2.2, Q35, `-cpu max`, four cores/one thread each, 8 GiB,
**nested KVM**, fresh OVMF variables and default VGA 1280×800. Static Caelum
TTY/cursor selected; three user spaces and network ready. Modern VirtIO SCSI
read-only CD-ROM, NIC and RNG; `intel-hda` plus `hda-output` with a 48 kHz S16
stereo WAV backend. No remote client during the idle cost window. The command
shape follows the [investigation](../audio-investigation/README.md#guest-configuration-and-commands),
with matching task 2 ELF/ISO, local filenames and a host accounting pidfile.
The manually launched baseline command, from the baseline worktree:

```sh
cp /usr/share/OVMF/OVMF_VARS.fd build/audio-task2-baseline-vars.fd
qemu-system-x86_64 -name audio-task2-baseline -machine q35 -accel kvm -cpu max -smp 4 -m 8G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=build/audio-task2-baseline-vars.fd \
  -display none -serial file:build/audio-task2-baseline-serial.txt -monitor stdio \
  -pidfile build/audio-task2-baseline.pid -gdb tcp:127.0.0.1:1243 \
  -drive if=none,id=audio_cd,format=raw,media=cdrom,readonly=on,file=build/pyxis.iso \
  -device virtio-scsi-pci,id=audio_scsi,disable-legacy=on \
  -device scsi-cd,bus=audio_scsi.0,drive=audio_cd,bootindex=1 \
  -object rng-random,id=audio_rng,filename=/dev/urandom \
  -device virtio-rng-pci,rng=audio_rng,disable-legacy=on \
  -device virtio-net-pci,netdev=audio_net,disable-legacy=on \
  -netdev user,id=audio_net,hostfwd=tcp:127.0.0.1:2351-10.0.2.15:2323 \
  -audiodev wav,id=audio,path=build/audio-task2-baseline.wav,out.frequency=48000,out.channels=2,out.format=s16 \
  -device intel-hda,id=hda -device hda-output,bus=hda.0,audiodev=audio
```

The [serial record](baseline-serial.txt) has the established single
`audio: ready ...; output idle` summary. [GDB](baseline-presenter-gdb.txt)
confirms 22 commands/responses, discovered codec `1af40012`, pin 3 → DAC 2,
command/stream DMA off, PCI command `0x402` (BME clear, INTx disabled), retained
20 KiB DMA backing and a live untimed parked worker. The WAV contains zero
frames. No native hardware was accessed.

### Presenter elapsed observations

Eight individually entered hardware-breakpoint/`finish` pairs around
`space_present`, matching ELF, `set may-call-functions off`, HPET counter
`0xfffffe80402020f0`, 10 ns period. GDB confirmed Caelum and 1280×800. This follows
the [existing timing method](../../screenshot-qualification.md#matched-presenter-cost)
and adds no guest instrumentation, inferior calls or guest state edits.

HPET ticks: **121997, 124578, 129359, 88096, 96148, 74535, 115815, 116776**.
Median **1.162955 ms**, range **0.745350–1.293590 ms**. These are debugger-qualified
elapsed observations including preemption/device waits and nested-host variation,
not isolated CPU time or native performance.

### Host cost with idle guest

Separate debugger-free window: two manual process/thread `/proc` stat snapshots,
[start](baseline-cpu-start.json) and [end](baseline-cpu-end.json), with monotonic
timestamps and `CLK_TCK=100`. Monitor `info cpus` mapped CPU0/1/2/3 to host
threads **462225/462226/462227/462229**. Elapsed **157.492567112 s**; QEMU process
CPU **30.63 s**, or **19.4485% of one host CPU**.

| Guest vCPU thread | Host CPU seconds | Percent of one host CPU |
| --- | ---: | ---: |
| CPU0 / BSP | 14.42 | 9.1560% |
| CPU1 | 5.07 | 3.2192% |
| CPU2 | 6.71 | 4.2605% |
| CPU3 | 4.40 | 2.7938% |

This measures nested KVM execution/exits and QEMU overhead, not guest-idle
percentages. Thread reads are sequential and jiffy-rounded. No owned build,
second VM, debugger or remote client ran during this window. A single window
is not a stable regression threshold. Repeat matched no-playback observations
with the same scene/configuration and report actual elapsed windows; measure
active playback and eight-session cost separately. Check ABI/bundle inputs
before reuse when this task introduces public headers; do not bypass bundle
validation to force a kernel-only comparison.

## Source assessment and remaining work

QEMU's controller offers single-message MSI; the existing kernel helper covers
MSI-X, so a checked MSI helper and one owned interrupt vector are needed. The
ISR must capture bounded status and wake the worker; it must not mix, allocate
or change stream/codec ownership. QEMU may coalesce descriptor completions,
so IRQ count alone does not establish consumed periods. Its codec also buffers
PCM after DMA; BCIS is not audible completion. Refill safety needs position,
timing and immutable BDL ownership, with explicit ambiguity detection and
measured scheduling limits. These are source observations, not dynamic-refill
qualification.

Session mutation/allocation stays on the BSP audio worker. Readiness uses
published immutable snapshots because other workers can inspect mixed waits.
Exit cleanup cannot sleep or retain a destroyed process pointer: use an
allocation-free notice and generation-safe ownership invalidation. Closing a
handle does not release the acquired session. The worker must continue serving
cleanup/status after terminal hardware failure.

Next: finish the worker/refill integration,
then qualify the public interface, concurrent signals, ordinary producer pauses,
exit/release, admission and sustained refill with manual QEMU/debugger checks.
Publish any concrete libpyxis/grant-forwarding/PCM-producer dependency before
updating the parent pin. No tests, self-tests, fault injection, boot/output
automation or new CI is introduced. Native binding remains later work; confirm
the owner's QEMU-closure/native-batch choice at milestone closure.

All task-owned baseline builds, guests and debuggers are stopped. Local baseline
SDK/userland/ports bundles are packaged for verified reuse. This report records
a baseline and accepted design round, not completed implementation or task 2 CI.

## Accepted DMA tuning and implementation checkpoint

The owner accepted the DMA tuning change on **2026-10-08**, after source review,
in addition to the three accepted policies above. Stock QEMU 10.2.2's codec output timer can request
**8192 bytes**, exceeding the original **7680-byte / 40 ms** ring. Controller
transfer walks at most the configured descriptor count; a whole lap can leave
LPIB unchanged with only coalesced BCIS. The codec adjusts or resets its timer
origin, so WALCLK since RUN is not an absolute byte counter. Small new observation
gaps alone cannot exclude catch-up from an earlier lag. Counting IRQs or guessing
missed laps from nominal rate is insufficient.

Primary source: [QEMU codec output timer and adaptive clock](https://gitlab.com/qemu-project/qemu/-/blob/v10.2.2/hw/audio/hda-codec.c),
[controller DMA transfer](https://gitlab.com/qemu-project/qemu/-/blob/v10.2.2/hw/audio/intel-hda.c).
This is source evidence, not an injected stall or measured production refill.

The accepted starting tuning is **eight 10 ms DMA periods (80 ms)**,
keeping 80 ms session queues, the fixed format and 4096-byte atomic writes.
That adds up to 40 ms of hardware buffering and needs position/time guards and
qualification; it is not a hard real-time or audible-drain guarantee. Revisit DMA depth and latency during native qualification. The 8192-byte burst
and initial 7680-byte ring are established from QEMU source, not an injected
stall or a measured native result.

Local implementation on `audio/sessions-mixer` includes the object/session,
space/grant, wait/readiness and typed BSP request changes, saturating mixer,
checked single-message MSI, interrupt vector and stream observation/prepare/RUN
helpers. Component commits are retained on `audio/task2-objects` (`e40f9c6`)
and `audio/task2-irq` (`168e337`); integration review additionally corrected
starvation to count empty episodes and avoid notifications from unchanged empty
queues. Kernel object compilation passed for all changed C sources, with no
warnings. The final owning worker/FIFO, refill and drain integration remains
unfinished; no complete kernel/image or runtime playback pass is claimed.
Those code commits remain local while this checkpoint is published.

The separately published userspace dependency is draft
[#168](https://git.internal/PyxisOS/pyxis-userland/pulls/168), branch
`audio/session-producer`, head **`97770cd`**. It adds libpyxis wrappers, optional
audio forwarding independent of input focus and the native PCM producer. An
ordinary new source-built SDK plus ports and all userspace applications passed
at that head; focused PCM/shell/session/mux/remote-terminal builds passed too.
WRITE returns an eight-byte accepted-count payload; CALL_WOULD_BLOCK is distinct
from CALL_QUEUE_FULL and the producer uses the accepted former status. The new
SDK must be used for these headers/libraries; no baseline bundle identity is
substituted. Parent gitlink publication follows dependency publication. Merge
order is userland before the parent implementation, once runtime qualification
is complete; neither draft is ready for task completion.

All owned build/debugger/guest processes are stopped at this checkpoint. The
remaining work is worker/refill/drain integration with the accepted tuning,
manual QEMU interface and PCM/mixing qualification, matched after measurements,
final review and exact submitted-head CI. Native and consumer tasks do not start.
