# Invoked by make ports after SDK export. Discover dependencies here so Make
# sees the freshly exported headers/libraries rather than their earlier mtimes.
LUA ?= lua
KILO_INPUTS := $(wildcard ports/*.lua ports/kilo/*.lua ports/kilo/Makefile ports/kilo/patches/*.patch)
SDK_INPUTS := $(shell find build/sdk -type f)
KILO_IMAGE := build/ports/kilo/stage/bin/kilo.pxe
KILO_LICENSE := build/ports/kilo/stage/share/licenses/kilo/LICENSE

.PHONY: all
all: $(KILO_IMAGE) $(KILO_LICENSE)

# This work tree is disposable build output. Port edits belong in ports/kilo,
# not the fetched source copy, which is replaced when its inputs change.
$(KILO_IMAGE) $(KILO_LICENSE) &: $(KILO_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; \
	  exit 1; }
	rm -rf build/ports/kilo
	$(LUA) ports/build.lua kilo --sdk $(abspath build/sdk) --work $(abspath build/ports/kilo)
