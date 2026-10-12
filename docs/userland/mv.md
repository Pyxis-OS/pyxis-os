# Moving files and directories

`mv [--] SOURCE... DESTINATION` uses libc/native rename. RAM and writable npfs
support files and directory trees within one store/mount instance. FAT32 and
host backing support files only. There are no options besides `--`.

```text
mv home://draft.txt home://final.txt
mv home://drafts home://archive
mv home://a home://b home://archive
mv -- -odd-name home://
```

If the destination is an existing directory, each source moves into it under
its own final component. Otherwise two operands select the exact new name;
several sources require a destination directory. Replacement requires matching
kinds, and directory victims must be empty. A trailing source slash requires a
directory. Roots and final `.`/`..` cannot be moved; moving a tree into itself or
its descendant refuses. Existing handles and delegated roots keep identity and
rights. Existing cwd chains remain usable, with potentially stale display text.

Sources are attempted in order; a failure does not stop later sources. Any
failure makes the exit status nonzero and reports the actual libc error. There
is no recursive copy/delete fallback across stores or mounts. Cross-store and
FAT32/host directory moves report “Operation not supported”; nonempty victims
report ENOTEMPTY. Read-only/insufficient authority refuses. An uncertain storage
failure does not promise rollback or justify retry.

The destination-directory test runs once before the first move and needs LOOKUP;
its result is not a whole-path snapshot. Native publication still rechecks
kinds, replacement authority and cycles under the mutation owner. See
[filesystem mutations](../interfaces/filesystem-mutations.md) and
[held working paths](process-state.md).

## Qualification — 2026-10-12

Measured code: Pyxis `cf7312f0`, userland `4f9d678` (delivery `9f781e9`
adds a comment correction), fs `414be0ac`, ports `f93becb9`. Ordinary
`make -j16 image LOG_LEVEL=info` passed using the published LLVM 23.1.3 builder;
all directory ABI consumers were rebuilt. Kernel SHA-256
`963633b70836bf213a6c02b487ba75ff279d6789b8680b373029ab8c7ae9afde`;
normal initrd `256a4edefc50ac123701d2f23a6ce6f8b6a8bfefa152c05146053651818c1558`.

Manual QEMU 10.2.2, Q35/nested KVM, host CPU, 512 MiB, one/four CPUs,
fresh matching OVMF, modern VirtIO block/RNG, PS/2 and software GOP 1280x800.
No NIC, USB or physical media. The unchanged baseline (`8d8a1e39`, userland
`5af803ef`) reproduced `mv`'s directory refusal. Ignored local native callers
in a qualification archive passed 44 RAM checks on four CPUs and 45 npfs checks
on each of four/one CPUs: same/cross-parent moves, same-kind empty replacement,
held file/directory identity, unchanged moved enumeration, detached-victim
creation refusal, attenuated root lookup/refusal, retained cwd and `..`, cycles,
nonempty/type/no-replace refusals, roots and trailing separators. Cross-store
moves refused; `mv` moved multiple directory sources and refused cross-volume
and descendant targets. Ordinary file staging/copy/replacement remained usable.

A private 128 MiB npfs pool with two volumes in a 160 MiB GPT image was synced,
QEMU stopped, and its detached partition passed `fsck.npfs`. Reboot/reopen on one
CPU read both moved/replaced trees correctly. Read-only GDB inspection confirmed
worker-owned same-volume parents during the normal cross-parent transaction.
No fixtures, captures, raw logs or test framework are committed. FAT32/host
refusal and RAM parent-teardown locking were inspected, not hardware-qualified;
no crash, corruption, resource-exhaustion or uncertain-I/O injection was run.
