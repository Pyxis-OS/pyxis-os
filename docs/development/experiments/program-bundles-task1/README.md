# Unpacked development bundles: task 1

Recorded on 2026-10-09 for [program bundles](../../../wip/program-bundles.md).
These are nested-KVM observations. Temporary native fixtures, image variants,
JSONL, serial and debugger captures stay in ignored `build/bundles-task1-*`;
no benchmark, test or boot automation was added.

## Inputs and matched launch comparison

The baseline precedes implementation: main `bae571a27e64e27c9103eaa91c9148357cfe1545`,
using exact-head [CI #1543](https://git.internal/PyxisOS/pyxis-os/actions/runs/1543)
kernel/SDK/userland/ports artifacts. Pins are userland `2fc6badb`, ports `0f0aa443`,
fs `b427df29` and lwIP `a1aadb91`.

The measured after build contains stack implementation `c8f59c25`, userland
`0793743` and ports `248658f`, with the same fs/lwIP pins. It includes the native
capture comment correction and dependency integration subsequently committed as
`2e770a08`; those changes add no further executable behavior. Kernel
ELF SHA-256 is `073d714bf9d410272feffb92822013664c3625a4557f811992a709ba778959fd`.
SDK, all ports and userland were rebuilt with the existing LLVM 23.1.3 builder
at `49e2c1a`; there is no compiler-container rebuild or external-source change.
The different dependency merge commits at the start of the task have trees
identical to baseline pins. Effective kernel configuration values match.

Both timed archives stage `launch = true` in Remote and omit a bundle catalog.
The after archive contains no sample bundle or admission probe. QEMU 10.2.2 uses
Q35, nested KVM, `-cpu max`, 512 MiB RAM, UTC RTC, one socket, one or four cores,
and one thread per core. Standard VGA presents 1280x800; VirtIO SCSI CD, RNG
and modern VirtIO networking match. Host loopback TCP port 24567 forwards to
`10.0.2.15:2323`. Each boot has fresh raw OVMF variables. Logging is `info`,
memory profiling is off and GDB is detached while timing. The
[task 1 QEMU command](../threads-task1/README.md#inputs-and-configuration)
applies with `build/bundles-task1-baseline` / `build/bundles-task1-after` paths.

After a 16-child warmup, time three fresh remote sessions per CPU count:

```text
lua -e 'for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'
exit
```

The existing machine client consumes a FIFO opened read/write so input remains
open through command completion; immediate transport EOF currently ends the
session before this workload on unchanged main as well. For one repetition,
substitute the CPU/repetition/output paths as needed:

```sh
mkfifo build/bundles-task1-after/input-1
(printf '%s\n' \
  'lua -e '\''for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'\''' \
  exit >build/bundles-task1-after/input-1) &
/usr/bin/time -f '%e' -o build/bundles-task1-after/launch-1-1.seconds \
  build/tools/pyxis-remote --machine --no-shell-echo 127.0.0.1 24567 \
  3<>build/bundles-task1-after/input-1 <&3 \
  >build/bundles-task1-after/launch-1-1.jsonl
```

| 1,024-child whole session | Before samples (s) | After samples (s) | Before/after median (s) |
| --- | --- | --- | --- |
| 1 CPU | 3.15, 3.15, 3.13 | 3.06, 3.06, 3.07 | 3.15 / 3.06 |
| 4 CPUs | 3.30, 3.33, 3.85 | 3.15, 3.13, 3.15 | 3.33 / 3.15 |

Every sample completed 1,024 children, exited with zero and reported final
`drain=complete`. There is no observed plain-launch regression. Whole-session
cost includes root/Lua startup, interpretation, transport, exits and final drain;
these short samples do not isolate loader latency or establish a speedup,
statistical confidence, owner-host or physical-hardware performance.

## Manual bundle and admission observations

A separate four-CPU image adds an explicit development catalog and temporary
native sample. Its `.pxb` contains `manifest.json`, `app/bin/probe.pxe` and
`app/data/message.txt`. Two command names select the same native entry; its
manifest requests a 4 MiB stack, an `assets` root for `data`, required memory
and clock grants, and optional TCP listen authority. Remote has only connect
authority, so the sample enumerates memory/clock and reports the optional
listener absent. Its unrequested launcher is also absent; explicit streams remain.

Bare `bundle-first`, logical `bin://bundle-second`, explicit `.pxb/`, Lua
`pyxis.run`, and `bundle-first | cat` all exited with zero. The native entry
touched every page of a volatile 2 MiB local frame, read the same text through
`app://data/message.txt` and `assets://message.txt`, queried both roots' exact
LOOKUP|ENUMERATE|READ_FILES rights, and observed mutation denied. Copying the
bundle with native `cp -r` to `tmp://sample.pxb` and launching it passed as well.
An 8 MiB manifest request also loaded and exited normally.

Malformed/duplicate-key JSON, invalid UTF-8, unknown fields, zero/unaligned/
over-cap stacks, parent-traversal paths, missing entries and system-only grant
classes rejected before publication. Missing required TCP listen authority
rejected; the optional counterpart was absent. Lua also reported an invalid
manifest as failure. A failed manifest with stdout redirected left a prior
`preserve` file unchanged.

A temporary native SDK client directly inspected catalog results. The valid
view selected the requested command; only an absent command returned NOT_FOUND.
Duplicate IDs, duplicate commands across distinct IDs, collision with ordinary
`bin://echo.pxe`, broken registered sources and duplicate catalog keys returned
BAD_REQUEST with no selected program. A ninth registration and JSON above 64 KiB
returned LIMIT. Direct native launch requests below 1 MiB, unaligned and over
8 MiB returned BAD_REQUEST with no observer; zero/default and exactly 8 MiB
returned observers whose children exited normally.

## Read-only debugger inspection and cleanup

At `user_task_prepare`, the sample entry was `0x400000`, returned stack top
`0x7ffffffff000`, startup at `0x1000`, stack base `0x7fffffbff000` with 1,024
backed pages, and guard `0x7fffffbfe000` with one reserved page. VM ranges were
disjoint. Physical page-table walking showed guard PTE zero, stack user RW/NX,
code user RX and startup metadata user read-only/NX. No inferior function call
was made. A plain `echo -n` separately retained 256 backed stack pages at
`0x7fffffeff000` and a reserved guard at `0x7fffffefe000`.

Normal BSP retirement reached `process_task_reclaimed` with task null,
`task_storage` still true and committed EXITED/zero. Destruction reduced
allocated frames from 12,504 to the prior 11,429. A pipeline's second bundle
had a valid native magic prefix but a truncated image. The kernel rejected it
and rolled back its first prepared 4 MiB-stack child before publication:
PROCESS_PREPARING, task storage present, no committed result. Batch count fell
from one to zero and allocated frames from 12,492 to 11,429. At the restored
prompt, live heap allocations/bytes again matched 6,140 / 1,898,992; all 13
kernel pools were retained and retired arena bytes stayed zero. A subsequent
plain command and final root exit/drain succeeded.

These observations qualify native archive/RAM directory views and this small
bundle, not mutable-tree snapshots, ZIP backing, installed large-image capture,
Clang or physical hardware. Held directories do not freeze writable aliases;
published development revisions must stay unchanged and retained. Maximum JSON
parse-tree overlap (about 5 MiB) is a source-inspected bound, not measured peak
memory or full budget-pressure qualification. Task-owned clients, QEMU and GDB
were stopped after inspection.
