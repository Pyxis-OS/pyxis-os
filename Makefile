CROSS_COMPILE ?= x86_64-elf-
CC := $(CROSS_COMPILE)gcc
QEMU ?= qemu-system-x86_64
QEMU_DISPLAY ?= gtk
MEMORY ?= 256M
CPUS ?= 1
ACCEL ?= kvm
INIT ?= userspace/init.sh
export INIT

LOG_LEVEL ?= info
ifeq ($(LOG_LEVEL),trace)
TRACE_ENABLED := 1
else ifeq ($(LOG_LEVEL),info)
TRACE_ENABLED := 0
else
$(error LOG_LEVEL must be info or trace)
endif
OVMF_CODE ?= /usr/share/OVMF/x64/OVMF_CODE.4m.fd
OVMF_VARS ?= /usr/share/OVMF/x64/OVMF_VARS.4m.fd

CPPFLAGS := -Ibuild -Iinclude -Iarch/x86_64/include -Ithird_party/limine -Ithird_party/tlsf
CFLAGS := -std=gnu23 -O2 -g3 -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=kernel \
          -fno-omit-frame-pointer -Wall -Wextra -Wshadow -Wstrict-prototypes \
          -Wmissing-prototypes -MMD -MP
LDFLAGS := -nostdlib -static -no-pie -Wl,-T,arch/x86_64/linker.ld \
           -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -Wl,-Map,build/caelum.map

C_SOURCES := $(wildcard boot/limine/*.c arch/x86_64/*.c kernel/*.c kernel/user/*.c kernel/object/*.c kernel/fs/*.c kernel/mm/*.c kernel/fb/*.c lib/*.c) \
             third_party/tlsf/tlsf.c
ASM_SOURCES := $(wildcard boot/limine/*.S arch/x86_64/*.S)
OBJECTS := $(patsubst %.c,build/%.o,$(C_SOURCES)) $(patsubst %.S,build/%.o,$(ASM_SOURCES))

.PHONY: all tools userspace initrd image run debug clean check-toolchain FORCE
all: build/caelum.elf

tools:
	$(MAKE) -C tools

userspace: tools
	$(MAKE) -C userspace

initrd: build/initrd.cpio

# Always stage the selected contents, even when INIT changes to an older file.
# Package after the recursive build; make may have cached its outputs' mtimes.
# Compare before replacing so identical contents do not rebuild the ISO.
build/initrd.cpio: userspace Makefile
	@command -v cpio >/dev/null 2>&1 || { \
	  echo 'Missing GNU cpio: install it, then run make initrd.' >&2; \
	  exit 1; }
	@test -f "$$INIT" || { printf 'Init is not a regular file: %s\n' "$$INIT" >&2; exit 1; }
	cp -- "$$INIT" build/userspace/init.tmp
	@cmp -s build/userspace/init.tmp build/userspace/init || mv build/userspace/init.tmp build/userspace/init
	@rm -f build/userspace/init.tmp
	cd build/userspace && printf '%s\n' init shell.pxe cat.pxe ls.pxe mkdir.pxe share share/hello.txt | \
	  cpio --create --format=newc --reproducible --owner=0:0 --quiet > ../initrd.cpio.tmp
	@cmp -s build/initrd.cpio.tmp $@ || mv build/initrd.cpio.tmp $@
	@rm -f build/initrd.cpio.tmp

check-toolchain:
	@command -v $(CC) >/dev/null 2>&1 || { \
	  echo "Missing $(CC): add the cross-toolchain to PATH or set CROSS_COMPILE." >&2; \
	  exit 1; }

build/caelum.elf: $(OBJECTS) arch/x86_64/linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

# Preserve the header timestamp unless the selected level actually changes.
# Compiler flags alone do not make existing objects out of date.
FORCE:

build/kernel-log-config.h: FORCE
	@mkdir -p $(@D)
	@printf '#define KLOG_TRACE_ENABLED %s\n' $(TRACE_ENABLED) > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

build/%.o: %.c build/kernel-log-config.h | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/%.o: %.S | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

image: build/pyxis.iso

build/pyxis.iso: build/caelum.elf build/initrd.cpio \
                 boot/limine/limine.conf scripts/make-image.sh \
                 third_party/limine/BOOTX64.EFI third_party/limine/limine-uefi-cd.bin
	./scripts/make-image.sh

run debug: image
	QEMU="$(QEMU)" QEMU_DISPLAY="$(QEMU_DISPLAY)" MEMORY="$(MEMORY)" CPUS="$(CPUS)" ACCEL="$(ACCEL)" \
	OVMF_CODE="$(OVMF_CODE)" OVMF_VARS="$(OVMF_VARS)" ./scripts/run-qemu.sh $@

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
