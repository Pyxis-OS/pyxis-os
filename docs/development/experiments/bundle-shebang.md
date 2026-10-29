# Bundle-interpreter shebang cost

## Build and fixture

Userland `c7a9739` (libpyxis selection `25be0e3`, shell `c7a9739`) on userland main
`053e65d`; parent base `78143c17`. Compiler clang 23.1.3 (`8bdf1360`) from the
builder container; full image (`DISTRIBUTION=full`).

QEMU/KVM q35 with the patched QEMU, four vCPUs, 4 GiB RAM, OVMF, VirtIO network,
no persistent disk. The development host is itself a KVM guest (Intel
i9-12900K), so these are nested-VM numbers; the host had no other owned guest
or build during the retained runs. Earlier runs that overlapped other users'
builds (load average above 7) are excluded.

## Method

Each cohort is a script of 100 lines, each launching a one-line script or
command that prints `BASELINE` to a file (`>` to `home://sink.txt`):

| Cohort | Line launched |
| --- | --- |
| native echo | `echo BASELINE` |
| native shebang | script `#!boot://init-shell.pxe`, then `echo BASELINE` |
| bundle shebang | script `#!bin://shell.pxb`, then `echo BASELINE` |
| explicit bundle | `bin://shell.pxb home://e2.pxs`, e2 being the same `echo BASELINE` without a shebang |

The host timed one Remote command (the 100-line script) from send to
`command_complete` and divided by 100; one warm-up, then nine measured samples per
cohort, cohorts interleaved and alternating order each round, three runs. A single
launch per Remote command was also tried; the Remote round trip (about 12 ms)
dominated and hid the difference, so it is not used.

## Results

Milliseconds per launch; median of each run's nine-sample median, with the
lowest and highest single sample across the three runs.

| Cohort | Median | Range |
| --- | ---: | ---: |
| native echo | 0.98 | 0.93-1.10 |
| native shebang | 3.15 | 3.07-3.74 |
| bundle shebang | 2.85 | 2.68-3.17 |
| explicit bundle | 2.95 | 2.77-4.50 |

Run medians (ms per launch): native shebang 3.13, 3.15, 3.27; bundle shebang 2.89,
2.85, 2.83; explicit bundle 2.96, 2.95, 2.88.

A bundle-interpreter shebang costs no more than the explicit launch of the same
bundle in these runs, and no more than a native-interpreter shebang. I did not
isolate why the bundle path is faster than the native `init-shell.pxe` one and
do not claim a speedup. The manifest preparation is the same work the explicit
launch does.

## Limits

One interpreter bundle (the shell) was timed. Functional checks used Lua 5.5 and
5.1 bundles and a purpose-built bundle (`cat` as entry) to observe the interpreter's
`app://`. Not exercised: Neovim `-c`, Remote successors, session panes, a bundle
replaced between resolution and launch (the selection is held by construction), and
native hardware.
