# Invoked by make ports after SDK export. Discover dependencies here so Make
# sees the freshly exported headers/libraries rather than their earlier mtimes.
LUA ?= lua
ifeq ($(shell command -v $(LUA) 2>/dev/null),)
$(error Missing Lua 5.4: install it or set LUA=lua5.4)
endif

# Each recipe's metadata.lua lists every file it stages, and is the only such list.
port_outputs = $(addprefix build/ports/$(1)/stage/,$(shell $(LUA) -e \
  'for _, path in pairs(dofile("ports/$(1)/metadata.lua").outputs) do print(path) end'))

KILO_INPUTS := $(wildcard ports/*.lua ports/kilo/*.lua ports/kilo/Makefile ports/kilo/patches/*.patch)
DOOM_INPUTS := $(wildcard ports/doom/*.lua ports/doom/Makefile ports/doom/*.c \
                         ports/doom/patches/*.patch) ports/ports.lua ports/build.lua
QUAKE_INPUTS := $(wildcard ports/quake/*.lua ports/quake/Makefile ports/quake/*.c \
                          ports/quake/*.h ports/quake/patches/*.patch) ports/ports.lua ports/build.lua
BUSYBOX_INPUTS := $(wildcard ports/busybox/*.lua ports/busybox/Makefile ports/busybox/*.c \
                            ports/busybox/*.h ports/busybox/patches/*.patch) ports/ports.lua ports/build.lua
LINKS_INPUTS := $(wildcard ports/links/*.lua ports/links/Makefile ports/links/*.c ports/links/*.h \
                          ports/links/include/*.h ports/links/include/*/*.h ports/links/patches/*.patch) \
                ports/ports.lua ports/build.lua

TCC_INPUTS := $(wildcard ports/*.lua ports/tcc/*.lua ports/tcc/Makefile ports/tcc/patches/*.patch)
SDK_INPUTS := $(shell find build/sdk -type f)
FASTFETCH_INPUTS := $(wildcard ports/fastfetch/*.lua ports/fastfetch/*.cmake \
  ports/fastfetch/Makefile ports/fastfetch/PORT-NOTICE ports/fastfetch/patches/*.patch) \
  ports/ports.lua ports/build.lua ports/LICENSE

LUA_INPUTS := $(wildcard ports/lua/*.lua ports/lua/*.c ports/lua/*.h \
                        ports/lua/Makefile ports/lua/patches/*.patch) ports/ports.lua ports/build.lua


TZDATA_INPUTS := $(wildcard ports/tzdata/*.lua) ports/ports.lua ports/build.lua

CA_CERTIFICATES_INPUTS := $(wildcard ports/ca-certificates/*) ports/ports.lua ports/build.lua

PCIIDS_INPUTS := $(wildcard ports/pciids/*) ports/ports.lua ports/build.lua

USBIDS_INPUTS := $(wildcard ports/usbids/*) ports/ports.lua ports/build.lua

SBASE_INPUTS := $(wildcard ports/sbase/*.lua ports/sbase/Makefile \
                          ports/sbase/patches/*.patch) ports/ports.lua ports/build.lua

PICOHTTPPARSER_INPUTS := $(wildcard ports/picohttpparser/*.lua ports/picohttpparser/Makefile) ports/ports.lua ports/build.lua

ZLIB_INPUTS := $(wildcard ports/zlib/*.lua ports/zlib/Makefile ports/zlib/PORT-NOTICE) \
               ports/ports.lua ports/build.lua

LIBUV_INPUTS := $(wildcard ports/libuv/*.lua ports/libuv/Makefile ports/libuv/PORT-NOTICE \
                         ports/libuv/include/uv/*.h ports/libuv/pyxis/*.c \
                         ports/libuv/pyxis/*.h ports/libuv/examples/* \
                         ports/libuv/patches/*.patch) ports/ports.lua ports/build.lua

LIBPNG_INPUTS := $(wildcard ports/libpng/*.lua ports/libpng/Makefile \
                          ports/libpng/*.dfa ports/libpng/PORT-NOTICE) \
                 ports/ports.lua ports/build.lua

FMT_INPUTS := $(wildcard ports/fmt/*.lua ports/fmt/Makefile ports/fmt/*.cmake \
                        ports/fmt/patches/*.patch) ports/ports.lua ports/build.lua

SDL2_INPUTS := $(wildcard ports/sdl2/*.lua ports/sdl2/Makefile ports/sdl2/SDL_config.h \
                         ports/sdl2/PORT-NOTICE ports/sdl2/pyxis/* ports/sdl2/cmake/* \
                         ports/sdl2/patches/*.patch) \
               ports/ports.lua ports/build.lua

MBEDTLS_INPUTS := $(wildcard ports/mbedtls/*.lua ports/mbedtls/*.h ports/mbedtls/*.mk \
                           ports/mbedtls/*.cmake ports/mbedtls/Makefile \
                           ports/mbedtls/PORT-NOTICE) ports/ports.lua ports/build.lua

CHOCOLATE_DOOM_INPUTS := $(wildcard ports/chocolate-doom/*.lua ports/chocolate-doom/Makefile \
                                   ports/chocolate-doom/PORT-NOTICE \
                                   ports/chocolate-doom/patches/*.patch) \
                         ports/ports.lua ports/build.lua

CHOCOLATE_QUAKE_INPUTS := $(wildcard ports/chocolate-quake/*.lua ports/chocolate-quake/Makefile \
                                    ports/chocolate-quake/PORT-NOTICE \
                                    ports/chocolate-quake/patches/*.patch) \
                          ports/ports.lua ports/build.lua

# EDuke32 is built only when DUKE3D_DATA asks for it; see its PORT-NOTICE.
EDUKE32_INPUTS := $(wildcard ports/eduke32/*.lua ports/eduke32/Makefile \
                            ports/eduke32/PORT-NOTICE ports/eduke32/patches/*.patch) \
                  ports/ports.lua ports/build.lua

# DevilutionX is built only when DIABLO_DATA asks for it; see its PORT-NOTICE.
DEVILUTIONX_INPUTS := $(wildcard ports/devilutionx/*.lua ports/devilutionx/Makefile \
                                ports/devilutionx/PORT-NOTICE ports/devilutionx/patches/*.patch) \
                      ports/ports.lua ports/build.lua

KILO_OUTPUTS := $(call port_outputs,kilo)
DOOM_OUTPUTS := $(call port_outputs,doom)
QUAKE_OUTPUTS := $(call port_outputs,quake)
BUSYBOX_OUTPUTS := $(call port_outputs,busybox)
LINKS_OUTPUTS := $(call port_outputs,links)
LUA_OUTPUTS := $(call port_outputs,lua)
FASTFETCH_OUTPUTS := $(call port_outputs,fastfetch)
TCC_OUTPUTS := $(call port_outputs,tcc)
TZDATA_OUTPUTS := $(call port_outputs,tzdata)
CA_CERTIFICATES_OUTPUTS := $(call port_outputs,ca-certificates)
PCIIDS_OUTPUTS := $(call port_outputs,pciids)
USBIDS_OUTPUTS := $(call port_outputs,usbids)
SBASE_OUTPUTS := $(call port_outputs,sbase)
PICOHTTPPARSER_OUTPUTS := $(call port_outputs,picohttpparser)
ZLIB_OUTPUTS := $(call port_outputs,zlib)
LIBUV_OUTPUTS := $(call port_outputs,libuv)
LIBPNG_OUTPUTS := $(call port_outputs,libpng)
FMT_OUTPUTS := $(call port_outputs,fmt)
SDL2_OUTPUTS := $(call port_outputs,sdl2)
MBEDTLS_OUTPUTS := $(call port_outputs,mbedtls)
DEVILUTIONX_OUTPUTS := $(call port_outputs,devilutionx)
CHOCOLATE_DOOM_OUTPUTS := $(call port_outputs,chocolate-doom)
CHOCOLATE_QUAKE_OUTPUTS := $(call port_outputs,chocolate-quake)
EDUKE32_OUTPUTS := $(call port_outputs,eduke32)

.PHONY: all
all: $(MBEDTLS_OUTPUTS) $(PICOHTTPPARSER_OUTPUTS) $(ZLIB_OUTPUTS) $(LIBPNG_OUTPUTS) $(FMT_OUTPUTS) $(SDL2_OUTPUTS) \
     $(DOOM_OUTPUTS) $(QUAKE_OUTPUTS) $(BUSYBOX_OUTPUTS) $(LINKS_OUTPUTS) $(KILO_OUTPUTS) $(LUA_OUTPUTS) \
     $(TCC_OUTPUTS) $(TZDATA_OUTPUTS) $(SBASE_OUTPUTS) $(CA_CERTIFICATES_OUTPUTS) $(PCIIDS_OUTPUTS) \
     $(USBIDS_OUTPUTS) $(FASTFETCH_OUTPUTS) $(CHOCOLATE_DOOM_OUTPUTS) $(CHOCOLATE_QUAKE_OUTPUTS)
all: $(LIBUV_OUTPUTS)
ifneq ($(DIABLO_DATA),)
all: $(DEVILUTIONX_OUTPUTS)
endif
ifneq ($(DUKE3D_DATA),)
all: $(EDUKE32_OUTPUTS)
endif

# This work tree is disposable build output. Port edits belong in ports/kilo,
# not the fetched source copy, which is replaced when its inputs change.
$(KILO_OUTPUTS) &: $(KILO_INPUTS) $(SDK_INPUTS) scripts/ports.mk
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

$(LUA_OUTPUTS) &: $(LUA_INPUTS) $(SDK_INPUTS) $(MBEDTLS_OUTPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/lua
	$(LUA) ports/build.lua lua --sdk $(abspath build/sdk) --work $(abspath build/ports/lua) --mbedtls $(abspath build/ports/mbedtls/stage/dev)

$(DOOM_OUTPUTS) &: $(DOOM_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/doom
	$(LUA) ports/build.lua doom --sdk $(abspath build/sdk) --work $(abspath build/ports/doom)

$(QUAKE_OUTPUTS) &: $(QUAKE_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/quake
	$(LUA) ports/build.lua quake --sdk $(abspath build/sdk) --work $(abspath build/ports/quake)

# This recipe builds host zic and data only; target SDK changes do not alter TZif.
$(TZDATA_OUTPUTS) &: $(TZDATA_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/tzdata
	$(LUA) ports/build.lua tzdata --sdk $(abspath build/sdk) --work $(abspath build/ports/tzdata)

# Pinned trust data is independent of target SDK contents.
$(CA_CERTIFICATES_OUTPUTS) &: $(CA_CERTIFICATES_INPUTS) scripts/ports.mk
	rm -rf build/ports/ca-certificates
	$(LUA) ports/build.lua ca-certificates --sdk $(abspath build/sdk) --work $(abspath build/ports/ca-certificates)

$(BUSYBOX_OUTPUTS) &: $(BUSYBOX_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/busybox
	$(LUA) ports/build.lua busybox --sdk $(abspath build/sdk) --work $(abspath build/ports/busybox)

$(LINKS_OUTPUTS) &: $(LINKS_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/links
	$(LUA) ports/build.lua links --sdk $(abspath build/sdk) --work $(abspath build/ports/links)

# The pinned PCI ID text is independent of target SDK contents.
$(PCIIDS_OUTPUTS) &: $(PCIIDS_INPUTS) scripts/ports.mk
	rm -rf build/ports/pciids
	$(LUA) ports/build.lua pciids --sdk $(abspath build/sdk) --work $(abspath build/ports/pciids)

$(SBASE_OUTPUTS) &: $(SBASE_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/sbase
	$(LUA) ports/build.lua sbase --sdk $(abspath build/sdk) --work $(abspath build/ports/sbase)

$(PICOHTTPPARSER_OUTPUTS) &: $(PICOHTTPPARSER_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/picohttpparser
	$(LUA) ports/build.lua picohttpparser --sdk $(abspath build/sdk) --work $(abspath build/ports/picohttpparser)

$(ZLIB_OUTPUTS) &: $(ZLIB_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/zlib
	$(LUA) ports/build.lua zlib --sdk $(abspath build/sdk) --work $(abspath build/ports/zlib)

$(LIBUV_OUTPUTS) &: $(LIBUV_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/libuv
	$(LUA) ports/build.lua libuv --sdk $(abspath build/sdk) --work $(abspath build/ports/libuv)

$(LIBPNG_OUTPUTS) &: $(LIBPNG_INPUTS) $(SDK_INPUTS) $(ZLIB_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/libpng
	$(LUA) ports/build.lua libpng --sdk $(abspath build/sdk) --work $(abspath build/ports/libpng) --zlib $(abspath build/ports/zlib/stage/dev)

$(FMT_OUTPUTS) &: $(FMT_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/fmt
	$(LUA) ports/build.lua fmt --sdk $(abspath build/sdk) --work $(abspath build/ports/fmt)

$(SDL2_OUTPUTS) &: $(SDL2_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/sdl2
	$(LUA) ports/build.lua sdl2 --sdk $(abspath build/sdk) --work $(abspath build/ports/sdl2)

$(DEVILUTIONX_OUTPUTS) &: $(DEVILUTIONX_INPUTS) $(SDK_INPUTS) $(ZLIB_OUTPUTS) $(LIBPNG_OUTPUTS) \
                          $(FMT_OUTPUTS) $(SDL2_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/devilutionx
	$(LUA) ports/build.lua devilutionx --sdk $(abspath build/sdk) --work $(abspath build/ports/devilutionx) \
	  --zlib $(abspath build/ports/zlib/stage/dev) --libpng $(abspath build/ports/libpng/stage/dev) \
	  --fmt $(abspath build/ports/fmt/stage/dev) --sdl2 $(abspath build/ports/sdl2/stage/dev)

$(CHOCOLATE_DOOM_OUTPUTS) &: $(CHOCOLATE_DOOM_INPUTS) $(SDK_INPUTS) $(SDL2_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/chocolate-doom
	$(LUA) ports/build.lua chocolate-doom --sdk $(abspath build/sdk) \
	  --work $(abspath build/ports/chocolate-doom) --sdl2 $(abspath build/ports/sdl2/stage/dev)

$(EDUKE32_OUTPUTS) &: $(EDUKE32_INPUTS) $(SDK_INPUTS) $(SDL2_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/eduke32
	$(LUA) ports/build.lua eduke32 --sdk $(abspath build/sdk) \
	  --work $(abspath build/ports/eduke32) --sdl2 $(abspath build/ports/sdl2/stage/dev)

$(CHOCOLATE_QUAKE_OUTPUTS) &: $(CHOCOLATE_QUAKE_INPUTS) $(SDK_INPUTS) $(SDL2_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/chocolate-quake
	$(LUA) ports/build.lua chocolate-quake --sdk $(abspath build/sdk) \
	  --work $(abspath build/ports/chocolate-quake) --sdl2 $(abspath build/ports/sdl2/stage/dev)

$(MBEDTLS_OUTPUTS) &: $(MBEDTLS_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/mbedtls
	$(LUA) ports/build.lua mbedtls --sdk $(abspath build/sdk) --work $(abspath build/ports/mbedtls)

$(FASTFETCH_OUTPUTS) &: $(FASTFETCH_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/fastfetch
	$(LUA) ports/build.lua fastfetch --sdk $(abspath build/sdk) --work $(abspath build/ports/fastfetch)

$(USBIDS_OUTPUTS) &: $(USBIDS_INPUTS) scripts/ports.mk
	rm -rf build/ports/usbids
	$(LUA) ports/build.lua usbids --sdk $(abspath build/sdk) --work $(abspath build/ports/usbids)
