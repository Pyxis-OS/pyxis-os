CROSS_COMPILE ?= x86_64-elf-
CC := $(CROSS_COMPILE)gcc
QEMU ?= qemu-system-x86_64
MEMORY ?= 256M
ACCEL ?= tcg
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS.fd

CPPFLAGS := -Iinclude -Iarch/x86_64/include -Ithird_party/limine
CFLAGS := -std=gnu23 -O2 -g3 -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=kernel \
          -fno-omit-frame-pointer -Wall -Wextra -Wshadow -Wstrict-prototypes \
          -Wmissing-prototypes -MMD -MP
LDFLAGS := -nostdlib -static -no-pie -Wl,-T,arch/x86_64/linker.ld \
           -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -Wl,-Map,build/caelum.map

C_SOURCES := $(wildcard boot/limine/*.c arch/x86_64/*.c kernel/*.c kernel/mm/*.c lib/*.c)
ASM_SOURCES := $(wildcard arch/x86_64/*.S)
OBJECTS := $(patsubst %.c,build/%.o,$(C_SOURCES)) $(patsubst %.S,build/%.o,$(ASM_SOURCES))

.PHONY: all image run debug clean
all: build/caelum.elf

build/caelum.elf: $(OBJECTS) arch/x86_64/linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

image: build/pyxis.iso

build/pyxis.iso: build/caelum.elf boot/limine/limine.conf scripts/make-image.sh \
                 third_party/limine/BOOTX64.EFI third_party/limine/limine-uefi-cd.bin
	./scripts/make-image.sh

run debug: image
	QEMU="$(QEMU)" MEMORY="$(MEMORY)" ACCEL="$(ACCEL)" \
	OVMF_CODE="$(OVMF_CODE)" OVMF_VARS="$(OVMF_VARS)" ./scripts/run-qemu.sh $@

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
