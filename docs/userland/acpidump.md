# acpidump

`acpidump` writes the firmware's ACPI tables as the kernel's interpreter holds
them, read through the [`acpi://` root](../kernel/acpi.md#table-export). The
root lists one name per installed table; `ls acpi://` and `cp acpi://DSDT home://`
work without a service. A signature occurring once names itself; one occurring
more than once is numbered from 1 in the interpreter's array order (`SSDT1`,
`SSDT2`), which can differ from Linux's numbering. A `/` or nonprintable byte in a
signature shows as `_`, and signatures are counted after that substitution. The RSDP, RSDT and XSDT are
not exported.

```text
acpidump               # Linux acpidump text, which acpixtract reads
acpidump -s            # one summary line per table
acpidump -b tmp://acpi # one raw file per name in an existing directory
```

The text form prints `SIGN @ 0xADDRESS` per table, then rows of sixteen hex
bytes with their printable ASCII, then an empty line. The address is the
physical address of a table the firmware published in memory and zero for a
table AML loaded later; it is informational. Bytes are returned exactly as
published, including a bad checksum. `-s` shows the name, signature, length,
address, the interpreter's checksum state (`ok`, `bad` or `unchecked`) and the
`runtime-loaded` and `withheld` flags. `-b` creates each file exclusively and
reports an existing name without replacing it.

`MSDM`, which carries the OEM product key, is listed with its length and the
withheld flag; opening it is denied and the dump skips it with one line on
standard error naming the withheld tables. `ls -l acpi://` reports that
entry as denied with an unknown size. A failed read, write or listing exits
nonzero.

The root is bound only in spaces whose boot configuration sets
[`acpi_tables = true`](init.md#boot-configuration): live Development and Remote.
It travels through session, remote and shell handoffs like any configured root,
so every command in such a shell can read the tables. On the unauthenticated
Remote terminal this is the same accepted exposure as screen capture.

## Pulling the tables to the host

Over the [remote terminal](remote-terminal.md#client-modes) in machine mode,
one host command collects the text dump, and `acpixtract` turns it into raw
tables for `iasl -d`:

```sh
printf 'acpidump\nexit\n' | build/tools/pyxis-remote --machine --no-shell-echo \
    --columns 200 --rows 50 HOST 2323 \
  | jq -r 'select(.type == "output") | .base64' | base64 -d > acpidump.txt
acpixtract -a acpidump.txt   # dsdt.dat, ssdt1.dat, ...
iasl -d dsdt.dat
```

The dump is the program's own output; shell diagnostics, including the withheld
line, go to the same terminal but `acpixtract` ignores lines that are not table
rows. Raw files written with `-b` can also be fetched one at a time with
`xfer send` from a client started with `--download-dir`.
