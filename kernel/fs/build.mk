# The shared library owns only format codecs. Cache, allocation and mutation
# policy stay in Caelum's native_store implementation.
PNF_SOURCES := $(addprefix fs/format/,$(addsuffix .c,base header record journal))
PNF_OBJECTS := $(patsubst %.c,build/%.o,$(PNF_SOURCES))
OBJECTS += $(PNF_OBJECTS)

build/kernel/fs/native.o: build/kernel-config.h

$(PNF_OBJECTS): CPPFLAGS += -Ifs/include -Ifs/format
$(PNF_OBJECTS): CFLAGS += -fno-builtin -nostdinc -isystem $(shell $(CC) -print-file-name=include)

$(PNF_SOURCES):
	@test -f $@ || { \
	  echo 'Missing native format source $@: run git submodule update --init fs.' >&2; \
	  exit 1; }

$(filter build/kernel/fs/native%.o,$(OBJECTS)) build/kernel/init.o build/kernel/service/request.o \
build/kernel/object/file.o build/kernel/object/directory.o \
build/kernel/object/launcher.o build/kernel/object/mount.o \
build/kernel/user/boot.o build/kernel/user/launch.o build/kernel/syscall.o: CPPFLAGS += -Ifs/include
