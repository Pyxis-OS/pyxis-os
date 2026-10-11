# Hosted Clang C++ closure (2026-10-11)

[Contract](../../wip/hosted-clang.md#parallel-delivery-tasks),
[userland #216](https://git.internal/PyxisOS/pyxis-userland/pulls/216),
[fork #5](https://git.internal/PyxisOS/pyxis-llvm/pulls/5),
[Pyxis #714](https://git.internal/PyxisOS/pyxis-os/pulls/714).
Baseline: Pyxis `466d2ed5`, userland `df210f1a`, ports `df3210b0`, filesystem
`b427df29`, installed compiler/runtime `49e2c1a`. Qualified source: userland
`e94e2f3` (rebased without closure changes as `22287160` onto `d560fbb`), fork
`604f008d` atop merged Support `6951d3c9`. Final ordinary SDK/kernel build uses
Pyxis base `34dfce9e` and userland `22287160`.

## Builds and functional checks

Existing builder `pyxis-llvm23.1.3-49e2c1a`; no compiler-container/pin change.
Ordinary `make -j16 sdk` passes before and after, and final `make -j16 sdk kernel`
passes with the documented Kconfig Python dependency. Local fork runtimes use
Generic/Pyxis, Release, static libunwind/libc++abi/libc++, existing bare-metal
unwind and non-demangling terminate policy. Localization, wide characters and
random_device are enabled; threads/filesystem/TZdb remain off. No placeholder
functions, Unix locale objects or target host-libc linkage.

```sh
make -j16 sdk kernel
cmake --build <fork>/build/pyxis-cxx -- -j16
cmake --install <fork>/build/pyxis-cxx
# Real source TUs, real SDK + freshly installed fork C++ headers/archives:
clang++ --target=x86_64-unknown-pyxis --sysroot=<qualified-sdk>/sysroot \
  -std=c++17 -O2 -fno-exceptions -fno-rtti -DLLVM_ON_PYXIS=1 \
  -I<fork>/llvm/include -I<generated-llvm>/include \
  -I<fork>/clang/include -I<generated-clang>/include -c <source.cpp>
```

`raw_os_ostream`, `Mustache`, `ConvertUTFWrapper`, `LockFileManager` and
`PPMacroExpansion` now compile; all five failed against the original disabled
profile. No target compiler link/run or synchronization/TLS qualification.
The production SDK still follows the installed `49e2c1a` profile; local richer
qualification uses the actual rebuilt headers/archives, not a moved source pin.
Container publication is required before adopting the fork, coordinated with
threads gate 7.

Foreground manual QEMU helper checks pass: string streams (including floating
insertion/extraction), classic locale data/name refusal, `put_time`, wide strings,
numeric formatting/end/range behavior, UTF-8 split/count/bounded conversions and
invalid scalars; wide FILE orientation, encoded byte positions, read-ahead,
whole-character pushback, failed-seek preservation and truncated-input error;
wide standard streams and custom-wide-locale refusal; entropy 256-byte success,
size refusal with unchanged destination, random_device/token behavior and
missing-grant refusal. Ordinary byte streams still work. Buffered-error paths
were inspected, not fault-injected. Console glyph absence is separate from UTF-8
encoding, which the FILE checks compare byte for byte. No native ThinkPad run.

## Matched costs and size

QEMU 10.2.2, nested KVM, q35, CPU max, four cores/one thread, 256 MiB, UTC RTC,
OVMF, VGA, VirtIO-SCSI CD/RNG/net, no disk/HOST mount or profiler. Same frozen
[task-1 control](hosted-clang-support.md#configuration-and-builds) kernel/shell
and grants throughout (`1f230e79`, source-equivalent measured kernel/ABI inputs
`2fca30fd`; userland `df210f1a`). This isolates helper headers/libraries, rather
than substituting today's changed scheduler/IPC/thread kernel into the timing
control. Ordinary current-main build evidence above is separate.

Before edits: verified 1 MiB RAM write/read and 10,000 `string` construct/append
operations (420,000 resulting characters), four iterations per invocation,
first excluded, three invocations. Medians: write 10.160 ms (9.904–10.220),
read 0.418 ms (0.398–0.631), strings 0.343 ms (0.338–0.389).
Afterward, identical helper source rebuilt against each real SDK/runtime;
manual interleaved A1/B1/A2/B2/A3/B3 in one boot, four iterations each, first
excluded (nine retained samples per version). No task-owned build overlaps these
retained runs. One earlier overlap set is discarded. Commands:

```text
boot://cxx-before.pxb
boot://cxx-after.pxb
# Repeat this pair three times; each prints four verified iterations.
```

| Measurement | Before median (range), ms | After median (range), ms |
| --- | --- | --- |
| 1 MiB RAM write | 17.686 (10.387–21.182) | 11.191 (10.138–13.247) |
| 1 MiB verified read | 0.591 (0.445–0.960) | 0.455 (0.429–0.516) |
| 10,000 string operations | 0.479 (0.347–0.827) | 0.341 (0.336–0.429) |

Variation is large and ranges overlap, with earlier baseline close to the low
end; these numbers establish no attributable speedup or host/native cost.
Clock-call cost is included. No storage sync/durability measurement. Launch
implementation is unchanged; no new launch timing claim.

| Artifact | Original subset bytes | Richer closure bytes |
| --- | --- | --- |
| libc++ archive | 605,368 | 2,028,726 |
| libc++abi archive | 587,078 | 586,718 |
| libunwind archive | 76,198 | 76,198 |
| Matched string/FILE helper | 159,047 | 193,823 |

The all-facility qualification helper is 590,799 bytes. Archive growth is not
per-application residency: static linking selects reached code. `mbstate_t`
grows to 12 bytes; consumers and libc++ must be rebuilt with matching headers.
