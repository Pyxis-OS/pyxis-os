# ThinkPad check for SMP task 4a

An owner-run native check before #417 merges. It compares this PR with current
main on the ThinkPad (12 threads, so 11 workload CPUs after the BSP). It
answers two questions:

- Do compute-bound programs now run in parallel?
- How do pipes behave when their ends land on different CPUs (the open
  question in the [4a record](README.md#pipe-and-endpoint-ipc-4-cpus))?

## What is staged

Everything is in `/shared/pxe/boot`, the PXE TFTP root. The two new menu entries
are first, so the check entry boots after the 5-second timeout. The previous menu is saved as
`limine.conf.before-smp-4a`; your earlier entries and files are unchanged.

| Menu entry | Files | Source | SHA-256 (`caelum.elf` / `initrd.cpio`) |
| --- | --- | --- | --- |
| SMP 4a check (PR #417) | `smp-4a/check/` | branch `spaces/placement-balancing` at `c490dbd` | `21821ccb…cfbeb6` / `efd16325…1455e3e5e` |
| SMP 4a baseline (main) | `smp-4a/main/` | main `43f9415` (same code as `d30e678`) | `b9bed45a…238cd17f` / `c767df9b…c6dd9b` |

Both are ordinary `make -j16 image` builds with the repository's network and
session configuration (built-in port, DHCP). Both entries use the same command
line, which creates these tabs in order:

| Tab | Space | Shown title | Purpose |
| ---: | --- | --- | --- |
| 1 | Caelum | Caelum | kernel log |
| 2 | development | Development | network setup, local shell |
| 3 | remote | Remote | remote terminal server on port 2323 |
| 4–6 | pipe-a, pipe-b, pipe-c | Read-only | pipe runs, CPUs chosen by the scheduler |
| 7–8 | pinned-a, pinned-b | Read-only | pipe runs, limited to CPU 1 and CPU 2 |

On main, every space is pinned to one CPU anyway. On the check build, tabs 7–8
reproduce main's same-CPU pipe behaviour on the new kernel.

## Run it, once per menu entry

Run everything first on **SMP 4a check**, then reboot into **SMP 4a baseline (main)**
and repeat the same steps.

1. **Wait for the network.** Wait for `net0: DHCP 192.168.0.50/24 …` in the
   Development tab (Super+Right once from Caelum).

2. **Open a remote terminal** from the desktop. Build the client with
   `make -C tools remote` if needed:

   ```sh
   build/tools/pyxis-remote 192.168.0.50 2323
   ```

3. **Single client.** In the remote shell, run one client and note its `Elapsed`
   line:

   ```text
   allocbench heap --rounds 262144
   ```

4. **Four concurrent clients.** Don't paste several lines into the remote
   shell. Typing competes with the jobs, and output interleaves with input. From
   your Pyxis checkout on the desktop, start them together over one remote
   connection instead:

   ```sh
   /shared/pxe/boot/smp-4a/par-heap.sh build/tools/pyxis-remote 4
   ```

   The script, staged next to the builds and not part of the repository, sends
   four `allocbench heap --rounds 262144 &` lines at once. It waits, exits the
   remote shell and prints each client's `Elapsed` line plus a count. It needs
   `python3` on the desktop.

5. **Eight concurrent clients.** Run the same script with `8` instead of `4`.

6. **Pipes.** On the ThinkPad itself, press Super+Right to reach tab 4. Type:

   ```text
   session app://iobench.pxe pipe --buffer 4096
   ```

   Note its two summary lines (`pipe acceptance summary` and
   `pipe completion summary`). Each run hands off that tab's shell, so every
   tab runs once.

7. **More pipe runs.** Repeat step 6 in tabs 5, 6, 7 and 8.

## Expected

| Step | Check build | Main |
| --- | --- | --- |
| 4–5: concurrent `Elapsed` | each close to step 3 | each about 4× or 8× step 3, all clients sharing one CPU |
| 6–7: tabs 4–6 | unknown natively, which is the point | close to tabs 7–8 |
| 6–7: tabs 7–8 | close to main | same-CPU pipe |

In the nested VM, tabs 4–6 on the check build were bimodal. Some passes were
faster than same-CPU pipes and some were 3–4× slower. If the ThinkPad shows tabs 4–6 clearly slower
than tabs 7–8, the proposed fix is to start new tasks on their parent's CPU
unless it is two or more tasks heavier.

Before reporting, confirm which kernel ran. The check build logs
`userspace: space development: app://init entry=0x…` in the Caelum tab with no
CPU number. Main's line ends with `, CPU 1`.

## Report back

For each build:

- the step 3 `Elapsed`;
- the four and eight `Elapsed` values from steps 4–5;
- the acceptance and completion summary lines from tabs 4–8.

Also mention any hang, fault or missing output.

## Afterwards

To restore the previous menu:

```sh
cp /shared/pxe/boot/limine.conf.before-smp-4a /shared/pxe/boot/limine.conf
```

`smp-4a/` can then be deleted.

A QEMU dry run of these exact steps on the staged binaries used 12 vCPUs. All
eight tabs showed and both command types worked, and four background clients each matched one client.
Its timings are not meaningful: 12 guest vCPUs ran on an 8-vCPU nested host.
