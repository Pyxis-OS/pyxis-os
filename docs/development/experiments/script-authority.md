# Shell script authority qualification

## Build and fixture

Userland before `4f0c13ca`, after `797f95c3`; parent source base `aa4f4a804`.
Published builder `pyxis-llvm23.1.3-8a4d001`; rebuilt SDK C runtime/libpyxis/libterm
and shell. The baseline shell was rebuilt from the recorded before source.
Other guest programs and the optional-JIT/no-JIT Mesa triangle bundles were held
unchanged. Baseline and changed shells replaced all three shell locations
(`shell.pxe`, `init-shell.pxe`, `shell.pxb/app/bin/shell.pxe`); the changed bundle
manifest adds optional JIT.

QEMU/KVM q35, host CPU plus invtsc, four vCPUs, 4 GiB RAM, OVMF, VirtIO network,
RNG and display, no persistent disk. Both use the same frozen JIT-capable kernel
artifact built at `ddf2291dd78d` (SHA-256
`aba67c88916380687c1a33cfc5483292f2782ac66349eb5a40a70b9e3519b758`).
The fixture explicitly enables `jit = true` in its Remote space, then repeats
with that setting false. This isolates the userspace change; it is not a new
full current-main image or native-hardware qualification.

## Results

The script body launches `boot://mesa-jit.pxb home://script.ppm`. Before the
change, direct launch selected llvmpipe but the script reported JIT unavailable
and selected softpipe.

After the change, direct, nested, pipeline, background and captured script
launches select llvmpipe with JIT enabled, and softpipe with JIT disabled.
`boot://shell.pxb -c` launching the script also passes both cases. Background
verification reads the output produced by a foreground command inside the
background script; background redirects themselves remain unsupported.
A shebang using an exported root alias to the same held interpreter FILE passes.
A bundle omitting JIT still selects softpipe in the enabled space. Explicitly
forcing llvmpipe without JIT fails instead of silently gaining authority.
These checks validate renderer selection and completed triangle rendering,
not just the existence of a grant. Remote final drain/exit completed after each run.

An extra `shell.pxb -c "session ...pxs"` check returns the same launcher-boundary
refusal before and after. Existing rootless SESSION re-entry remains outside this
qualification; no fix or new success is claimed. Ordinary SESSION grant assembly
and lifetime were retained separately from script source inheritance. No injected
allocation/admission failure, teardown leak proof or native check was performed.

## Matched nested costs

Existing Remote framing/command completion supplied host elapsed times, including
transport and launch. A/B/A cohorts use the same fixture and flags, with one
warm-up and five measured samples per workload/cohort. No concurrent owned guest
or build ran during this retained comparison. Earlier potentially overlapping or
unmatched samples are excluded.

| Workload | A median [min, max], ms | B median [min, max], ms | A repeat median [min, max], ms |
| --- | ---: | ---: | ---: |
| Native `echo BASELINE` | 3.396 [3.289, 3.534] | 3.444 [3.301, 4.675] | 3.326 [3.247, 3.485] |
| `.pxs` containing that echo | 5.178 [5.093, 5.515] | 5.186 [5.124, 5.238] | 5.197 [5.170, 5.447] |
| Shell bundle `-c` echo | 6.768 [6.644, 7.088] | 6.486 [6.425, 6.661] | 6.558 [6.389, 6.741] |

The script interval is unchanged within these samples; do not infer a general
speedup or native launch cost. Constructing the inventory does not add source
lookups to ordinary native launches. Existing optional job provisioning retains
its omission-on-unavailable contract.

Small raw results, commands and executable hashes are retained under
`/shared/present/script-authority/qualification/`; working roots, SDKs and images
stay on the VM local disk. The [shell reference](../../userland/shell.md#script-runtime-grants)
defines the implemented behaviour; [Lua inheritance](../../userland/script-authority.md)
is recorded separately with its qualification.
