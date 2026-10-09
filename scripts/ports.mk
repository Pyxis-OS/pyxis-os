# Invoked by make ports after SDK export. Discover dependencies here so Make
# sees the freshly exported headers/libraries rather than their earlier mtimes.
LUA ?= lua
KILO_INPUTS := $(wildcard ports/*.lua ports/kilo/*.lua ports/kilo/Makefile ports/kilo/patches/*.patch)
DOOM_INPUTS := $(wildcard ports/doom/*.lua ports/doom/Makefile ports/doom/*.c \
                         ports/doom/patches/*.patch) ports/ports.lua ports/build.lua
DOOM_IMAGE := build/ports/doom/stage/bin/doom.pxe
DOOM_LICENSE := build/ports/doom/stage/share/licenses/doom/LICENSE
QUAKE_INPUTS := $(wildcard ports/quake/*.lua ports/quake/Makefile ports/quake/*.c \
                          ports/quake/*.h ports/quake/patches/*.patch) ports/ports.lua ports/build.lua
QUAKE_IMAGE := build/ports/quake/stage/bin/quake.pxe
QUAKE_LICENSE := build/ports/quake/stage/share/licenses/quake/LICENSE
BUSYBOX_INPUTS := $(wildcard ports/busybox/*.lua ports/busybox/Makefile ports/busybox/*.c \
                            ports/busybox/*.h ports/busybox/patches/*.patch) ports/ports.lua ports/build.lua
BUSYBOX_IMAGE := $(addprefix build/ports/busybox/stage/bin/,vi.pxe less.pxe tar.pxe)
BUSYBOX_LICENSE := build/ports/busybox/stage/share/licenses/busybox/LICENSE
LINKS_INPUTS := $(wildcard ports/links/*.lua ports/links/Makefile ports/links/*.c ports/links/*.h \
                          ports/links/include/*.h ports/links/include/*/*.h ports/links/patches/*.patch) \
                ports/ports.lua ports/build.lua
LINKS_IMAGE := build/ports/links/stage/bin/links.pxe
LINKS_LICENSE := build/ports/links/stage/share/licenses/links/COPYING

TCC_INPUTS := $(wildcard ports/*.lua ports/tcc/*.lua ports/tcc/Makefile ports/tcc/patches/*.patch)
SDK_INPUTS := $(shell find build/sdk -type f)
FASTFETCH_INPUTS := $(wildcard ports/fastfetch/*.lua ports/fastfetch/*.cmake \
  ports/fastfetch/Makefile ports/fastfetch/PORT-NOTICE ports/fastfetch/patches/*.patch) \
  ports/ports.lua ports/build.lua ports/LICENSE
FASTFETCH_OUTPUTS := $(addprefix build/ports/fastfetch/stage/,bin/fastfetch.pxe \
  share/licenses/fastfetch/LICENSE share/licenses/fastfetch/yyjson.h \
  share/licenses/fastfetch/MPL-2.0 share/licenses/fastfetch/PORT-NOTICE)
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

CA_CERTIFICATES_INPUTS := $(wildcard ports/ca-certificates/*) ports/ports.lua ports/build.lua
CA_CERTIFICATES_OUTPUTS := $(addprefix build/ports/ca-certificates/stage/,\
  share/ca-certificates/cacert.pem share/ca-certificates/cacert.pem.sha256 \
  share/ca-certificates/source.txt share/licenses/ca-certificates/LICENSE \
  share/licenses/ca-certificates/NOTICE)

PCIIDS_INPUTS := $(wildcard ports/pciids/*) ports/ports.lua ports/build.lua
PCIIDS_OUTPUTS := $(addprefix build/ports/pciids/stage/,share/hwdata/pci.ids \
  share/pciids/source.txt share/licenses/pciids/LICENSE share/licenses/pciids/NOTICE)

USBIDS_INPUTS := $(wildcard ports/usbids/*) ports/ports.lua ports/build.lua
USBIDS_OUTPUTS := $(addprefix build/ports/usbids/stage/,share/hwdata/usb.ids \
  share/usbids/source.txt share/licenses/usbids/LICENSE share/licenses/usbids/NOTICE)

SBASE_INPUTS := $(wildcard ports/sbase/*.lua ports/sbase/Makefile \
                          ports/sbase/patches/*.patch) ports/ports.lua ports/build.lua
SBASE_OUTPUTS := $(addprefix build/ports/sbase/stage/,bin/cksum.pxe bin/tee.pxe \
  bin/uniq.pxe bin/sha256sum.pxe bin/wc.pxe bin/tail.pxe bin/sort.pxe \
  share/licenses/sbase/LICENSE share/licenses/sbase/arg.h \
  share/licenses/sbase/strtonum.c share/licenses/sbase/memmem.c \
  share/licenses/sbase/reallocarray.c share/licenses/sbase/queue.h \
  share/licenses/sbase/unicode-license.txt)

PICOHTTPPARSER_INPUTS := $(wildcard ports/picohttpparser/*.lua ports/picohttpparser/Makefile) ports/ports.lua ports/build.lua
PICOHTTPPARSER_OUTPUTS := $(addprefix build/ports/picohttpparser/stage/,dev/include/picohttpparser.h dev/lib/libpicohttpparser.a share/licenses/picohttpparser/picohttpparser.h)

ZLIB_INPUTS := $(wildcard ports/zlib/*.lua ports/zlib/Makefile ports/zlib/PORT-NOTICE) \
               ports/ports.lua ports/build.lua
ZLIB_OUTPUTS := $(addprefix build/ports/zlib/stage/,dev/lib/libz.a \
  dev/include/zlib.h dev/include/zconf.h share/licenses/zlib/LICENSE \
  share/licenses/zlib/PORT-NOTICE share/zlib/source.txt)

LIBPNG_INPUTS := $(wildcard ports/libpng/*.lua ports/libpng/Makefile \
                          ports/libpng/*.dfa ports/libpng/PORT-NOTICE) \
                 ports/ports.lua ports/build.lua
LIBPNG_OUTPUTS := $(addprefix build/ports/libpng/stage/,dev/lib/libpng.a \
  dev/include/png.h dev/include/pngconf.h dev/include/pnglibconf.h \
  share/licenses/libpng/LICENSE share/licenses/libpng/PORT-NOTICE \
  share/libpng/source.txt)

FMT_INPUTS := $(wildcard ports/fmt/*.lua ports/fmt/Makefile ports/fmt/*.cmake \
                        ports/fmt/patches/*.patch) ports/ports.lua ports/build.lua
FMT_OUTPUTS := $(addprefix build/ports/fmt/stage/,dev/lib/libfmt.a \
  dev/include/fmt/format.h dev/lib/cmake/fmt/fmt-config.cmake \
  dev/share/licenses/fmt/LICENSE dev/share/fmt/source.txt)

SDL2_INPUTS := $(wildcard ports/sdl2/*.lua ports/sdl2/Makefile ports/sdl2/SDL_config.h \
                         ports/sdl2/PORT-NOTICE ports/sdl2/pyxis/* ports/sdl2/cmake/* \
                         ports/sdl2/patches/*.patch) \
               ports/ports.lua ports/build.lua
SDL2_OUTPUTS := $(addprefix build/ports/sdl2/stage/,dev/lib/libSDL2.a \
  dev/include/SDL2/SDL.h dev/include/SDL2/SDL_config.h dev/lib/cmake/SDL2/SDL2Config.cmake \
  dev/share/licenses/sdl2/LICENSE.txt \
  dev/share/licenses/sdl2/PORT-NOTICE dev/share/sdl2/source.txt)

MBEDTLS_INPUTS := $(wildcard ports/mbedtls/*.lua ports/mbedtls/*.h ports/mbedtls/*.mk \
                           ports/mbedtls/*.cmake ports/mbedtls/Makefile \
                           ports/mbedtls/PORT-NOTICE) ports/ports.lua ports/build.lua
MBEDTLS_OUTPUTS := $(addprefix build/ports/mbedtls/stage/,dev/lib/libmbedtls.a \
  dev/lib/libmbedx509.a dev/lib/libtfpsacrypto.a dev/include/mbedtls/ssl.h \
  dev/include/psa/crypto.h dev/include/mbedtls/private_access.h \
  dev/include/mbedtls/pyxis_tls_config.h \
  dev/include/mbedtls/pyxis_crypto_config.h dev/share/mbedtls.mk \
  share/licenses/mbedtls/LICENSE share/licenses/tf-psa-crypto/LICENSE \
  share/licenses/mbedtls/PORT-NOTICE)

# DevilutionX is built only when DIABLO_DATA asks for it; see its PORT-NOTICE.
DEVILUTIONX_INPUTS := $(wildcard ports/devilutionx/*.lua ports/devilutionx/Makefile \
                                ports/devilutionx/PORT-NOTICE ports/devilutionx/patches/*.patch) \
                      ports/ports.lua ports/build.lua
DEVILUTIONX_OUTPUTS := $(addprefix build/ports/devilutionx/stage/,bin/devilutionx.pxe \
  share/devilutionx/assets/ui_art/diablo.pal share/devilutionx/source.txt \
  share/licenses/devilutionx/LICENSE.md share/licenses/devilutionx/PORT-NOTICE)

.PHONY: all
all: $(MBEDTLS_OUTPUTS) $(PICOHTTPPARSER_OUTPUTS) $(ZLIB_OUTPUTS) $(LIBPNG_OUTPUTS) $(FMT_OUTPUTS) $(SDL2_OUTPUTS) \
     $(DOOM_IMAGE) $(DOOM_LICENSE) $(QUAKE_IMAGE) $(QUAKE_LICENSE) \
     $(BUSYBOX_IMAGE) $(BUSYBOX_LICENSE) $(LINKS_IMAGE) $(LINKS_LICENSE) $(KILO_IMAGE) $(KILO_LICENSE) \
     $(LUA_IMAGE) $(LUA_LICENSE) $(LUA_DEVELOP) $(TCC_OUTPUTS) $(TZDATA_OUTPUTS) $(SBASE_OUTPUTS) \
     $(CA_CERTIFICATES_OUTPUTS) $(PCIIDS_OUTPUTS) $(USBIDS_OUTPUTS) $(FASTFETCH_OUTPUTS)
ifneq ($(DIABLO_DATA),)
all: $(DEVILUTIONX_OUTPUTS)
endif

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

$(LUA_IMAGE) $(LUA_LICENSE) $(LUA_DEVELOP) &: $(LUA_INPUTS) $(SDK_INPUTS) $(MBEDTLS_OUTPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/lua
	$(LUA) ports/build.lua lua --sdk $(abspath build/sdk) --work $(abspath build/ports/lua) --mbedtls $(abspath build/ports/mbedtls/stage/dev)

$(DOOM_IMAGE) $(DOOM_LICENSE) &: $(DOOM_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/doom
	$(LUA) ports/build.lua doom --sdk $(abspath build/sdk) --work $(abspath build/ports/doom)

$(QUAKE_IMAGE) $(QUAKE_LICENSE) &: $(QUAKE_INPUTS) $(SDK_INPUTS) scripts/ports.mk
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

$(BUSYBOX_IMAGE) $(BUSYBOX_LICENSE) &: $(BUSYBOX_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/busybox
	$(LUA) ports/build.lua busybox --sdk $(abspath build/sdk) --work $(abspath build/ports/busybox)

$(LINKS_IMAGE) $(LINKS_LICENSE) &: $(LINKS_INPUTS) $(SDK_INPUTS) scripts/ports.mk
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

$(MBEDTLS_OUTPUTS) &: $(MBEDTLS_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/mbedtls
	$(LUA) ports/build.lua mbedtls --sdk $(abspath build/sdk) --work $(abspath build/ports/mbedtls)

$(FASTFETCH_OUTPUTS) &: $(FASTFETCH_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/fastfetch
	$(LUA) ports/build.lua fastfetch --sdk $(abspath build/sdk) --work $(abspath build/ports/fastfetch)

$(USBIDS_OUTPUTS) &: $(USBIDS_INPUTS) scripts/ports.mk
	rm -rf build/ports/usbids
	$(LUA) ports/build.lua usbids --sdk $(abspath build/sdk) --work $(abspath build/ports/usbids)
