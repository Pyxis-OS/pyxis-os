# FAT32 scratch writing

Choose **Write scratch FAT32** in the live image's boot menu, then explicitly
select a disposable disk ID and one-based GPT entry. This trusted profile is
separate from the read-only inspector and installer. It does not mount, format,
repair or adopt existing files. Initial qualification covers disposable VirtIO
media; USB needs transport qualification. Native ESP writing is not qualified.

The selector opens the disk read-write, releases the initial whole-disk claim,
claims the selected GPT partition and delegates INFO|READ|WRITE|RELEASE plus
memory and console streams. The writer revalidates GPT and FAT32 under that
claim. Its backing callbacks confine filesystem I/O to the selected extent.
It receives no disk inventory service, mount right, launcher or directory roots.
This is trusted code: the existing WRITE right also permits CLAIM; it is not a
permanently attenuated partition capability. Ordinary boot grants are unchanged.

At `fat32-write>` use `mkdir /PATH`, `create /PATH = TEXT`,
`append RECEIPT = TEXT`, `extend RECEIPT BYTES` or `replace RECEIPT = TEXT`.
Paths and inline text use the bounded ASCII profile, with literal spaces and no
quoting or shell expansion. `receipts` lists the session's creation receipts.
`info`, `check`, `list /PATH`, `read /PATH` and `quit` provide inspection and exit.
File reads emit original bytes, including terminal controls; read only known text
at the console. Receipts authorize only objects created successfully in this
session. Restarting loses those update permissions; names never imply ownership.

The native caller caps files at 4 KiB, receipts at 32, each receipt's recorded
chain at eight clusters, commands at 4 KiB, workspace at 32 MiB and directory
depth at 64. Each operation has 67,108,864 work steps and 16,777,216 backing
sectors. These are caller policies, not FAT32 format limits. Namespace creation
requires supported ASCII names and aliases for all siblings in the target
directory. A valid unsupported namespace refuses rather than guessing a conflict.

Every mutation preflights whole-volume integrity and its finite preparation
budgets before writing. Growth zero-fills; replacement prepares a separate chain.
Success means the operation's final flush completed. Data, mirrored FAT,
directory publication and FSInfo changes follow the
[shared writer contract](../../fs/docs/fat32-write.md).
Dirty flags are preserved. This does not promise crash atomicity, untorn sectors
or rollback after uncertain effects.

An uncertain mutation permanently stops this writer session. Recovery details
and prepared backing remain available while it stays open; subsequent commands
cannot reuse them. There is no automatic replay, repair or staging cleanup.
Capture needed information before quitting: the session does not persist it.
Explicit RELEASE ends the claim even if flushing fails; the writer reports that
failure and unconfirmed durability. The device's existing write-failed latch
continues to refuse mutation until reboot. Closing or process retirement also
releases a retained claim, without converting a failed operation into success.

[FAT32 volumes](fat32-volumes.md) also provide mounted read-write operations and
declared editor grants. This raw development profile retains its separate receipt
ownership and dirty-flag policy.
