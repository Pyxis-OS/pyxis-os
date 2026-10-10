# Shared process state qualification

2026-10-10, [working path and environment](../../../userland/process-state.md).
Nested-KVM QEMU results, not native hardware qualification. Scratch consumers,
screenshots, JSON and logs remain local under `build/state-{baseline,qual}`.
No benchmark infrastructure, in-tree tests or CI jobs were added.

## Inputs and builds

Before code: fresh main `d0423029`, userland `fbce73bf`, ports `4326582e`,
filesystem `b427df29`. The ordinary SDK/image build passed. Implementation:
userland `e95370c7`, ports `4f01913b`, plus the parent startup member rename at
unchanged offset/layout. SDK, all first-party applications and the complete
ordinary image built using `pyxis-llvm23.1.3-49e2c1a` (LLVM 23.1.3). Public
working-path/environment headers also compiled from C++ with `-Werror`.

Ports libuv/relay, Lua, Links, Fastfetch, TCC host/guest and EDuke32 built against
the updated SDK; each changed ordered patch applied to its actual pinned source
with strict whitespace checks. Final ports `be901cf5` rebases the identical
migration files onto main `cf348aa` (grep); parent integration includes main
`838fde7f`. Measurements remain tied to the frozen inputs above.

## Interleaved launch cost

QEMU 10.2.2: Q35, KVM, host CPU, four cores/one thread each, 256 MiB, standard VGA,
matching raw OVMF CODE/VARS, VirtIO SCSI CD, RNG and modern VirtIO network with
user backend. No HOST mount, disk, profiler or debugger in timed boots. One VM
alternated frozen before/after ISOs and reset into fresh RAM, preserving its
OVMF variables. The same existing client sent 1,024 `echo -n` commands then exit:

```text
/usr/bin/time -f %e -o sample.seconds pyxis-remote --machine --no-shell-echo --columns 120 --rows 40 127.0.0.1 <forwarded-port> < plain-launch.txt > sample.jsonl
```

Before-code baseline: **14.72 s**, all 1,024 children exited zero and complete
drain. Matched samples repeated after implementation, seconds:

| Pair | Before | After |
| --- | --- | --- |
| 1 | 14.27 | 14.06 |
| 2 | 13.99 | 15.00 |
| 3 | 15.29 | 14.11 |

Before median **14.27 s (13.99–15.29)**; after **14.11 s (14.06–15.00)**.
Every sample completed exactly 1,024 successful children, shell exit zero and
`drain=complete`. Ranges overlap and paired differences change sign: no
reproducible launch regression or speedup established. This measures the whole
remote session, including transport/framing/startup/drain, not isolated loader
latency. Snapshot copying/retention adds real work whose smaller cost is not
resolved by these shared-host measurements.

## Manual QEMU and debugger results

An authorized scratch C program replaced only Development's init in a separate
image. It used the updated libuv and libc, not placeholders:

- `chdir` and relative fopen/stat followed `tmp://state/a`; `missing/..` returned
  ENOENT and preserved cwd. Short getcwd returned ERANGE without changing four
  sentinel bytes; the allocation form returned the tracked description.
- Value input was copied; overwrite zero retained it; empty value stayed present;
  invalid removal returned EINVAL and absent removal succeeded. Owned environment
  enumeration/free and libuv loop init/close returned success.
- A default child retained cwd A and value `captured`, reading A's marker after
  its parent moved to B and changed the value. An explicit relative child cwd
  selected A without changing the parent; replacement environment omitted other
  values, and an explicit empty environment exposed none. Invalid child cwd
  returned UV_ENOENT, remained closable and left the loop clean.
- All children exited normally/zero with native reason EXITED. Parent state was
  unchanged by child-local mutation; a held environment snapshot kept its earlier
  value. Read-only GDB inspected the Development TTY and its resulting text.

The ordinary remote shell passed cd, relative mkdir/redirect/copy/rename/read/ls/
remove/rmdir and clean exit. Its relay-bundle request was denied under Remote's
existing delegation policy. A separate scratch mux-enabled boot showed the first
pane changing cwd and reading its relative file, while a new second pane began
at the parent's home cwd and read that file through an explicit scheme path.
GDB inspected the active Development space; temporary config was restored.

