#ifndef KERNEL_ACPI_TABLES_H
#define KERNEL_ACPI_TABLES_H

#include <abi/acpi_tables.h>
#include <abi/syscall.h>
#include <kernel/service/request.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct file_object;

/* Every table request is answered by the ACPI worker, which alone may read
 * the interpreter's table array. The executor forwards it; the worker runs
 * requests in arrival order at its top level and completes them on the BSP
 * with IF=0. The name buffer is an input for INFO and OPEN and the output
 * for ENUMERATE. */
enum acpi_tables_operation {
  ACPI_TABLES_ENUMERATE,
  ACPI_TABLES_QUERY,
  ACPI_TABLES_OPEN,
};

struct acpi_tables_request {
  struct bsp_request request;
  struct acpi_tables_request *next; /* Worker queue, cleared before completion. */
  enum acpi_tables_operation operation;
  struct directory_cursor cursor; /* ENUMERATE input. */
  size_t capacity; /* ENUMERATE: caller's name buffer size. */
  uint64_t kind; /* OPEN input: FILE or ANY opens; DIRECTORY is WRONG_TYPE. */
  char name[ACPI_TABLE_NAME_MAX + 1];
  struct directory_enumerate_reply entry; /* ENUMERATE output. */
  struct acpi_table_info info; /* QUERY output. */
  struct file_object *file; /* OPEN output: one owned sealed FILE. */
  enum call_status status;
};

/* BSP executor, IF=0. Queues the request for the worker, or completes it at
 * once with UNAVAILABLE when the interpreter never installed its tables. */
void acpi_tables_forward(struct acpi_tables_request *request);

/* ACPI worker only. Runs at most LIMIT queued requests, so one turn of the
 * worker's loop cannot be held by table traffic; the rest stay queued and
 * acpi_tables_pending(), true if any is queued (checked with IF=0), keeps the
 * worker from sleeping until they are served. */
void acpi_tables_run(size_t limit);
bool acpi_tables_pending(void);
/* ACPI worker, after uacpi_initialize() succeeded. */
void acpi_tables_available(void);

#endif
