CROSS_COMPILE ?= x86_64-unknown-pyxis-
CC := $(CROSS_COMPILE)clang
HOSTCC ?= cc
HOSTAR ?= ar
LUA ?= lua
PYTHON ?= python3
export CROSS_COMPILE HOSTCC LUA
QEMU ?= qemu-system-x86_64
QEMU_DISPLAY ?= gtk
QEMU_VIDEO ?= std
export QEMU_VIDEO
MEMORY ?= 8G
CPUS ?= 1
THREADS ?= 1
ACCEL ?= kvm
VIRTIO_FS_SOCKET ?=
VIRTIO_NET ?= 0
VIRTIO_RNG ?= 1
QEMU_NO_REBOOT ?= 0
VIRTIO_BLK_IMAGE ?=
VIRTIO_BLK_READONLY ?= 0
VFIO_PCI ?=
USB_IMAGE_MIB ?= 1024
USB_ESP_MIB ?= 256
USB_POOL_JOURNAL ?= 8MiB
USB_BOOT_IMAGE ?= build/pyxis-usb.img
export USB_IMAGE_MIB USB_ESP_MIB USB_POOL_JOURNAL
TCP_FORWARD ?=
UDP_FORWARD ?=
export VIRTIO_FS_SOCKET VIRTIO_NET TCP_FORWARD UDP_FORWARD VIRTIO_RNG VIRTIO_BLK_IMAGE VIRTIO_BLK_READONLY QEMU_NO_REBOOT
export VFIO_PCI
INIT ?=
GO_HELLO ?=
LIVE_CONFIG ?=
NETWORK_CONFIG ?=
# HOSTNAME commonly comes from the build host's environment. Only an explicit
# command-line value selects the live image's name, including an empty value.
ifeq ($(origin HOSTNAME),command line)
override PYXIS_IMAGE_HOSTNAME := $(HOSTNAME)
override PYXIS_IMAGE_HOSTNAME_SET := 1
IMAGE_HOSTNAME_TOOL := build/tools/machine-hostname
else
override PYXIS_IMAGE_HOSTNAME :=
override PYXIS_IMAGE_HOSTNAME_SET :=
endif
export PYXIS_IMAGE_HOSTNAME PYXIS_IMAGE_HOSTNAME_SET
MOUNT_DISK ?=
BOOT_MENU_TIMEOUT ?= 0
USB_HID_DESCRIBE ?= 0
WIFI_AX200 ?= 0
WIFI_RTL8922AE ?= 0
REMOTE_BEACON ?=
LOG_UDP ?= 0
LOG_CONSOLE_LEVEL ?=
DEBUG_NET ?=
DEBUG_WAIT ?= 0
DISPLAY_SIZE ?=
DISPLAY_TIMING ?=
DISPLAY_TIMING_METRICS ?= 0
DISPLAY_IDLE_BLANK ?=
DISPLAY_INVENTORY ?= 0
DISPLAY_FLIP ?=
DISPLAY_FLIP_METRICS ?= 0
DISPLAY_CURSOR_PROBE ?= 0
POINTER_SYNTHETIC ?= 0
ifneq ($(POINTER_SYNTHETIC),0)
ifneq ($(POINTER_SYNTHETIC),1)
$(error POINTER_SYNTHETIC must be 0 or 1)
endif
endif
export WIFI_AX200 WIFI_RTL8922AE USB_HID_DESCRIBE MOUNT_DISK BOOT_MENU_TIMEOUT REMOTE_BEACON LOG_UDP LOG_CONSOLE_LEVEL DEBUG_NET DEBUG_WAIT DISPLAY_SIZE DISPLAY_TIMING DISPLAY_TIMING_METRICS DISPLAY_IDLE_BLANK DISPLAY_INVENTORY DISPLAY_FLIP DISPLAY_FLIP_METRICS DISPLAY_CURSOR_PROBE POINTER_SYNTHETIC
# Space-separated components already extracted from bundles at the repo root.
PREBUILT ?=
ifneq ($(filter-out kernel sdk userspace ports,$(PREBUILT)),)
$(error PREBUILT accepts kernel sdk userspace ports)
endif
# Games are packages; refusing these avoids silently dropping an old input.
ifneq ($(strip $(DOOM_WAD) $(DOOM_DEMOS) $(QUAKE_DATA) $(DIABLO_DATA) $(DUKE3D_DATA)),)
$(error Game data is no longer an image input; use the recipes in packages/README.md)
endif
ifneq ($(filter-out 0,$(DEVILUTIONX) $(EDUKE32)),)
$(error DEVILUTIONX and EDUKE32 are retired; pack the personal diablo and duke3d packages with make game-packages)
endif
export INIT LIVE_CONFIG NETWORK_CONFIG GO_HELLO
# The boot archive (boot/initrd.lua): the core archive, or every program, the
# init scripts and the live spaces (docs/wip/pkg-upgrade.md, decisions 20 and 64).
DISTRIBUTION ?= core
ifneq ($(filter-out core full,$(DISTRIBUTION))$(words $(DISTRIBUTION)),1)
$(error DISTRIBUTION must be core or full)
endif
export DISTRIBUTION
# The archive's built-in package repository (decisions 65 and 68); empty builds
# an image with none. The release process sets the release value.
PACKAGE_REPOSITORY ?= http://192.168.0.213:5245/public/
# Development repositories are unsigned (decision 72); a release image sets 1.
PACKAGE_REPOSITORY_SIGNED ?= 0
# Every image can upgrade from the signed releases repository (decision 74); an
# empty value leaves it out.
RELEASE_REPOSITORY ?= https://packages.pyxis.nexus/releases/
export PACKAGE_REPOSITORY PACKAGE_REPOSITORY_SIGNED RELEASE_REPOSITORY

