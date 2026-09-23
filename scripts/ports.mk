# Invoked by make ports after SDK export. Discover dependencies here so Make
# sees the freshly exported headers/libraries rather than their earlier mtimes.
LUA ?= lua
KILO_INPUTS := $(wildcard ports/*.lua ports/kilo/*.lua ports/kilo/Makefile ports/kilo/patches/*.patch)
TCC_INPUTS := $(wildcard ports/*.lua ports/tcc/*.lua ports/tcc/Makefile ports/tcc/patches/*.patch)
SDK_INPUTS := $(shell find build/sdk -type f)
KILO_IMAGE := build/ports/kilo/stage/bin/kilo.pxe
KILO_LICENSE := build/ports/kilo/stage/share/licenses/kilo/LICENSE

TCC_STAGE := build/ports/tcc/stage
TCC_OUTPUTS := $(addprefix $(TCC_STAGE)/,bin/tcc.pxe lib/tcc/libtcc1.a \
  lib/tcc/include/stddef.h lib/tcc/include/stdarg.h lib/tcc/include/stdbool.h \
  lib/tcc/include/float.h share/licenses/tcc/COPYING share/licenses/tcc/libtcc1.c \
  share/licenses/tcc/va_list.c share/licenses/tcc/builtin.c share/tcc/source.txt)
TCC_OUTPUTS += $(addprefix $(TCC_STAGE)/share/tcc/patches/,$(notdir $(wildcard ports/tcc/patches/*.patch)))

.PHONY: all
all: $(KILO_IMAGE) $(KILO_LICENSE) $(TCC_OUTPUTS)

# This work tree is disposable build output. Port edits belong in ports/kilo,
# not the fetched source copy, which is replaced when its inputs change.
$(KILO_IMAGE) $(KILO_LICENSE) &: $(KILO_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; \
	  exit 1; }
	rm -rf build/ports/kilo
	$(LUA) ports/build.lua kilo --sdk $(abspath build/sdk) --work $(abspath build/ports/kilo)

$(TCC_OUTPUTS) &: $(TCC_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; \
	  exit 1; }
	rm -rf build/ports/tcc
	$(LUA) ports/build.lua tcc --sdk $(abspath build/sdk) --work $(abspath build/ports/tcc)
