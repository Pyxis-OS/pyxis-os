# Opt-in manual fixture; never part of ordinary images or automated tests.
FIXTURE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SDK ?= build/sdk
RAYLIB ?= build/ports/raylib/stage/dev
SDL2 ?= build/ports/sdl2/stage/dev
OUTPUT ?= build/raylib-audio.pxb
include $(SDK)/share/pyxis.mk

.PHONY: all
all: $(OUTPUT)/app/bin/audio.pxe $(OUTPUT)/app/tone.wav $(OUTPUT)/manifest.json

$(OUTPUT)/app/bin/audio.pxe: $(FIXTURE_DIR)/audio-check.c $(RAYLIB)/lib/libraylib.a $(SDL2)/lib/libSDL2.a
	mkdir -p $(dir $@)
	$(CC) $(PYXIS_CPPFLAGS) $(PYXIS_CFLAGS) -I$(RAYLIB)/include $(PYXIS_LDFLAGS) $(PYXIS_START) $< $(RAYLIB)/lib/libraylib.a $(SDL2)/lib/libSDL2.a $(PYXIS_LDLIBS) -o $@

$(OUTPUT)/app/tone.wav: $(FIXTURE_DIR)/tone.py
	mkdir -p $(dir $@)
	python3 $< $@

$(OUTPUT)/manifest.json: $(FIXTURE_DIR)/manifest.json
	mkdir -p $(dir $@)
	cp $< $@
