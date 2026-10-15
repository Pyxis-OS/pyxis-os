# Kernel log levels

Status: **direction accepted by the owner, 2026-10-14; scope accepted
2026-10-15 ([below](#accepted-scope)); implementation in progress.**

## Problem

The kernel has two levels: `klog`, always compiled in, and `ktrace`, compiled in
only with `LOG_LEVEL=trace`. About 400 `klog` and 120 `ktrace` call sites share
them, and roughly 60 `klog` lines are errors or warnings with nothing marking
them. In a trace build, per-operation lines (one per idle CPU, per process exit,
per HD Audio verb) scroll the lines that matter off the Caelum tab: on vela, the
xHCI capability lines were present but unreadable. Deleting those lines would
lose information that is occasionally useful.

## Direction

- **Five levels:** error, warn, info, debug and trace. One-off state changes and
  inventories that `ktrace` carries today become debug. Per-operation detail
  becomes trace, the deepest level, rather than being removed.
- **Every record carries its level** in the log ring and in UDP capture, next to
  its existing subsystem prefix.
- **Two controls:** `LOG_LEVEL` sets the compile-time ceiling, so arguments of
  deeper levels are not evaluated in ordinary builds. A runtime console threshold
  decides what the Caelum tab draws. The ring keeps every compiled-in record, and
  `log` can filter by level when reading.
- **Default console threshold: info**, which keeps today's screen. A `log.level=`
  boot option changes it, with the warn-and-default rule for invalid values.
  Errors and warnings are drawn in a distinct colour.
- **Per-subsystem thresholds are deferred.** Records keep their subsystem
  prefix; separate thresholds come only if the global ones prove too coarse.

## Migration

Classify the existing call sites without rewording their messages. The parked
[#851](https://git.internal/PyxisOS/pyxis-os/pulls/851) list is the starting point
for trace: the per-idle and per-exit scheduler lines, and the HD Audio per-verb
lines, whose per-sequence summary becomes debug.

## Accepted scope

Scope proposed in [#867](https://git.internal/PyxisOS/pyxis-os/pulls/867);
the owner accepted it with these decisions on 2026-10-15:

1. **Default build ceiling: debug.** The cost is measured as planned, and
   reported if it moves.
2. **The level is a byte in the log ring's line header**, not text. The log ABI
   and the UDP format change. `log`, `log -l` and `pyxis-log` change with them,
   with no backward compatibility. Message text stays free-form and is never
   parsed for a level.
3. **Serial shows only lines at or above the console threshold.**

### API

`include/kernel/log.h` replaces `klog` and the old `ktrace` with one call per
level. `enum log_level` is ABI, in `<abi/log.h>`, ordered from most to least
severe: `LOG_LEVEL_ERROR = 1`, `WARN`, `INFO`, `DEBUG`, `TRACE`.

```c
void klog(enum log_level level, const char *format, ...);
void kvlog(enum log_level level, const char *format, va_list args);

kerror(...)  kwarn(...)  kinfo(...)  kdebug(...)  ktrace(...)
```

Call sites use the five macros. `kdebug` and `ktrace` keep the old `ktrace`
pattern: format checking always, argument evaluation only within the build
ceiling. `klog` is for callers that hold a level at run time, such as the uACPI
host adapter; it drops records deeper than the ceiling. Error, warn and info
cannot be compiled out. Panic output is error level, which every threshold
shows.

### Where records go

| Destination | Records |
| --- | --- |
| Log ring, so `log` and UDP capture | everything compiled in |
| Serial, early framebuffer console, Caelum tab | at or above the console threshold |

- A line's level is the level of the record that started it, stored once in
  the ring's line header beside its length. The header grows from four to
  five bytes.
- Output that has no level of its own is info: serial-mirrored console writes
  from boot init and the `LOG_PUTCHAR` system call. Boot init's console output
  is still drawn by its own console path whatever the threshold.
- The Caelum tab draws errors in red, warnings in yellow, and debug and trace
  in grey if the threshold shows them. Each record restores the default colour
  after its text. The early console is monochrome and stays so.
- When `log_set_tty` replays retained lines, it applies the same threshold and
  colours, so the tab shows the same lines before and after the replay.

### Log ABI

A READ reply's payload becomes a packed sequence of records. Each record is a
four-byte `struct log_record` header, `{uint16_t length; uint8_t level;
uint8_t reserved;}`, followed by `length` text bytes of one line. A line longer
than the remaining capacity, or unfinished, continues in the next read under
the same level. Headers are unaligned and are copied before use. The payload
limit stays `LOG_READ_MAX` (1024) bytes, headers included. Cursors, snapshots,
loss reporting and authority are unchanged.

### UDP format

Each datagram already carries one piece of one line. Header byte 18, which had
to be zero, now carries the line's level; byte 19 stays zero. Fatal datagrams
are error level. A receiver from before the change rejects the datagrams
because byte 18 is nonzero, so `LOG_UDP_VERSION` stays 1.

### Readers

- `log` prints each line with a one-letter level column (`E`, `W`, `I`, `D`,
  `T`). `log -l LEVEL` prints that level and more severe ones, and combines
  with `-f`. The column is formatted by the reader from the record's level; it
  is not stored.
- `pyxis-log` prints the same column and gains `--level LEVEL`.

### Build ceiling

`LOG_LEVEL ?= debug`, accepting `info`, `debug` or `trace`; any other value is
a make error. `build/kernel-log-config.h` defines `LOG_LEVEL_CEILING`,
replacing `TRACE_ENABLED` and `KLOG_TRACE_ENABLED`. `scripts/bundle.sh`
already records `LOG_LEVEL` in the kernel bundle inputs. `LOG_LEVEL=info`
compiles what the previous default compiled.

### Boot option

`log.level=error|warn|info|debug|trace` sets the console threshold, following
the [#829](https://git.internal/PyxisOS/pyxis-os/pulls/829) rules:

- The last duplicate wins.
- A missing, empty or unknown final value warns and uses **info**.
- A level deeper than the build ceiling warns and uses the ceiling.

`boot_options_parse` runs early in `arch_init`. Only the banner and the Limine
adapter's first few info lines come before it. The threshold is fixed after
parsing. There is no runtime setter: `LOG_RIGHT_READ` has no configuration
operation, and adding one would add an authority.

### Migration

One kernel PR, in separately reviewable commits, and a userland PR for `log`
that merges first:

1. **Core.** Levels, ring header, ABI records, UDP byte, routing, colour,
   replay filtering, the Makefile ceiling and `log.level=`. Every `klog(`
   becomes `kinfo(` and every `ktrace` stays `ktrace` at trace level, so the
   default screen draws the same lines as before. The uACPI adapter maps
   error and warning to `kerror` and `kwarn`, info to debug, and trace and
   debug to trace. lwIP `LWIP_PLATFORM_DIAG` becomes debug.
2. **Errors and warnings.** `kinfo` sites that report failures, refusals,
   timeouts or degraded fallbacks become `kerror` or `kwarn`, including the
   #829 boot-option warnings. Messages are not reworded.
3. **Debug.** One-off `ktrace` lines become `kdebug`: inventories, capability
   lines and state changes. Per-operation lines stay at trace.

Classification rules:

- Lines enabled by an explicit diagnostic boot option stay info.
- No info line moves deeper unless it is per-operation.

The [#851](https://git.internal/PyxisOS/pyxis-os/pulls/851) lines, per-idle
and per-exit, stay `ktrace`, and #851 closes in favour of this work. The HD
Audio per-verb lines also stay trace.

### Measurement

The baseline is taken at current main before implementation, with matched
runs afterwards:

- **Configuration:** nested KVM QEMU, q35, 4 CPUs, 1 GiB, VirtIO SCSI
  CD-ROM, five boots per build.
- **Measured:** serial bytes to the shell prompt, ring lines and dropped lines,
  and host-timestamped time from QEMU start to `Caelum ready`.
- **Builds:** after implementation, `LOG_LEVEL=info`, `debug` and `trace`.
