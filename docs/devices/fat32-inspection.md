# Read-only FAT32 inspection

Build the live image and choose **Inspect FAT32 (read-only)** in Limine. This is
an inspection boot profile, separate from Install/Update. In its console tab,
select the displayed disk ID and one-based GPT entry explicitly. Keep that media
quiescent until the inspector exits; read-only opening provides no snapshot or
exclusion against another writer. Do not inspect a volume concurrently changing
on another system or through another Pyxis handle.

At `fat32>` use `info`, `check`, `list /PATH`, `read /PATH` or `quit`. Paths are
ASCII, rooted at `/`, with literal spaces and no quoting or shell expansion.
Names preserve UTF-8; supported ASCII aliases can address Unicode long names.
File reads emit their original bytes, including terminal controls. Use only
known text files at the interactive console. A successful read/list checks its
used metadata/chains; use `check` for whole-volume reachability and allocation
validation. Failures are explicit; nothing is repaired, mounted or written.

The trusted bootstrap uses the existing install-only disk inventory authority,
opens the selected disk read-only and attenuates the child disk grant to INFO|READ.
The inspector receives memory and console streams, with no DISKS service, MOUNT,
write or launcher authority, roots, namespace or cwd. GPT admission reads disk
metadata; subsequent engine/adapter reads stay inside the chosen partition.
The raw disk capability itself is a whole-device read handle, not a new
partition capability. Ordinary boot grants remain unchanged.

The shared [reader contract](../../fs/docs/fat32-read.md) defines the supported
GPT/FAT32 profile and validation. Matching device/FAT logical sectors are 512 or 4096 bytes; clusters are
powers of two up to 128 sectors; one FAT or two mirrored FATs are supported.
Legacy MBR, bare FAT, FAT12/16, exFAT and nonmirrored active-FAT layouts refuse.
The inspector caps workspace at 32 MiB, directory depth at 64 and each operation
at 67,108,864 work steps and 16,777,216 backing sectors. Native requests are at
most 4 KiB. These are inspector policies, not promises that every valid large
volume completes within them. Exceeding a bound fails without weaker validation.

For Linux images or explicitly selected whole-disk devices, use the separate
[host inspector](../../fs/docs/fat32-inspect.md). Its output can be redirected
and its workspace/work budgets configured. Neither inspector's refusal permits
an ESP rebuild: the installer's existing lenient recovery reader and foreign-file
safeguards remain separate and unchanged. [FAT32 volumes](fat32-volumes.md) also
provide the separate [scratch writer](fat32-scratch-writing.md) and
[native RO/RW mounts](fat32-mounts.md).
