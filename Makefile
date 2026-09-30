CROSS_COMPILE ?= x86_64-unknown-pyxis-
CC := $(CROSS_COMPILE)gcc
HOSTCC ?= cc
HOSTAR ?= ar
LUA ?= lua
export CROSS_COMPILE HOSTCC LUA
QEMU ?= qemu-system-x86_64
QEMU_DISPLAY ?= gtk
MEMORY ?= 256M
CPUS ?= 1
ACCEL ?= kvm
VIRTIO_FS_SOCKET ?=
VIRTIO_NET ?= 0
VIRTIO_RNG ?= 1
VIRTIO_BLK_IMAGE ?=
VIRTIO_BLK_READONLY ?= 0
TCP_FORWARD ?=
UDP_FORWARD ?=
export VIRTIO_FS_SOCKET VIRTIO_NET TCP_FORWARD UDP_FORWARD VIRTIO_RNG VIRTIO_BLK_IMAGE VIRTIO_BLK_READONLY
INIT ?=
INIT_DEFAULT ?= app://init-idle
INIT_PRIMARY ?= app://init
INIT_CPUS ?= 2=app://init-readonly 3=app://init-remote
MOUNT_DISK ?=
MOUNT_PRINCIPAL ?=
export INIT_DEFAULT INIT_PRIMARY INIT_CPUS MOUNT_DISK MOUNT_PRINCIPAL
# Space-separated components already extracted from bundles at the repo root.
PREBUILT ?=
ifneq ($(filter-out kernel sdk userspace ports,$(PREBUILT)),)
$(error PREBUILT accepts kernel sdk userspace ports)
endif
DOOM_WAD ?=
DOOM_DEMOS ?=
export INIT DOOM_WAD DOOM_DEMOS

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

