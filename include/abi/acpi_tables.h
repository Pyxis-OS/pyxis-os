#ifndef ABI_ACPI_TABLES_H
#define ABI_ACPI_TABLES_H

#include <abi/directory.h>

/* The acpi_tables resource is a read-only PROTOCOL_DIRECTORY root over the
 * ACPI interpreter's installed-table array. ENUMERATE lists one name per
 * table; LOOKUP of a name with kind FILE returns that table's bytes, exactly
 * as published, as an immutable READ-only FILE copied when the lookup runs.
 * Mutation operations return READ_ONLY. The RSDP, RSDT and XSDT are not in
 * the array and have no name.
 *
 * A signature occurring once is its own name (DSDT, FACP). A signature
 * occurring more than once is numbered from 1 in array order (SSDT1..SSDTn).
 * Names are recomputed from the current array, so a table that AML installs
 * later can renumber its signature; the enumeration cursor then reports
 * CHANGED. Numbering may differ from another operating system's. */
#define ACPI_TABLES_RIGHTS (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | \
                            DIRECTORY_RIGHT_READ_FILES)

/* Longest name: a four-byte signature and a decimal index. */
#define ACPI_TABLE_NAME_MAX 24u

/* A PROTOCOL_DIRECTORY operation accepted only by the acpi_tables root; other
 * directories return BAD_OPERATION. Requires ENUMERATE. The request is a
 * complete directory_message whose body holds this record, zero-padded. */
#define ACPI_TABLES_INFO UINT64_C(32)

struct acpi_table_info_request {
  uint64_t name;
  uint64_t name_length; /* Bytes excluding NUL; one table name. */
};

#define ACPI_TABLE_CHECKSUM_CHECKED (UINT64_C(1) << 0)
#define ACPI_TABLE_CHECKSUM_BAD (UINT64_C(1) << 1)
/* Listed with its length, but LOOKUP returns DENIED: the bytes stay in the kernel. */
#define ACPI_TABLE_WITHHELD (UINT64_C(1) << 2)
/* Installed by AML Load/LoadTable after boot rather than found through the RSDP. */
#define ACPI_TABLE_RUNTIME_LOADED (UINT64_C(1) << 3)

/* The checksum state is the interpreter's: CHECKED set with BAD clear means it
 * was verified; CHECKED clear means it was never computed. physical_address is
 * informational and zero unless the firmware published the table in memory. */
struct acpi_table_info {
  char signature[4];
  uint32_t reserved;
  uint64_t length;
  uint64_t physical_address;
  uint64_t flags;
};

_Static_assert(sizeof(struct acpi_table_info_request) == 16, "ACPI table info request layout");
_Static_assert(sizeof(struct acpi_table_info_request) <= sizeof(union directory_payload),
    "ACPI table info request fits the directory message");
_Static_assert(sizeof(struct acpi_table_info) == 32, "ACPI table info layout");

#endif