LOG_LEVEL ?= debug
ifeq ($(LOG_LEVEL),info)
LOG_LEVEL_CEILING := LOG_LEVEL_INFO
else ifeq ($(LOG_LEVEL),debug)
LOG_LEVEL_CEILING := LOG_LEVEL_DEBUG
else ifeq ($(LOG_LEVEL),trace)
LOG_LEVEL_CEILING := LOG_LEVEL_TRACE
else
$(error LOG_LEVEL must be info, debug or trace)
endif
OVMF_CODE ?= /usr/share/OVMF/x64/OVMF_CODE.4m.fd
OVMF_VARS ?= /usr/share/OVMF/x64/OVMF_VARS.4m.fd

CPPFLAGS := -Ibuild -Iinclude -Iarch/x86_64/include -Ithird_party/limine -Ithird_party/tlsf
CPPFLAGS += -DPOINTER_SYNTHETIC_ENABLED=$(POINTER_SYNTHETIC)
CFLAGS := -std=gnu23 -O2 -g3 -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=kernel \
          -fno-omit-frame-pointer -Wall -Wextra -Wshadow -Wstrict-prototypes \
          -Wmissing-prototypes -MMD -MP
LDFLAGS := -nostdlib -static -no-pie -Wl,-T,arch/x86_64/linker.ld \
           -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -Wl,-Map,build/caelum.map
# The Pyxis Clang driver links P1F executables; Limine loads the kernel as ELF.
LDFLAGS += -Wl,--oformat=elf

