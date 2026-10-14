# Capability model: native baseline run sheet

Prepared 2026-10-14 against main `3b714754`, userland `ec234db4`, ports
`2296a06c`, fs `414be0ac`, lwIP `a1aadb91`. **Not run.** The native baseline
gate in the [implementation plan](../../wip/capability-model-implementation.md)
remains open; task 1 must not merge before the agreed measurements are recorded.
The method choice below needs the owner before Luna stages the batch.

## Stage and conditions

Luna builds current main with its exact pins using the ordinary image path and
records the complete revisions, image/ELF hashes, compiler image, kernel
configuration and boot options. Keep the boot archive's installed
`share/iobench.bin`, `iobench`, Lua and `echo`; add no benchmark executable.
Use the stock local Development session with its declared clock, memory,
launcher, pipe and service grants and the `text` binding. Missing authority or
tools is a failed setup, not a zero sample.

Record ThinkPad model, CPU count, live/installed boot, display/session setup,
power state and background workloads. Use AC, idle input and no competing work.
Timed boots have no profiler or debugger. Retain every sample and failure in the
batch log; summarize median and range, without removing slow successful samples.
Repeat the same methods and configuration after milestones A and C. Nothing
here measures host/nested QEMU or physical-device read-back.

## Timed batch

Run each OPEN or launch command three times. Each Lua invocation performs one
untimed warmup and ten samples, printing nanoseconds. Close, verification and
printing are outside its OPEN interval. These are manual commands for the
existing interpreter, not an installed measurement framework.

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

Small-program launch **through exit**, proposed method:

```text
lua -e 'local args={"bin://echo.pxe","-n"}; local run=pyxis.run; local now=pyxis.monotonic_ns; assert(run(args)==0); for i=1,10 do local a=now(); local s=run(args); local b=now(); assert(s==0); print(i,b-a) end'
lua -e 'local now=pyxis.monotonic_ns; local a=now(); for i=1,1000 do now() end; local b=now(); print("clock-wrapper-loop ns/call",(b-a)/1000)'
```

`echo -n` emits no payload. `pyxis.run` still includes argument allocation/copy,
image lookup, grant preparation, launch, child startup/execution, WAIT and cleanup.
This is not isolated launcher-return or child-entry latency. Record the clock
reference without subtracting it; Lua GC and scheduling remain included.

Pipe transfer, three fresh sessions/boots with the same settings:

```text
session bin://iobench.pxe pipe --buffer 4096 --rounds 10
```

`session` replaces the shell and does not restore its prompt. This uses a 64 KiB
byte pipe and a verified 1 MiB transfer, with fresh workers per pass and one
warmup. Launch, allocation, preparation, readiness, verification and reporting
are outside timing. Keep both acceptance and completion intervals; headline
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

At the checkpoint, inspect each configured CPU's current/ready user tasks:

```gdb
set may-call-functions off
p schedulers[0].current_task
p schedulers[0].ready_head
set $task = schedulers[0].ready_head
```

Only for a nonzero task, inspect its `process`, `cpu_index`, `entry` and `next`;
skip kernel workers with no process. After locating a live process:

```gdb
set $process = $task->process
p $process->space->name
p/x $process->startup_address
set $range = $process->address_space->head
while $range && $range->base != $process->startup_address
  set $range = $range->next
end
p $range
```

Only for a nonzero, fully backed startup range, record `state`, `pages`,
`backed_pages` and `pages * 4096`. These are kernel-mapped metadata reads, not
access through another process's user page tables. Record the observed task and
space; use the same identifiable role and configuration in three inspection
boots and the matched runs. The checkpoint guarantees no particular user task
on the current/ready lists. If a comparable process cannot be identified, mark
this item unavailable and keep the baseline gate open.

## Method decision and result record

**Proposed default:** use these existing-tool Lua OPEN and launch-through-exit
intervals, plus the checkpoint-observed full startup allocation of an identified
live process. Alternative: require isolated launch or a particular child's
startup size; current tools cannot reliably observe those boundaries, so the
owner must authorize a narrow measurement extension before the gate can close.

After the batch, add only configuration, commands, sample counts, medians/ranges,
startup allocation and limits here. Raw logs stay in the batch folder/PR. No
native result is claimed yet.