C_SOURCES := $(wildcard boot/limine/*.c arch/x86_64/*.c kernel/*.c kernel/user/*.c kernel/object/*.c kernel/service/*.c kernel/fs/*.c kernel/mm/*.c kernel/fb/*.c kernel/pci/*.c kernel/virtio/*.c kernel/storage/*.c kernel/net/*.c lib/*.c) \
             third_party/tlsf/tlsf.c
ASM_SOURCES := $(wildcard boot/limine/*.S arch/x86_64/*.S)
OBJECTS := $(patsubst %.c,build/%.o,$(C_SOURCES)) $(patsubst %.S,build/%.o,$(ASM_SOURCES))

.DEFAULT_GOAL := all
include kernel/net/lwip/build.mk
include kernel/fs/build.mk

.PHONY: all kernel bundle-kernel bundle-sdk bundle-userspace bundle-ports image-inputs tools fs-tools sdk sdk-headers userspace ports initrd image run debug clean check-toolchain FORCE
all: kernel

ifneq ($(filter kernel,$(PREBUILT)),)
kernel:
	./scripts/bundle.sh verify kernel
else
kernel: build/caelum.elf
	CC="$(CC)" LOG_LEVEL="$(LOG_LEVEL)" CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" ./scripts/bundle.sh record kernel
endif

tools:
	$(MAKE) -C tools

fs-tools:
	@test -f fs/Makefile || { \
	  echo 'Missing filesystem submodule: run git submodule update --init fs.' >&2; \
	  exit 1; }
	$(MAKE) -C fs SOURCE=$(abspath fs) BUILD=$(abspath build/fs-tools) HOST_CC="$(HOSTCC)" HOST_AR="$(HOSTAR)"

sdk-headers:
	@test -f userspace/runtime.mk || { \
	  echo 'Missing userspace submodule: run git submodule update --init userspace.' >&2; \
	  exit 1; }
	./scripts/export-sdk.sh headers

ifneq ($(filter sdk,$(PREBUILT)),)
sdk:
	./scripts/bundle.sh verify sdk
else
sdk: tools sdk-headers
	$(MAKE) -C userspace -f runtime.mk SDK=$(abspath build/sdk) BUILD=$(abspath build/runtime)
	./scripts/export-sdk.sh complete
	./scripts/bundle.sh record sdk
endif

ifneq ($(filter userspace,$(PREBUILT)),)
userspace: ports
	./scripts/bundle.sh verify userspace
else
userspace: ports
	$(MAKE) -C userspace SDK=$(abspath build/sdk) LUA_PREFIX=$(abspath build/ports-dev/lua) PICOHTTPPARSER_PREFIX=$(abspath build/ports-dev/picohttpparser) MBEDTLS_PREFIX=$(abspath build/ports-dev/mbedtls) BUILD=$(abspath build/userspace) install DESTDIR=$(abspath build/userspace-root)
	./scripts/bundle.sh record userspace
endif

ifneq ($(filter ports,$(PREBUILT)),)
ports: sdk
	./scripts/bundle.sh verify ports
else
ports: sdk
	@test -f ports/build.lua || { \
	  echo 'Missing ports submodule: run git submodule update --init ports.' >&2; \
	  exit 1; }
	$(MAKE) -f scripts/ports.mk
	$(LUA) scripts/stage-tree.lua ports/install.lua build/ports-root ports=build/ports
	$(LUA) scripts/stage-tree.lua ports/develop.lua build/ports-dev ports=build/ports
	./scripts/bundle.sh record ports
endif

bundle-kernel: kernel
	./scripts/bundle.sh pack kernel
bundle-sdk: sdk
	./scripts/bundle.sh pack sdk
bundle-userspace: userspace
	./scripts/bundle.sh pack userspace
bundle-ports: ports
	./scripts/bundle.sh pack ports

initrd: build/initrd.cpio

# Recursive builds/selected bundles finish before assembly observes their output.
# Fresh staging removes stale inputs; unchanged contents retain the archive mtime.
build/initrd.cpio: userspace ports Makefile boot/initrd.lua scripts/stage-tree.lua scripts/assemble-initrd.sh
	./scripts/assemble-initrd.sh

check-toolchain:
	@command -v $(CC) >/dev/null 2>&1 || { \
	  echo "Missing $(CC): add the cross-toolchain to PATH or set CROSS_COMPILE." >&2; \
	  exit 1; }

ifneq ($(filter kernel,$(PREBUILT)),)
build/caelum.elf: | kernel
	@test -f $@
else
build/caelum.elf: $(OBJECTS) arch/x86_64/linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)
endif

# Preserve the header timestamp unless the selected level actually changes.
# Compiler flags alone do not make existing objects out of date.
FORCE:

build/kernel-log-config.h: FORCE
	@mkdir -p $(@D)
	@printf '#define KLOG_TRACE_ENABLED %s\n' $(TRACE_ENABLED) > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

# Only identity depends on HEAD. Preserve the timestamp for unchanged revisions.
build/kernel-build-revision.h: scripts/kernel-build-revision.sh FORCE
	@mkdir -p $(@D)
	@./scripts/kernel-build-revision.sh > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

build/kernel/object/system_info.o: build/kernel-build-revision.h

build/%.o: %.c build/kernel-log-config.h | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/%.o: %.S | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/limine.conf: boot/limine/limine.conf scripts/configure-boot.sh FORCE
	./scripts/configure-boot.sh

image: build/pyxis.iso

build/pyxis.iso: build/caelum.elf build/initrd.cpio \
                 build/limine.conf scripts/make-image.sh \
                 third_party/limine/BOOTX64.EFI third_party/limine/limine-uefi-cd.bin | image-inputs
	./scripts/make-image.sh

image-inputs: kernel sdk
	@test "$$(sed -n 's/^abi_sha256=//p' build/bundle-info/kernel.txt)" = \
	  "$$(sed -n 's/^abi_sha256=//p' build/bundle-info/sdk.txt)" || { \
	  echo 'Kernel and SDK interfaces differ; select matching bundles.' >&2; exit 1; }

run debug: image
	QEMU="$(QEMU)" QEMU_DISPLAY="$(QEMU_DISPLAY)" MEMORY="$(MEMORY)" CPUS="$(CPUS)" ACCEL="$(ACCEL)" \
	OVMF_CODE="$(OVMF_CODE)" OVMF_VARS="$(OVMF_VARS)" ./scripts/run-qemu.sh $@

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
