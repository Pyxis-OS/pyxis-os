# The shared library owns only format codecs. Cache, allocation and mutation
# policy stay in Caelum's npfs_store implementation.
NPFS_SOURCES := $(addprefix fs/format/,$(addsuffix .c,base header record journal))
NPFS_OBJECTS := $(patsubst %.c,build/%.o,$(NPFS_SOURCES))
OBJECTS += $(NPFS_OBJECTS)

build/kernel/fs/npfs.o: build/kernel-config.h

$(NPFS_OBJECTS): CPPFLAGS += -Ifs/include -Ifs/format
$(NPFS_OBJECTS): CFLAGS += -fno-builtin -nostdinc -isystem $(shell $(CC) -print-file-name=include)

$(NPFS_SOURCES):
	@test -f $@ || { \
	  echo 'Missing npfs format source $@: run git submodule update --init fs.' >&2; \
	  exit 1; }

$(filter build/kernel/fs/npfs%.o,$(OBJECTS)) build/kernel/init.o build/kernel/service/request.o \
build/kernel/object/file.o build/kernel/object/directory.o \
build/kernel/object/launcher.o build/kernel/object/mount.o build/kernel/object/disk.o \
build/kernel/storage/disk_access.o \
build/kernel/user/boot.o build/kernel/user/launch.o build/kernel/syscall.o \
build/kernel/acpi/power.o: CPPFLAGS += -Ifs/include