Inspected, not forced: allocation-failure preservation and native lookup errors,
unknown/stale description handling, read-only trust enforcement, uncertain-close
cleanup, bootstrap cwd clear before original-grant retirement, snapshot lifetime
through network overlays/launch capture. No allocation fault injection, HOST/
installed-image/native run, game execution or threads qualification. Proved
realpath and the remaining Neovim closure are later slices.

## Integration refresh after tab completion

Userland `3bd6c21f` rebases the slice onto main `1b6d26a` (#200), adapting
the new root-completion access to the shared context. Parent code integration
`9fb37ef0` merges Pyxis main `5531dfd1`; ports stays `be901cf5`. The ordinary
`make -j16 image` build passed with the same compiler. Rebuilt scratch consumers
repeated the cwd/environment/child checks above in the same four-CPU Q35/KVM
configuration, with GDB inspection confirming the expected results. A separate
ordinary boot completed `cd tm<Tab>` to `tmp://`, then `cat al<Tab>` to the
relative file `alpha` and read its contents. Ordinary image inputs were restored;
all task-owned VMs/debuggers stopped. Earlier launch samples retain their original
inputs; this refresh did not change the launch implementation.

## Realpath and descriptor slice (2026-10-10)

Baseline: Pyxis main `fa390738`, userland `0b1ede1e`, ports `9f64d683`.
Implementation: userland `9f70b758`, ports `22b8b4b2`; kernel unchanged. Ordinary
`make -j16 image` and libuv/uv-relay builds passed with the same LLVM 23.1.3
builder. The authorized local scratch program built against the refreshed SDK.
Four-CPU Q35 nested KVM, host CPU, 256 MiB, standard VGA, matching OVMF, SCSI CD
and RNG; GDB read-only inspection confirmed zero failures:

- Realpath proved files/directories, normalized relative spellings, preserved
  aliases and a LOOKUP-only directory grant. Ordered missing/.., file/..,
  boundary escapes, bound overflow and a live provider without OPEN failed.
  A native child with real cwd grants but no description retained relative
  authority while getcwd/relative proof failed; explicit scheme proof worked.
  Removing and recreating a held empty cwd left getcwd descriptive and caused
  relative proof to fail. Libuv returned a proved result or ENOENT and cleaned up.
- Fdopen mode/access/association failures left ownership unchanged. FILE r/w
  selection narrowed O_RDWR; w preserved size/cursor. Dup shared cursor and
  read-ahead across raw/FILE I/O; closing one file or pipe alias kept unread data
  alive. Final fclose invalidated its descriptor; a closed association did not
  regain access. Append applied across aliases.

Before code, existing `iobench` read of a verified 1 MiB RAM fixture measured
0.328–0.356 ms payload time. Matched frozen ordinary ISOs then alternated in one
VM, three samples per boot after one warmup. Commands through the existing
remote client: `iobench write tmp://io.bin --rounds 3`, then
`iobench read tmp://io.bin --rounds 3`. Modern VirtIO network/user forwarding;
no HOST, disk, profiler or debugger in timed boots. Payload medians, ms:

| Pair | Before | After |
| --- | --- | --- |
| 1 | 0.335 | 0.343 |
| 2 | 0.361 | 0.315 |
| 3 | 0.330 | 0.341 |

Nine samples each: before median **0.335 (0.328–0.475)**, after **0.341
(0.315–0.716)**. Every fixture and child completion passed, with full drain.
Ranges overlap and paired differences change sign; no stable read-cost change
established. These are RAM descriptor reads, not physical storage or isolated
realpath/fdopen timing. Allocation-failure ordering, missing identity and one-shot
close uncertainty were inspected, not injected. No native qualification. Raw
programs/logs remain under local `build/slice2-{baseline,qual}`; ordinary image
inputs restored and task-owned processes stopped. Freopen/buffering and the
Neovim recipe remain later work.
