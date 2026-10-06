# Pinned uACPI interpreter and its Caelum host interface; no host include paths.
UACPI_SOURCES := $(wildcard third_party/uacpi/source/*.c)
ACPI_OBJECTS := $(patsubst %.c,build/%.o,$(UACPI_SOURCES) $(wildcard kernel/acpi/*.c))

OBJECTS += $(ACPI_OBJECTS)

# Sized frees let the host account for exactly what uACPI holds.
$(ACPI_OBJECTS): CPPFLAGS += -Ithird_party/uacpi/include -DUACPI_SIZED_FREES
# Compiler freestanding headers only: accidental host headers must fail here.
$(ACPI_OBJECTS): CFLAGS += -nostdinc -isystem $(shell $(CC) -print-file-name=include)