C_SOURCES := $(wildcard boot/limine/*.c arch/x86_64/*.c arch/x86_64/amd/*.c arch/x86_64/ps2/*.c kernel/input/touchpad.c kernel/input/hid_i2c.c kernel/input/hid_layout.c kernel/input/hid_touchpad.c kernel/input/hid_clickpad.c kernel/input/hid_pointer.c kernel/input/hid_i2c_metrics.c kernel/i2c/*.c kernel/*.c kernel/debug/*.c kernel/random/*.c kernel/audio/*.c kernel/wifi/*.c kernel/bluetooth/*.c kernel/boot/*.c kernel/user/*.c kernel/object/*.c kernel/service/*.c kernel/fs/*.c kernel/mm/*.c kernel/fb/*.c kernel/ui/*.c kernel/display/*.c kernel/pci/*.c kernel/virtio/*.c kernel/usb/*.c kernel/storage/*.c kernel/net/*.c kernel/net/rtl8111/*.c lib/*.c) \
             third_party/tlsf/tlsf.c
ifeq ($(POINTER_SYNTHETIC),1)
C_SOURCES += kernel/input/synthetic_pointer.c
endif
ASM_SOURCES := $(wildcard boot/limine/*.S arch/x86_64/*.S)
OBJECTS := $(patsubst %.c,build/%.o,$(C_SOURCES)) $(patsubst %.S,build/%.o,$(ASM_SOURCES))

build/kernel/volume_icons.h: assets/ui/volume/pyxis-icons.png scripts/volume-icons.py
	$(PYTHON) scripts/volume-icons.py $< $@

build/kernel/volume_ui.o: build/kernel/volume_icons.h

build/kernel/ui_sound.h: assets/ui/power/ctrl-alt-del-button.wav scripts/audio-ui-sound.py
	$(PYTHON) scripts/audio-ui-sound.py $< $@

build/kernel/object/audio.o: build/kernel/ui_sound.h

.DEFAULT_GOAL := all
include kernel/net/lwip/build.mk
include kernel/fs/build.mk
include kernel/acpi/build.mk

.PHONY: all menuconfig kernel bundle-kernel bundle-sdk bundle-userspace bundle-ports image-inputs tools fs-tools sdk sdk-headers userspace ports recovery-initrd initrd image usb-image run debug run-usb debug-usb clean check-toolchain FORCE
all: kernel

menuconfig:
	$(PYTHON) -m menuconfig Kconfig

ifneq ($(filter kernel,$(PREBUILT)),)
kernel:
	./scripts/bundle.sh verify kernel

build/kernel-wifi-NOTICE: | kernel
	@test -f $@

build/kernel-amd-NOTICE: | kernel
	@test -f $@

build/kernel-random-NOTICE: | kernel
	@test -f $@
else
kernel: build/caelum.elf build/kernel-random-NOTICE build/kernel-amd-NOTICE build/kernel-wifi-NOTICE
	CC="$(CC)" LOG_LEVEL="$(LOG_LEVEL)" CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" ./scripts/bundle.sh record kernel

build/kernel-wifi-NOTICE: kernel/wifi/NOTICE
	@mkdir -p build
	cp $< $@

build/kernel-amd-NOTICE: arch/x86_64/amd/NOTICE
	@mkdir -p build
	cp $< $@

build/kernel-random-NOTICE: kernel/random/NOTICE
	@mkdir -p build
	cp $< $@
endif

# Data-only cached preparation for packages/fonts.lua, outside ordinary images.
.PHONY: font-assets
font-assets: sdk
	$(MAKE) -f scripts/ports.mk fonts

# Game packages for this build's system, packed only as an explicit release or
# deploy step and never a dependency of another target. In-tree recipes cover
# doom, quake and minesweeper; duke3d and diablo use private GAME_RECIPES.
GAMES ?= doom quake
GAME_RECIPES ?= packages
GAME_NAMES := doom quake minesweeper duke3d diablo
.PHONY: game-packages
ifneq ($(filter game-packages,$(MAKECMDGOALS)),)
ifeq ($(REPOSITORY),)
$(error Usage: make game-packages REPOSITORY=DIR [GAMES="doom quake"] [GAME_RECIPES=DIR])
endif
ifneq ($(filter-out $(GAME_NAMES),$(GAMES)),)
$(error Unknown games: $(filter-out $(GAME_NAMES),$(GAMES)); choose from $(GAME_NAMES))
endif
ifneq ($(shell git status --porcelain --ignore-submodules=none),)
$(error game-packages: the checkout has local changes, so its revision would not name these bytes)
endif
endif
game-packages: sdk build/os-release
	$(MAKE) -f scripts/ports.mk $(addprefix game-ports-,$(GAMES))
	for game in $(GAMES); do \
	  python3 tools/pxpkg.py pack --build build "$(GAME_RECIPES)/$$game.lua" "$(REPOSITORY)" || exit 1; \
	done
	$(PUBLISH_REPOSITORY)

# One package per program (docs/wip/pkg-upgrade.md, step 2), and the dev
# packages of sdk:// in packages/dev. packages/base.lua names the base set;
# every other recipe in those directories is a development package. Each target
# packs from this build, so its revision or release names the packages' ABI.
BASE_PROGRAMS = $(shell $(LUA) -e 'for _, d in ipairs(dofile("packages/base.lua").depends) do print(d.id) end')
DEVELOPMENT_PROGRAMS = $(filter-out $(BASE_PROGRAMS),$(basename $(notdir $(wildcard packages/programs/*.lua packages/dev/*.lua))))
PACKAGE_RECIPE = recipe=packages/programs/$$program.lua; [ -f "$$recipe" ] || recipe=packages/dev/$$program.lua
# Each package target ends with an unsigned publish under REPOSITORY_NAME, the
# name images configure it under; an empty name leaves the publish, as for a tree
# tools/sign-repository.sh will publish signed.
REPOSITORY_NAME ?= public
PUBLISH_REPOSITORY = $(if $(REPOSITORY_NAME),python3 tools/pxpkg.py publish --repository "$(REPOSITORY_NAME)" "$(REPOSITORY)")
.PHONY: base-packages development-packages release-package
ifneq ($(filter base-packages development-packages release-package,$(MAKECMDGOALS)),)
ifeq ($(REPOSITORY),)
$(error Usage: make base-packages|development-packages|release-package REPOSITORY=DIR)
endif
endif
base-packages: userspace ports build/os-release
	for program in $(BASE_PROGRAMS); do \
	  $(PACKAGE_RECIPE); \
	  python3 tools/pxpkg.py pack --build build "$$recipe" "$(REPOSITORY)" || exit 1; \
	done
	python3 tools/pxpkg.py pack --build build packages/base.lua "$(REPOSITORY)"
	$(PUBLISH_REPOSITORY)

development-packages: userspace ports build/os-release
	for program in $(DEVELOPMENT_PROGRAMS); do \
	  $(PACKAGE_RECIPE); \
	  python3 tools/pxpkg.py pack --build build "$$recipe" "$(REPOSITORY)" || exit 1; \
	done
	$(PUBLISH_REPOSITORY)

# A tagged build's release package (decision 73), packed into a signing tree;
# tools/sign-repository.sh releases DIR then publishes it.
release-package: image
	python3 tools/pxpkg.py pack --build build packages/release.lua "$(REPOSITORY)"

# Optional server package: it never enters the default image or package set.
.PHONY: caddy-package
ifneq ($(filter caddy-package,$(MAKECMDGOALS)),)
ifeq ($(REPOSITORY),)
$(error Usage: make caddy-package REPOSITORY=DIR GO_PREFIX=ABSOLUTE_DIR)
endif
ifeq ($(GO_PREFIX),)
$(error Caddy needs the pinned native Go cross-toolchain: set GO_PREFIX)
endif
ifneq ($(shell git status --porcelain --ignore-submodules=none),)
$(error caddy-package: the checkout has local changes, so its revision would not name these bytes)
endif
endif
caddy-package: sdk build/os-release
	$(MAKE) -f scripts/ports.mk caddy GO_PREFIX="$(GO_PREFIX)"
	python3 tools/pxpkg.py pack --build build packages/caddy.lua "$(REPOSITORY)"
	$(PUBLISH_REPOSITORY)

tools:
	$(MAKE) -C tools

build/tools/machine-hostname: FORCE | sdk
	$(MAKE) -C tools machine-hostname

fs-tools:
	@test -f fs/Makefile || { \
	  echo 'Missing filesystem submodule: run git submodule update --init fs.' >&2; \
	  exit 1; }
	$(MAKE) -C fs SOURCE=$(abspath fs) BUILD=$(abspath build/fs-tools) HOST_CC="$(HOSTCC)" HOST_AR="$(HOSTAR)"

sdk-headers: build/toolchain
	@test -f userspace/runtime.mk || { \
	  echo 'Missing userspace submodule: run git submodule update --init userspace.' >&2; \
	  exit 1; }
	./scripts/export-sdk.sh headers

ifneq ($(filter sdk,$(PREBUILT)),)
sdk:
	./scripts/bundle.sh verify sdk
else
sdk: sdk-headers
	$(MAKE) -C userspace -f runtime.mk SDK=$(abspath build/sdk) BUILD=$(abspath build/runtime)
	$(MAKE) -f scripts/npfs-sdk.mk SDK=$(abspath build/sdk)
	+./scripts/cxx-runtime.sh
	./scripts/export-sdk.sh complete
	./scripts/bundle.sh record sdk
endif

ifneq ($(filter userspace,$(PREBUILT)),)
userspace: ports
	./scripts/bundle.sh verify userspace
else
userspace: ports
	$(MAKE) -C userspace SDK=$(abspath build/sdk) LUA_PREFIX=$(abspath build/ports-dev/lua) PICOHTTPPARSER_PREFIX=$(abspath build/ports-dev/picohttpparser) MBEDTLS_PREFIX=$(abspath build/ports-dev/mbedtls) ZLIB_PREFIX=$(abspath build/ports-dev/zlib) LIBPNG_PREFIX=$(abspath build/ports-dev/libpng) WPA_RSN_PREFIX=$(abspath build/ports-dev/wpa-supplicant) BUILD=$(abspath build/userspace) install DESTDIR=$(abspath build/userspace-root)
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

# Firmware is an image dependency; kernel-only compilation stays offline.
.PHONY: ax200-firmware ax200-wifi-firmware rtl8922ae-firmware
ifeq ($(WIFI_RTL8922AE),1)
RTL8922AE_FIRMWARE_DEPENDENCY := rtl8922ae-firmware
endif

rtl8922ae-firmware:
	python3 scripts/fetch-firmware.py --metadata firmware/rtl8922ae/metadata.json --output build/firmware/rtl8922ae

ax200-wifi-firmware:
	python3 scripts/fetch-firmware.py --metadata firmware/ax200-wifi/metadata.json --output build/firmware/ax200-wifi

ax200-firmware:
	python3 scripts/fetch-firmware.py --metadata firmware/ax200/metadata.json --output build/firmware/ax200

initrd: build/initrd.cpio

recovery-initrd: build/recovery.cpio

# Select the rescue payload directly from component roots, before personal image
# overrides are applied. Never build recovery from the complete live archive.
build/recovery.cpio: userspace ports Makefile build/packages.lua boot/rescue.list scripts/recovery-initrd.lua scripts/stage-tree.lua scripts/assemble-recovery-initrd.sh build/kernel-random-NOTICE build/kernel-amd-NOTICE
	./scripts/assemble-recovery-initrd.sh

# Recursive builds/selected bundles finish before assembly observes their output.
# Fresh staging removes stale inputs; unchanged contents retain the archive mtime.
build/initrd.cpio: userspace ports ax200-firmware ax200-wifi-firmware $(RTL8922AE_FIRMWARE_DEPENDENCY) build/recovery.cpio Makefile boot/initrd.lua boot/rescue.list scripts/stage-tree.lua scripts/assemble-initrd.sh build/kernel-random-NOTICE build/kernel-amd-NOTICE build/kernel-wifi-NOTICE build/os-release build/distribution build/packages.lua \
                   boot/limine/limine.conf third_party/limine/BOOTX64.EFI third_party/limine/LICENSE $(IMAGE_HOSTNAME_TOOL)
	./scripts/assemble-initrd.sh

check-toolchain: build/toolchain
	@command -v $(CC) >/dev/null 2>&1 || { \
	  echo "Missing $(CC): add the cross-toolchain to PATH or set CROSS_COMPILE." >&2; \
	  exit 1; }

# Objects and SDK archives from different compilers must not mix in one build.
build/toolchain: FORCE
	@mkdir -p $(@D)
	@if [ ! -f $@ ]; then echo $(CC) > $@; \
	elif [ "$$(cat $@)" != $(CC) ]; then \
	  echo "build/ was made with $$(cat $@); run make clean first." >&2; \
	  exit 1; fi

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

build/kernel-config.h: Kconfig .config FORCE
	@mkdir -p $(@D)
	$(PYTHON) -m genconfig --header-path $@ --config-out build/kernel.config Kconfig

build/kernel/init.o build/arch/x86_64/clock.o: build/kernel-config.h

build/kernel-log-config.h: FORCE
	@mkdir -p $(@D)
	@printf '#define LOG_LEVEL_CEILING %s\n' $(LOG_LEVEL_CEILING) > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

# Rebuild the gated consumers when the flag changes, including 1 back to 0.
build/pointer-synthetic-config.h: FORCE
	@mkdir -p $(@D)
	@printf '#define POINTER_SYNTHETIC_ENABLED %s\n' $(POINTER_SYNTHETIC) > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

build/kernel/input.o build/kernel/boot/options.o build/kernel/input/synthetic_pointer.o build/kernel/display/display.o: build/pointer-synthetic-config.h

# Only identity depends on HEAD. Preserve the timestamp for unchanged revisions.
build/kernel-build-revision.h: scripts/kernel-build-revision.sh FORCE
	@mkdir -p $(@D)
	@./scripts/kernel-build-revision.sh > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

build/kernel/object/system_info.o: build/kernel-build-revision.h

# Like the revision header, boot://os-release follows HEAD and its tag.
build/os-release: boot/os-release.lua scripts/os-release.sh scripts/os-release.lua scripts/kernel-build-revision.sh FORCE
	@mkdir -p $(@D)
	@LUA=$(LUA) ./scripts/os-release.sh > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

build/%.o: %.c build/kernel-log-config.h | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/%.o: %.S | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/limine.conf: boot/limine/limine.conf scripts/configure-boot.sh FORCE
	./scripts/configure-boot.sh

build/packages.lua: scripts/package-config.lua FORCE
	@mkdir -p build
	@$(LUA) scripts/package-config.lua $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

# Rebuilds the archive when DISTRIBUTION changes.
build/distribution: FORCE
	@mkdir -p build
	@echo $(DISTRIBUTION) > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

ifneq ($(strip $(DEBUG_NET)),)
build/limine.conf: build/caelum.elf
endif

image: build/pyxis.iso

usb-image: build/caelum.elf build/initrd.cpio build/limine.conf fs-tools \
           third_party/limine/BOOTX64.EFI | image-inputs
	./scripts/make-usb-image.sh

build/pyxis.iso: build/caelum.elf build/initrd.cpio \
                 build/limine.conf scripts/make-image.sh \
                 third_party/limine/BOOTX64.EFI third_party/limine/limine-uefi-cd.bin | image-inputs
	./scripts/make-image.sh

image-inputs: kernel sdk
	@test "$$(sed -n 's/^abi_sha256=//p' build/bundle-info/kernel.txt)" = \
	  "$$(sed -n 's/^abi_sha256=//p' build/bundle-info/sdk.txt)" || { \
	  echo 'Kernel and SDK interfaces differ; select matching bundles.' >&2; exit 1; }

run debug: image
	QEMU="$(QEMU)" QEMU_DISPLAY="$(QEMU_DISPLAY)" MEMORY="$(MEMORY)" CPUS="$(CPUS)" THREADS="$(THREADS)" ACCEL="$(ACCEL)" \
	OVMF_CODE="$(OVMF_CODE)" OVMF_VARS="$(OVMF_VARS)" ./scripts/run-qemu.sh $@

# Launch an existing image; rebuilding would replace its pool and identities.
run-usb debug-usb:
	QEMU="$(QEMU)" QEMU_DISPLAY="$(QEMU_DISPLAY)" MEMORY="$(MEMORY)" CPUS="$(CPUS)" THREADS="$(THREADS)" ACCEL="$(ACCEL)" \
	OVMF_CODE="$(OVMF_CODE)" OVMF_VARS="$(OVMF_VARS)" USB_BOOT_IMAGE="$(USB_BOOT_IMAGE)" \
	./scripts/run-qemu.sh $@

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
