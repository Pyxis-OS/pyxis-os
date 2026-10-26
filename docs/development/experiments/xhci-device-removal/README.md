# xHCI device/subtree disappearance

2026-10-14. **Native dock check passed** on the ThinkPad at `11b335dd`;
see [native result](#native-result). The first submitted boot exposed a latent
main notification bug, fixed separately in #856. Native motivation: the owner's
`/shared/present/dock-ethernet-log3.txt` reports controller 7:0.3 stopped by
`device disappeared during interrupt transfer`.

## Scope and build

Interrupt consumers are boot HID, USB 2 hub status and root-connected AX200 HCI.
HCI also owns asynchronous bulk IN/OUT; storage owns synchronous bulk IN/OUT.
Disappearance closes the affected branch's admission before a yielding command
wait, fences all configured endpoints and disables slots child-first. Old DMA
and TD identities remain retained. Owned transaction-error/babble and EP0/bulk
request expiry also fail locally when those fences succeed: they can arrive
before a hub's disconnect notification. Caller wait expiry is still distinct
from request expiry and does not cancel a ticket.

Unowned/malformed events, controller/command failure and an unproved endpoint or
slot fence still quarantine the controller. No runtime allocation, DMA reuse,
new class admission, public ABI, normal klog line or compiler change.

Before-code baseline: `0f1aa2f89288570d68b152fc1d8225ee50538035`.
Root-removal check: `2894a7868559f767a6cc5403f0e7d365a4bed428`.
Nested-removal/HID rebind: `49f6ab30bd4c7f22bbc2492b6e386ca1f0cea751`.
Owned-request timeout correction: `e18257186c6f20202379f12081322d6ea91424d1`.
Final code check: `d70c4edb37b34ffca23f217bcf45e005b81395d2` (makes the existing
Disable Slot SUCCESS requirement explicit).

Builder: `git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-df87791`.
Ordinary kernel/image build: `make -j16 image PREBUILT="sdk ports userspace"
LOG_LEVEL=trace`. Unchanged SDK/ports/userland bundles were verified with
`scripts/bundle.sh verify`, following [build bundles](../../build-bundles.md).
Their source build is the task-2 image at `98ee6339`, with identical ABI and
dependency inputs: userland `f26690d5`, ports `2c49e86c`, fs `d84f8084`, lwIP
`a1aadb91`. This is kernel-only bundle reuse, not a fresh rebuild of those
components. No probe patch or injected debugger calls.

## QEMU and debugger results

QEMU 10.2.2, Q35, nested KVM, `-cpu max`, 2 GiB, UTC RTC, fresh OVMF variables,
ISO boot, VirtIO RNG/network. Remote forward: TCP 24085 to guest 2323; GDB 1248.
Read-only 32 MiB child and 48 MiB sibling raw disks. Root topology:

```sh
-device qemu-xhci,id=usb,p2=3,p3=3 \
-device usb-hub,id=hub,bus=usb.0,port=1 \
-device usb-kbd,id=hubkbd,bus=usb.0,port=1.3 \
-device usb-storage,id=hubdisk,bus=usb.0,port=1.2,drive=child \
-device usb-storage,id=siblingdisk,bus=usb.0,port=2,drive=sibling
```

Four-CPU baseline and fixed runs execute through `pyxis-remote --machine
--no-shell-echo 127.0.0.1 24085`:

```text
session bin://ipcbench.pxe call --size 4096 --messages 256 --rounds 100
```

HMP `device_del hub` reproduces the native failure on the baseline:
`running=false`, `failed=true`, interrupts disabled, unrelated root disk also
unusable through that controller. Fixed: slots 4/3/2 fence child-first,
`running=true`, `failed=false`, interrupts ready, no pending command. Host and
core hub/descendant records retire; the sibling disk and its block pool remain
healthy, the child disk fails locally. Both IPC runs complete 100 verified passes,
exit 0 and complete remote drain.

| Nested completion time, ms | Median | Range |
| --- | ---: | --- |
| Baseline | 527.772 | 74.277–1985.320 |
| Fixed root-removal check | 502.732 | 145.166–1628.596 |

These are workload-under-removal checks, including debugger interruption, with
different clock-read calibration and wide nested-host variation. They establish
completion, not an attributable performance change. Final code repeats root
removal with ten verified IPC passes and the same healthy controller/sibling
and retired branch state.

One-CPU nested topology adds `innerhub` at 1.4, moves the lost keyboard/storage
to 1.4.3/1.4.2, and adds a surviving keyboard at 1.1. HMP `device_del innerhub`
retires its children and hub, leaving the root hub's two interrupt receives
POSTED, sibling keyboard active and sibling disk healthy. An existing 100-pass
`iobench read boot://share/iobench.bin --buffer 64 --rounds 100` completes; this
is boot-RAM file activity, not USB bulk load. Removing/readding the sibling
keyboard at 1.1 creates an active fresh runtime HID record on the surviving hub.

A separate one-CPU boot check stops with a read-only GDB hardware breakpoint
just after the ordinary child BOT CBW submission, then removes `innerhub`.
The CBW success event is already queued: this checks software request ownership,
not unfinished hardware DMA. The following Data-IN request times out. Before
the timeout correction this still stopped the controller; at `e1825718` the
timeout retires locally. GDB confirms child slot 4 then hub slot 3 fenced, core
records retired and pending hub bitmaps cleared. Root hub and sibling storage
remain live; the sibling's ordinary GPT read completes `BLOCK_OK`, 512 bytes,
`submitted=true`. Old EP0/bulk ring addresses and physical backing remain.

Matching snapshots, SHA256:

| Code | ELF | ISO |
| --- | --- | --- |
| `e1825718` | `79843b514a9d218c7f17004d1fc23e8c4b33a80a8204efa5e8ffa2c5fc462c9e` | `353a29ad1c0ccd6ecf6b9e21e6d456cd642c2031e015295f2f56d1f44da40653` |
| `d70c4edb` | `58e10e07942a0e4a6fe194aaa7abcce9da821bd76f57c562ac781cae0672b1e7` | `609d660b8275bd5d15cc79b6bf329b9d7b7db68161a1c62978516fd6ff79d51b` |

Raw serial/GDB/client logs stay in the task worktree's `build/removal-*` files,
including the preserved failed active-BOT check and its corrected repeat.
All owned QEMU/debugger/client jobs are stopped. These QEMU checks are separate
from the owner-run native result below.

## Native boot blocker

Owner/luna, 2026-10-14 about 20:50: ThinkPad, dock attached, trace build of
`63fffdb4`, default PXE entry plus `usb.xhci.diagnose=1`. Verified native ELF
SHA256 `729d8280a9a803c0ff57fddbeef1b952738482fb2912dbf5cb1b74d44091a77b`,
archive `fa9887c77f9c8dddf9c2574007af561482179476676408425044aeb737aa1765`.
Before any unplug, the screen prints only
`assertion: arch_cpu_index() == 0 (kernel/usb/xhci.c:1179)`.
PXE was restored to main after that failed boot. The successful retest below
uses the separate main fix.

Source inspection identifies the only listed notify caller without an earlier
BSP assertion: `bluetooth_hci_revocation_notify()` loads the host atomically but
calls BSP-only `usb_host_notify()`. `cut_forwarder()` calls it unconditionally;
CUT and automatic cut on final controller-grant release can run on an AP.
The other HCI/block callers assert BSP context before reaching this call.
No task-placement or forwarder/HCI call-site change is present in this PR.
The exact initiating cut/release and why this boot exposed it are unmeasured.
QEMU has no AX200 host, so this notification path was not covered there.

This is a latent main bug, not a placement change in #852. Separate prerequisite
[#856](https://git.internal/PyxisOS/pyxis-os/pulls/856) guards the notification/wait
handoff across CPUs while all controller work stays BSP-owned. Its
[qualification record](../usb-controller-notify/README.md) reproduces explicit AP
CUT with the owner's authorized local HCI-host stand-in on current main, then
checks AP CUT/final-controller release and queued cancellation with the fix.
#852 is rebased on #856; no stand-in patch is present in either submitted tree.
The native retest below passed. No native debugger attribution is needed for
this established source/runtime mismatch.

The absent backtrace is source-explained: `KASSERT` calls ordinary `panic()`,
which prints the message and halts without a stack walk, on BSP or AP. It is
not evidence of a presenter/AP-specific stack-printing failure. See
[panic diagnostic debt](https://git.internal/PyxisOS/pyxis-os/issues/1294).

## Recheck after notification prerequisite

Rebased code `aa46af8774aadc6e887985968abeb6d34ca8869e` includes #856
`372e0135`; range-diff preserves all eight removal/record commits unchanged.
Build uses verified current-main bundles from the #856 baseline: userland
`220b8076`, ports `efe8d877`, fs/lwIP unchanged. No stand-in patch.
Ordinary trace image build passes, followed by the same four-CPU root hub
unplug during ten IPC passes. All passes verify, exit 0 and fully drain.
GDB finds controller running/not failed, notification guard unlocked, no pending
command, retired hub/children and a healthy sibling disk/pool. Slots 4/3/2 fence
child-first. ELF SHA256
`3ad7967d45ce1e012b046043315791da409ae31a363f03fedff83f0806669b31`,
ISO `322ed7c3ea4227885da0f338c40775dc406c9894a61cb42b5d52160be66ad10e`.
Only this record was dirty when built; kernel code matches that rebased head.
All owned jobs stopped; native boot/unplug retest follows below.

## Native result

Owner/Claude/luna, ThinkPad, 2026-10-14 about 21:55: `11b335dd`, trace build
`LOG_LEVEL=trace REMOTE_BEACON=t14 LOG_UDP=1`, default PXE entry plus
`usb.xhci.diagnose=1`. Dock attached on USB-C with Ethernet. Verified ELF SHA256
`8099511ec4be884e94af3278039247663c50862c96f7750b958d8065d8bda562`,
archive `e3122e9c2dc2e89db1cedf39371f1842b8c2308fab8de0ca7da26ff596f02aab`.
Captures: `/shared/present/xhci-852-native/{before,unplugged,replugged}.txt`.
PXE was restored afterward.

At boot, the dock blip retires locally: slots 4/2/3/1 fenced, root port 1
retired, old backing retained, no disappearance halt. These slots cover the
dock hubs, Billboard device and AX88179A. The owner unplugs, then replugs after
about ten seconds: system and Remote continue; IPC CALL passes succeed before,
between and after; all three controllers report `running=1 failed=0` with
interrupts advancing. No assertion or panic. Other-controller BOT stick, AX200
and camera remain in the immutable inventory; that listing alone does not
prove new device I/O. Expected runtime attachment refusal is observed, since
readmission of hubs/non-HID devices is outside this change.

The trace/diagnostic volume wraps the log ring, so per-event lines around the
manual unplug are not retained. The boot fences, controller samples and
continued workloads meet this packet's success criteria. The accepted
[log-level direction](https://git.internal/PyxisOS/pyxis-os/pulls/857) addresses capture noise;
no logging redesign belongs to this fix. Native AX200 removal and proof of
unfinished bulk DMA remain unqualified.

## Native ThinkPad packet

Luna builds the submitted head with the existing builder, `make -j16 image
LOG_LEVEL=trace REMOTE_BEACON=t14 LOG_UDP=1`; no compiler publication or userland
dependency PR. Stage the ordinary PXE kernel/archive, label the exact revision
and hashes, and append `usb.xhci.diagnose=1` for the existing controller/input
diagnostics. Boot the live profile without mounting the installed USB volume for
mutation during the unplug check.

On horse, start `build/tools/pyxis-log > xhci-dock-removal.log` and
`build/tools/pyxis-remote --listen t14 0.0.0.0 2323`. Keep the remote link on the
already working LAN NIC. Capture `lsusb -n` once to identify boot topology; it is
an immutable observation, not proof of current runtime health.

Run the same IPC command before and after unplug; keep normal input/remote log
following active while the owner unplugs/replugs the dock. Check
that the kernel and remote workload continue, unrelated USB devices still work,
and controller 7:0.3 has child-first `device slot ... fenced` traces rather than
the disappearance halt. Affected storage requests must fail honestly, preserving
any submitted/uncertain-effect result. Use the independent keyboard/Bluetooth or
an uncached read on a surviving device as a control, if present on that controller.
If it does halt, retain the exact fence/command/controller error; an unproved
fence remains fail-closed.

Replug does not yet admit replacement hubs, storage, HCI or dock Ethernet.
Only existing low/full-speed HID admission on surviving supported hubs is
expected. USB 3 periodic transfers, AX200 removal and native outstanding bulk
DMA are unqualified here; this packet supplies the dock/controller evidence.
