uACPI master after 6.1.1 (`6.1.1-187-g72f6fdc`, 2026-10-16), pinned to commit
`72f6fdc597d6dececdf8e5a148132a990adc2ebf`:
https://github.com/uACPI/uACPI/commit/72f6fdc597d6dececdf8e5a148132a990adc2ebf
It includes the fix for stores through ArgX references to fields
([uACPI #167](https://github.com/uACPI/uACPI/issues/167), merged as
[PR #169](https://github.com/uACPI/uACPI/pull/169)).

`LICENSE` retains the complete MIT license. `include/` and `source/*.c` are
copied unmodified, keeping upstream formatting. There are no local changes.
Upstream's tests, build-system files (Meson, CMake and `source/files.cmake`)
and README are not imported.

The kernel builds every source file with its normal freestanding flags and
`UACPI_SIZED_FREES`, so the host can account for uACPI's heap use. No other
configuration macro is set: uACPI uses its own default helpers for formatting
and string length, and the kernel's `memcpy`, `memmove`, `memset` and `memcmp`.
The Caelum host interface lives in `kernel/acpi`; see
[ACPI](../../docs/kernel/acpi.md). No host libc is linked.
