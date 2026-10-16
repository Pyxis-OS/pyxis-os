uACPI 6.1.1, pinned to commit `fd92d3f3bdd614afda47d08ff5fc373c6d1e4c27`:
https://github.com/uACPI/uACPI/commit/fd92d3f3bdd614afda47d08ff5fc373c6d1e4c27

`LICENSE` retains the complete MIT license. `include/` and `source/*.c` are
copied unmodified, keeping upstream formatting, except for one local change.
Upstream's tests, build-system files (Meson, CMake and `source/files.cmake`)
and README are not imported.

**Local change, `source/interpreter.c` `store_to_reference()`:** a `Store` to
an ArgX that holds `RefOf()` a field unit or buffer field writes the field
instead of replacing the referenced object. ACPICA's
`AcpiExStoreObjectToNode()` always writes fields ("field objects must retain
their original type permanently"), and only overwrites plain data objects.
Upstream master at `2cb5e71e` (2026-10-07) still overwrites. On vela, firmware EC
writes go through `ECWT(Value, RefOf(Field))`, which does `Arg1 = Arg0`, so
without this change no such write reached the EC; UCSI's `_DSM` CONTROL write
and doorbell were lost. Reported upstream as
[uACPI #167](https://github.com/uACPI/uACPI/issues/167); drop this change when
#167 is fixed upstream.

The kernel builds every source file with its normal freestanding flags and
`UACPI_SIZED_FREES`, so the host can account for uACPI's heap use. No other
configuration macro is set: uACPI uses its own default helpers for formatting
and string length, and the kernel's `memcpy`, `memmove`, `memset` and `memcmp`.
The Caelum host interface lives in `kernel/acpi`; see
[ACPI](../../docs/kernel/acpi.md). No host libc is linked.
