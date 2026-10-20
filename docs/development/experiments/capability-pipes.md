# Capability pipes first slice

Qualification and matched timing for milestone C task 9 (the first slice:
[value segments](../../interfaces/pipes.md#value-segments)), 2026-10-20.

## Revisions and configuration

| Image | Parent | Userland | Ports | fs / lwIP |
| --- | --- | --- | --- | --- |
| Before (byte pipes between user stages) | `a89a6933` | `ddb13012` | `d66853b7` | `f3f22b6e` / `a1aadb91` |
| After (merged slice) | `5d13f5d6` | `89bb578c` | `053d5714` | same |

Both images have the task 1 kernel; only the userland adoption differs. Built
with `make -j16 image BOOT_MENU_TIMEOUT=60` in `pyxis-llvm23.1.3-3d69e83`, default
configuration. ISO SHA256: before `d9ba2e13…5c81921`, after `d250d6fd…9153c47`.
QEMU 10.2.2, q35/KVM, `-cpu max`, four CPUs, 4 GiB, OVMF, VirtIO block/net/RNG,
nested in the shared Fedora VM (12 vCPUs), driven through plain Remote.

## Qualification (after image)

Kernel GDB dprintfs counted pipe operations and file reads.

| Check | Result |
| --- | --- |
| npfs file (32 MiB, read-only `system://` on a `MOUNT_DISK` variant of the after image) | `cat \| wc -c` 33554432 with no npfs reads, against 8210 when read through `cksum`; `cksum` matches the host |
| RAM and boot files | `cat \| wc -c` 1048576 with no file reads; `wc`, `cksum` match direct runs |
| HTTP reply snapshot (32 MiB, local server through slirp) | `wc -c`, `cksum` correct; `head -n 1`, `head -c 9 \| wc -c` correct |
| HTTP 404 | `cat` names the URL; `wc` exits 1, `head` 1, `grep` 2, each with Not found |
| Script writing a value then bytes | `cat FILE; echo tail` piped: `wc -c`, `wc -l`, `cksum` (host-checked), `cat` in order |
| `head` | Prefix of npfs, RAM and HTTP values; an early close leaves `cat` with its usual EPIPE |
| libuv try path | luv stdin reader counts value, mixed and byte-only input (TRY_READ, TRY_RECEIVE) |

Task 1 recorded the kernel probe: 59 checks on one and four CPUs, cross-process
blocking receive and full-slot handoff. The `MOUNT_DISK` variant used a pool
`config/boot.lua` adding a Remote space with a read-only `system` root; that space
has no HTTP service, so HTTP ran on the ordinary after image.

## Matched timing

A 32 MiB `tmp://big.bin` (32 copies of `boot://share/iobench.bin`). Each cohort
is one boot running, through Lua's existing `pyxis.run` and `pyxis.monotonic_ns`,
one warmup and ten samples of `shell -c "WORKLOAD"` per workload. Before and
after boots alternated, three cohorts each, twice; the second run added the
DIRECT and CKSUM rows. The VM reported 0–5% steal; one other agent's QEMU was
present during some cohorts; no build ran.

```text
lua -e 'local now=pyxis.monotonic_ns; local a={"bin://shell.pxb","-c",W}; assert(pyxis.run(a)==0); for i=1,10 do local t=now(); local s=pyxis.run(a); local u=now(); assert(s==0); print(i,u-t) end'
```

| Nested elapsed ms, run 2 | Before cohort medians | After cohort medians | Before / after range |
| --- | --- | --- | --- |
| `cat tmp://big.bin \| wc -c` | 7076.0 / 6890.3 / 6855.6 | 26.0 / 24.4 / 22.4 | 6837–8689 / 14.2–32.8 |
| `wc -c tmp://big.bin` | 8432.8 / 8187.0 / 8165.2 | 69.5 / 37.8 / 34.2 | 8156–9282 / 27.2–104.5 |
| `cat tmp://big.bin \| cksum` | 127.0 / 124.5 / 118.1 | 115.0 / 92.7 / 92.8 | 110.6–243.7 / 83.5–137.0 |
| `exit 0` (shell launch) | 13.4 / 16.7 / 11.4 | 12.8 / 15.1 / 11.6 | 5.4–21.5 / 4.8–17.8 |

Run 1 (`cat | wc -c`, shell launch) agreed: before medians 7090–7431 ms, after
26.7–27.3 ms.

Attribution: unpatched sbase `wc -c` decodes UTF-8 one character at a time
through stdio, which dominates the before pipeline (the direct row). The `wc`
patch counts `-c` with `fread_some`, so the after direct row is a byte read and
the after pipeline is SIZE only, close to launch cost. The pipe change for a
byte reader is the CKSUM row: libc reading the value with FILE reads instead of
`cat` copying 32 MiB through the pipe.

## Limits

Nested elapsed observations, not native hardware costs, with uncontrolled
shared-host load. RAM-backed input only for timing; npfs and HTTP were
qualified, not timed. Outputs, GDB captures and images stay local.
