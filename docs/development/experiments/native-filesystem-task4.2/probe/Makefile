SDK ?= ../../../../../build/sdk
BUILD ?= ../../../../../build/installer-probe
SDK := $(abspath $(SDK))
BUILD := $(abspath $(BUILD))
SOURCE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
include $(SDK)/share/pyxis.mk

.PHONY: all
all: $(BUILD)/installer.pxe

$(BUILD)/main.o: $(SOURCE)/main.c
	mkdir -p $(BUILD)
	$(CC) $(PYXIS_CPPFLAGS) $(PYXIS_CFLAGS) -Werror -c $< -o $@

$(BUILD)/installer.elf: $(BUILD)/main.o $(PYXIS_START) $(PYXIS_LIBRARIES) $(PYXIS_LINKER_SCRIPT)
	$(CC) $(PYXIS_LDFLAGS) $(PYXIS_START) $< $(PYXIS_LDLIBS) -o $@

$(BUILD)/installer.pxe: $(BUILD)/installer.elf
	$(PYXIS_ELF2PXE) --format p1f -o $@ $<

-include $(BUILD)/main.d
