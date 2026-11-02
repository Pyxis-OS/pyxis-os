# Dynamic linking baseline (task 0)

The static-linking baseline for [dynamic linking](../../wip/dynamic-linking.md)
task 0, recorded on 2026-11-02 so that each later task can compare against it.
The QEMU, CI and size rows are below. The native vela rows will be added after a
scheduled run.

## Inputs

- **Revisions:** pyxis-os main `ea4f42eb17fe`, userspace `4da7da62`, ports
  `52381a05`, fs `f3f22b6e`, lwIP `4fb74e1b`.
- **Build:** the existing builder `pyxis-llvm23.1.3-8bdf136` built the core and
  full images, every public package (base and development), and one local
  fixture.
- **Packing:** the base set was packed with the manifest-check fix that later
  landed in [#1670](https://git.internal/PyxisOS/pyxis-os/pulls/1670).
  `prism-dev` didn't pack at this revision, for the same reason.
- **Fixture:** imgui-demo's `demo.cpp` with `if (argc < 2) return 0;` at the top
  of `main`, linked exactly like imgui-demo.
  - It carries the whole SDL2-OSMesa, Mesa, ImGui and libc++ closure:
    63,521,266 bytes, against imgui-demo's 63,521,250.
  - It was packed as a local `imgui-fixture` bundle with imgui-demo's manifest,
    minus the display, keyboard and pointer grants.
  - Chronium accepted it as a measurement fixture. It is never committed or
    published.

## QEMU configuration

- **QEMU:** QEMU 10.2.2 under nested KVM, with the
  [threads task 1](threads-task1/README.md#inputs-and-configuration) machine
  definition.
- **Memory:** 1 GiB, not 512 MiB. The fixture's image, its capture and the RAM
  package volume don't fit together in 512 MiB.
- **Image:** the full ISO, because the core image boots the installer.
- **Packages:** `printf`, `nvim`, `imgui-fixture` and `imgui-demo` were
  installed from a local unsigned repository on the host.
- **Timing:** each workload ran as one Remote session, timed on the host with
  `/usr/bin/time` around `pyxis-remote --machine` as in the earlier records.
  - **Lua:** `echo -n` is from the boot archive and runs in the existing Lua
    loop.
  - **Shell:** the packaged programs run by explicit `pkg://` path in shell
    command sequences, 16 commands to a line, because Lua's `pyxis.run` resolves
    command names only. Each line ends with `echo -n`.
  - **Checks:** every sample was checked from its output: 1,024 `x` from
    `printf`, 64 Neovim version banners, and no launch errors.
- **Order:** a 16-launch warmup came first, then three samples per workload in
  the order shown.
- **Host load:** the runs started only after the host's one-minute load had
  stayed below 2 for two minutes, and it stayed around 2 during them. Earlier
  runs during another agent's build varied up to sixfold and are discarded.

## Launch results (whole session, seconds)

| Workload | 1 CPU | 4 CPUs |
| --- | --- | --- |
| `echo -n` ×1,024, boot archive, Lua loop | 0.86, 0.86, 0.87 | 0.88, 0.88, 0.89 |
| `pkg://printf/printf.pxe x` ×1,024 | 5.15, 5.15, 5.16 | 5.25, 5.34, 5.37 |
| `pkg://nvim/nvim.pxb --version` ×64 | 2.62, 2.63, 2.68 | 2.21, 2.30, 2.99 |
| `pkg://imgui-fixture/imgui-fixture.pxb` ×16 | 1.93, 1.93, 1.95 | 1.91, 1.91, 2.16 |

Per launch, that is about 0.85 ms for `echo`, 5 ms for packaged `printf`,
35–41 ms for Neovim and 120 ms for the 63.5 MB fixture.

- **What is included:** each interval includes session startup, shell or Lua
  interpretation, output transport and the final drain. It does not isolate
  loader latency.
- **Comparisons:** sequence and Lua-loop rows are not comparable to each other,
  only to their own after-runs.
- **Archive versus packages:** boot-archive images load in place, while package
  images are captured from RAM npfs, so the packaged rows include that copy.

## Memory (procs MEM, 4 CPUs)

`procs --all --sort mem` reports committed private memory: image, stacks,
MEMORY and JIT regions.

| Process | MEM |
| --- | --- |
| imgui-demo running in Development (9 threads) | 132M |
| Neovim editing a file, UI and embedded server | 11M and 9M |
| shell (`init-shell.pxe`) | 1624K |
| mux | 1824K |
| session | 1672K |

## Sizes

Package archive bytes at this revision, for 110 packages:

| Set | Packages | MiB |
| --- | --- | --- |
| Base set (`packages/base.lua`) | 37 | 48.7 |
| Other public packages | 72 | 514.9 |

| Largest packages | MiB |
| --- | --- |
| mesa-dev | 190.3 |
| lus-demo | 72.8 |
| imgui-demo | 64.6 |
| libultraship-dev | 57.7 |
| nvim | 30.8 |
| libcxx-dev | 16.5 |
| sdl3-dev | 11.4 |

The core ISO is 20,623,360 bytes and the full ISO 38,105,088 bytes.

## CI

These are build-job phases of 115 successful pyxis-os runs, from 2026-10-30
05:34Z to 2026-11-02 08:57Z, with builder `8bdf136`. They were cut with the
2026-10-29 phase method (job log timestamps). Values are medians with ranges, in
seconds.

| Runs | Build job | Ports | SDK | Userland and image |
| --- | --- | --- | --- | --- |
| Push to main, all full builds (29) | 144 (132–212) | 101 (90–157) | 20 (19–40) | about 7 |
| Pull requests building an image (65) | 160 (129–274) | 112 (88–215) | 22 (19–37) | about 7 |
| Documentation-only pull requests (21) | 4 (3–9) | — | — | — |

Every pull request that isn't documentation-only builds everything today, so a
split by change class changes only the counts.

## Limits

- These are nested-KVM QEMU results on one shared host. They establish neither
  native nor owner-host performance.
- The native vela rows are pending a scheduled run.
- The fixture's early return measures launch cost up to `main` plus process
  exit. It says nothing about SDL or GL initialization.
