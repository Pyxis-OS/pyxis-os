# procs

`procs` lists processes and execution groups. It is a native command that shows
Pyxis concepts (spaces, execution groups, per-boot keys), not a `ps` or `top`
port. It ships in the `procs` package, a member of base.

```text
procs [--all] [--sort cpu|mem|name|age] [--live]
```

- **Default:** this space as a tree. Ungrouped processes come first, then each
  execution group with its member processes indented beneath it.
- **`--sort`:** a flat list of processes, ordered by CPU %, private memory, name
  or age.
- **`--all`:** every space, with a SPACE column. It uses the system process view,
  which the `procs` manifest requests as `process_view/list`; without that grant
  `--all` fails and the default listing still works.
- **`--live`:** redraws every second on a terminal. `c`, `m`, `n`, `a` and `t`
  switch to CPU, memory, name, age and tree order; `q` or Ctrl+C exits.

## Columns

| Column | Meaning |
| --- | --- |
| KEY | per-boot listing key, never reused; not a process ID, and no command accepts it |
| STAT | `run` (running or queued), `wait` (every thread blocked) or `exit` (stopping, threads remain) |
| THR | live threads |
| CPU% | CPU time over the last interval: 250 ms for a one-shot listing, the refresh for `--live`; can exceed 100 with several threads |
| TIME | user plus kernel CPU time so far |
| MEM | committed private memory: image, stacks, MEMORY and JIT regions; not RSS, and owned objects such as RAM files are not counted ([debt](https://git.internal/PyxisOS/pyxis-os/issues/1604)) |
| AGE | time since the process was published |
| NAME | the program's first argument's last path component |

Group rows show their member count and `sealed`, `stopping` or `complete`.

## Limits

- **Names can be spoofed:** they come from whatever the launching program put
  in argv[0].
- **Rows are samples:** state, CPU and memory may change while the listing is
  taken.
- **Truncation:** at most 64 KiB of rows per listing (about 450). A longer
  listing prints a truncation line
  ([debt](https://git.internal/PyxisOS/pyxis-os/issues/1603)).
- **Read-only:** stopping a process from the list is later work.
- **No CPU columns without a TSC rate:** when the kernel knows no TSC rate,
  CPU% and TIME show `-`.

The kernel interface is [process listing](../interfaces/process-listing.md).
