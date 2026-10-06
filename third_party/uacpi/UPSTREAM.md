uACPI 6.1.1, pinned to commit `fd92d3f3bdd614afda47d08ff5fc373c6d1e4c27`:
https://github.com/uACPI/uACPI/commit/fd92d3f3bdd614afda47d08ff5fc373c6d1e4c27

`LICENSE` retains the complete MIT license. `include/` and `source/*.c` are
copied unmodified, keeping upstream formatting. Upstream's tests, build-system
files (Meson, CMake and `source/files.cmake`) and README are not imported.

The kernel builds every source file with its normal freestanding flags and
`UACPI_SIZED_FREES`, so the host can account for uACPI's heap use. No other
configuration macro is set: uACPI uses its own default helpers for formatting
and string length, and the kernel's `memcpy`, `memmove`, `memset` and `memcmp`.
The Caelum host interface lives in `kernel/acpi`; see
[ACPI](../../docs/kernel/acpi.md). No host libc is linked.
