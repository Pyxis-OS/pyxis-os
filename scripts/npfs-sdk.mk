# Build the authoritative codecs for target applications. Each consumer
# supplies its own two memory symbols; the archive owns no allocation or I/O.
include $(SDK)/share/pyxis.mk
export SDK CC PYXIS_COMPILER_ID PYXIS_CPPFLAGS PYXIS_CFLAGS

NPFS_OBJECTS := $(addprefix build/npfs-sdk/,$(addsuffix .o,base header record journal))
NPFS_ARCHIVE := build/npfs-sdk/libnpfs-format.a
FAT32_ARCHIVE := build/fat32-sdk/libfat32-read.a
.DEFAULT_GOAL := all

.PHONY: all
all: $(NPFS_ARCHIVE) $(FAT32_ARCHIVE)

$(FAT32_ARCHIVE): $(wildcard fs/fat32/*.c fs/fat32/*.h) fs/include/pyxis_fs/fat32.h fs/Makefile scripts/npfs-sdk.mk $(SDK)/share/pyxis.mk build/npfs-sdk/.config
	$(MAKE) -C fs SOURCE=$(abspath fs) BUILD=$(abspath build/fat32-sdk) \
	  HOST_CC="$(CC)" HOST_AR="$(AR)" CPPFLAGS="$(PYXIS_CPPFLAGS)" CFLAGS="$(PYXIS_CFLAGS)" fat32-read

$(NPFS_ARCHIVE): $(NPFS_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^

build/npfs-sdk/%.o: fs/format/%.c scripts/npfs-sdk.mk $(SDK)/share/pyxis.mk build/npfs-sdk/.config
	@mkdir -p $(@D)
	$(CC) $(PYXIS_CPPFLAGS) $(PYXIS_CFLAGS) -fno-builtin -c $< -o $@

build/npfs-sdk/.config: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' "$$SDK" "$$CC" "$$PYXIS_COMPILER_ID" "$$PYXIS_CPPFLAGS" "$$PYXIS_CFLAGS" > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

.PHONY: FORCE
FORCE:

-include $(NPFS_OBJECTS:.o=.d)
