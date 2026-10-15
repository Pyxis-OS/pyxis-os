# Kernel log levels

Status: **direction accepted by the owner, 2026-10-14; implementation assigned
2026-10-15.** The [scope note](#scope-note) below is proposed and awaits the
owner's decisions.

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

## Scope note

Proposed 2026-10-15 for the implementation task; **not yet accepted**. Items
marked *Decision* need the owner's answer. The rest are implementation choices
that follow from the accepted direction.

### API

`include/kernel/log.h` replaces `klog` and the old `ktrace` with one call per
level. The levels are ordered from most to least severe:

```c
enum log_level {
  LOG_LEVEL_ERROR = 1,
  LOG_LEVEL_WARN,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_TRACE,
};

void klog(enum log_level level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void kvlog(enum log_level level, const char *format, va_list args);

#define kerror(...) klog(LOG_LEVEL_ERROR, __VA_ARGS__)
#define kwarn(...)  klog(LOG_LEVEL_WARN, __VA_ARGS__)
#define kinfo(...)  klog(LOG_LEVEL_INFO, __VA_ARGS__)
#define kdebug(...) /* compiled only when LOG_LEVEL_CEILING >= DEBUG */
#define ktrace(...) /* compiled only when LOG_LEVEL_CEILING >= TRACE */
```

Call sites use the five macros. `kdebug` and `ktrace` keep today's `ktrace`
pattern: format checking always, argument evaluation only within the ceiling.
`klog` itself is for callers that hold a level at run time, such as the uACPI
host adapter; it drops records deeper than the ceiling. Error, warn and info
cannot be compiled out. The `LOG_LEVEL_` prefix avoids the existing `LOG_READ`
and `LOG_SNAPSHOT` operation names in `<abi/log.h>`.

Panic output is error level and ignores the console threshold. The fatal path
otherwise stays as it is: no new locks, heap or GS dependency.

### Where records go

| Destination | Records | Level shown as |
| --- | --- | --- |
| Log ring, so also `log` and UDP capture | everything compiled in | text tag |
| Serial | at or above the console threshold | text tag |
| Early framebuffer console, Caelum tab | at or above the console threshold | colour |

- **Tag.** *Decision 2.* Each line in the ring and on serial starts with one
  letter and a space, `E`, `W`, `I`, `D` or `T`, before the subsystem prefix:
  `W hda: command timeout verb=…`. A line started by an embedded newline gets
  its own tag. Continuation calls on an unfinished line inherit that line's
  level, so a split line is never half drawn.
- **Colour.** The Caelum tab draws no tag. Errors are red, warnings yellow,
  and debug and trace dim if the threshold shows them. The early console is
  monochrome and stays so.
- **Replay.** When `log_set_tty` replays the retained log, it applies the
  threshold and colours to each line and strips its tag. The tab therefore
  shows the same lines whether they came before or after the replay.
- **Boot-init console output.** It is mirrored into the ring and serial as now,
  tagged `I`, and drawn by its own console path whatever the threshold. It
  carries no level of its own; that is out of scope.

### Build ceiling

```make
LOG_LEVEL ?= debug   # Decision 1; accepted values: info, debug, trace
```

`build/kernel-log-config.h` defines `LOG_LEVEL_CEILING`, replacing
`TRACE_ENABLED` and `KLOG_TRACE_ENABLED`. Any other value is a make error, as
today. `scripts/bundle.sh` already records `LOG_LEVEL` in the kernel bundle
inputs. Builds that pass `LOG_LEVEL=info` or `LOG_LEVEL=trace` keep working.
`info` builds compile exactly what today's default compiles.

### Boot option

`log.level=error|warn|info|debug|trace` sets the console threshold. It follows
the [#829](https://git.internal/PyxisOS/pyxis-os/pulls/829) rules: the last
duplicate wins, and a missing, empty or unknown final value warns and uses
**info**. A level deeper than the build ceiling is valid but cannot draw more
than was compiled in. It warns and uses the ceiling.

`boot_options_parse` runs early in `arch_init`. Only the banner and the Limine
adapter's first few lines come before it, all at info, so the early console
honours the option almost from the start. The threshold is fixed after parsing. No runtime setter is planned:
`LOG_RIGHT_READ` has no configuration operation, and adding one is a new
authority.

### `log` and UDP

The kernel log ABI does not change: `log` prints the tags, and UDP capture
carries them because it drains the ring. A userspace PR adds `log -l LEVEL`,
which prints that level and more severe ones and combines with `-f`. Its
reader finds tags at line starts across 1024-byte read chunks. It can merge
before or after the kernel PR. Untagged lines from an older kernel stay
visible.

### Migration

One kernel PR, in commits that can be reviewed separately:

1. **Core.** Levels, routing, tags, colour, replay filtering, the Makefile
   ceiling and `log.level=`. Every `klog(` becomes `kinfo(`, and every `ktrace`
   stays `ktrace` at trace level, so this commit compiles and draws the same
   lines as before. Two adapters map existing levels: uACPI error and warning
   to `kerror` and `kwarn`, info to debug, trace and debug to trace; lwIP
   `LWIP_PLATFORM_DIAG` to debug.
2. **Errors and warnings.** About 60 `kinfo` sites that report failures,
   refusals, timeouts or degraded fallbacks become `kerror` or `kwarn`. This
   includes the #829 boot-option warnings. Messages are not reworded, so
   existing text such as `ACPI: error:` stays.
3. **Debug.** One-off `ktrace` lines become `kdebug`: inventories, capability
   lines and state changes, such as the xHCI capability lines from vela.
   Per-operation lines stay at trace.

Classification rules:

- Lines enabled by an explicit diagnostic boot option, such as
  `usb.xhci.diagnose` or `display.timing.metrics`, stay info. The option is the
  request to see them.
- No info line moves to debug or trace unless it is per-operation; the default
  screen should not lose lines.

The [#851](https://git.internal/PyxisOS/pyxis-os/pulls/851) lines, per-idle
and per-exit, are already `ktrace` and stay at trace without an edit. The
HD Audio per-verb lines (`hda.c` verb/response, `hda-codec` verb/response)
also stay trace, with any per-sequence summary at debug. #851 then closes in
favour of this PR, or is rebased to only document the classification, as the
owner prefers.

Docs updated in the same PR: [kernel log](../interfaces/kernel-log.md),
[configuration](../development/configuration.md), the README
`LOG_LEVEL=trace` line, and this document rewritten as implemented behaviour.

### Cost and measurement

This may change cost in three ways:

- With a debug ceiling, the one-off `kdebug` records are formatted into the
  ring in ordinary builds.
- Serial output depends on Decision 3.
- The ring holds two more bytes per line and fills sooner in debug and trace
  builds.

Before implementing, the baseline at current main will be taken on nested KVM
QEMU, q35, 4 CPUs, 1 GiB, VirtIO SCSI CD-ROM, with five boots per build:

- serial bytes up to the shell prompt;
- ring line count and dropped lines from `log` at the prompt;
- host-timestamped time from QEMU start to `Caelum ready`.

The same workload is repeated afterwards for `LOG_LEVEL=info`, `debug` and
`trace`, and the native default boot is repeated on the ThinkPad if the nested
numbers move. Results go in the PR and the rewritten doc.

### Decisions

1. **Default build ceiling: debug** (default) or info. With debug, ordinary and
   native images keep one-off detail in the ring, readable with `log` without
   a rebuild. That was the vela problem. The cost is formatting those
   records, gated by the measurement above. With info, ordinary builds stay as
   they are and debug detail needs `LOG_LEVEL=debug`.
2. **Level marking: a one-letter text tag at the start of each ring and serial
   line** (default), or an out-of-band level byte in the ring's line header.
   The tag needs no ABI or UDP wire change and survives copy and paste. A
   header byte would need a `log_read_reply` change to expose per-line levels
   and a matching UDP format change. The tag letters become a parse contract
   for `log -l`.
3. **Serial follows the console threshold** (default), or serial carries
   everything compiled in, like the ring. Serial output is synchronous with
   interrupts off and the log lock held. A real 115200-baud UART costs about
   87 µs per byte, and QEMU takes a VM exit per byte. The ring and UDP are the
   cheap full captures. Under the default, a QEMU trace run that wants trace
   on serial also passes `log.level=trace`.
