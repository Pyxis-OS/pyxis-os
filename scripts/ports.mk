# Invoked by make ports after SDK export. Discover dependencies here so Make
# sees the freshly exported headers/libraries rather than their earlier mtimes.
LUA ?= lua
KILO_INPUTS := $(wildcard ports/*.lua ports/kilo/*.lua ports/kilo/Makefile ports/kilo/patches/*.patch)
DOOM_INPUTS := $(wildcard ports/doom/*.lua ports/doom/Makefile ports/doom/*.c \
                         ports/doom/patches/*.patch) ports/ports.lua ports/build.lua
DOOM_IMAGE := build/ports/doom/stage/bin/doom.pxe
DOOM_LICENSE := build/ports/doom/stage/share/licenses/doom/LICENSE

TCC_INPUTS := $(wildcard ports/*.lua ports/tcc/*.lua ports/tcc/Makefile ports/tcc/patches/*.patch)
SDK_INPUTS := $(shell find build/sdk -type f)
KILO_IMAGE := build/ports/kilo/stage/bin/kilo.pxe
KILO_LICENSE := build/ports/kilo/stage/share/licenses/kilo/LICENSE

LUA_INPUTS := $(wildcard ports/lua/*.lua ports/lua/*.c ports/lua/*.h \
                        ports/lua/Makefile ports/lua/patches/*.patch) ports/ports.lua ports/build.lua
LUA_IMAGE := build/ports/lua/stage/bin/lua.pxe
LUA_LICENSE := build/ports/lua/stage/share/licenses/lua/lua.h
LUA_DEVELOP := $(addprefix build/ports/lua/stage/dev/,lib/liblua.a \
               include/lua.h include/luaconf.h include/lauxlib.h include/lualib.h)

TCC_STAGE := build/ports/tcc/stage
TCC_OUTPUTS := $(addprefix $(TCC_STAGE)/,bin/tcc.pxe lib/tcc/libtcc1.a \
  lib/tcc/include/stddef.h lib/tcc/include/stdarg.h lib/tcc/include/stdbool.h \
  lib/tcc/include/float.h share/licenses/tcc/COPYING share/licenses/tcc/libtcc1.c \
  share/licenses/tcc/va_list.c share/licenses/tcc/builtin.c share/tcc/source.txt)
TCC_OUTPUTS += $(addprefix $(TCC_STAGE)/share/tcc/patches/,$(notdir $(wildcard ports/tcc/patches/*.patch)))

TZDATA_INPUTS := $(wildcard ports/tzdata/*.lua) ports/ports.lua ports/build.lua
TZDATA_STAGE := build/ports/tzdata/stage
TZDATA_OUTPUTS := $(addprefix $(TZDATA_STAGE)/,share/zoneinfo/UTC \
  share/zoneinfo/Europe/Bucharest share/zoneinfo/tzdata.zi share/zoneinfo/version \
  share/licenses/tzdata/LICENSE share/tzdata/source.txt)

.PHONY: all
all: $(DOOM_IMAGE) $(DOOM_LICENSE) $(KILO_IMAGE) $(KILO_LICENSE) \
     $(LUA_IMAGE) $(LUA_LICENSE) $(LUA_DEVELOP) $(TCC_OUTPUTS) $(TZDATA_OUTPUTS)

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

$(LUA_IMAGE) $(LUA_LICENSE) $(LUA_DEVELOP) &: $(LUA_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/lua
	$(LUA) ports/build.lua lua --sdk $(abspath build/sdk) --work $(abspath build/ports/lua)

$(DOOM_IMAGE) $(DOOM_LICENSE) &: $(DOOM_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/doom
	$(LUA) ports/build.lua doom --sdk $(abspath build/sdk) --work $(abspath build/ports/doom)

# This recipe builds host zic and data only; target SDK changes do not alter TZif.
$(TZDATA_OUTPUTS) &: $(TZDATA_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/tzdata
	$(LUA) ports/build.lua tzdata --sdk $(abspath build/sdk) --work $(abspath build/ports/tzdata)
