# Pinned read-only core and mode/health bridge; the publisher, planners,
# construction, checking and host adapters stay out.
PFS_CORE := base block record tree platform pool access writer_access
PFS_SOURCES := $(addprefix fs/core/,$(addsuffix .c,$(PFS_CORE)))
PFS_OBJECTS := $(patsubst %.c,build/%.o,$(PFS_SOURCES))
PFS_INCLUDES := -Ifs/include -Ifs/core

OBJECTS += $(PFS_OBJECTS)

$(PFS_OBJECTS): CPPFLAGS += $(PFS_INCLUDES)
# Compiler freestanding headers only: accidental host headers must fail here.
$(PFS_OBJECTS): CFLAGS += -fno-builtin -nostdinc -isystem $(shell $(CC) -print-file-name=include)

$(PFS_SOURCES):
	@test -f $@ || { \
	  echo 'Missing filesystem core source $@: run git submodule update --init fs.' >&2; \
	  exit 1; }

build/kernel/fs/native.o build/kernel/init.o build/kernel/service/request.o \
build/kernel/object/file.o build/kernel/object/directory.o \
build/kernel/object/launcher.o build/kernel/object/mount.o \
build/kernel/user/boot.o build/kernel/user/launch.o build/kernel/syscall.o: CPPFLAGS += -Ifs/include
