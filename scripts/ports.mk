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
DOOM_INPUTS := $(wildcard ports/doom/*.lua ports/doom/Makefile ports/doom/manifest.json ports/doom/*.c \
                         ports/doom/patches/*.patch) ports/ports.lua ports/build.lua
QUAKE_INPUTS := $(wildcard ports/quake/*.lua ports/quake/Makefile ports/quake/manifest.json ports/quake/*.c \
                          ports/quake/*.h ports/quake/patches/*.patch) ports/ports.lua ports/build.lua
BLURGTEXT_INPUTS := $(wildcard ports/blurgtext/* ports/blurgtext/patches/* ports/blurgtext/notices/*) ports/ports.lua ports/build.lua
IOQUAKE3_INPUTS := $(wildcard ports/ioquake3/*.lua ports/ioquake3/Makefile ports/ioquake3/manifest.json \
                              ports/ioquake3/PORT-NOTICE ports/ioquake3/*.h ports/ioquake3/cmake/* ports/ioquake3/pyxis/* ports/ioquake3/patches/*.patch \
                              ports/ioquake3/notices/*) \
                    ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
OPENARENA_INPUTS := $(wildcard ports/openarena/*.lua ports/openarena/PORT-NOTICE ports/openarena/SOURCES.txt ports/openarena/*.patch) ports/ports.lua ports/build.lua
BUSYBOX_INPUTS := $(wildcard ports/busybox/*.lua ports/busybox/Makefile ports/busybox/*.c \
                            ports/busybox/*.h ports/busybox/patches/*.patch) ports/ports.lua ports/build.lua
LINKS_INPUTS := $(wildcard ports/links/*.lua ports/links/Makefile ports/links/*.c ports/links/*.h \
                          ports/links/include/*.h ports/links/include/*/*.h ports/links/patches/*.patch) \
                ports/ports.lua ports/build.lua

IMGUI_INPUTS := $(wildcard ports/imgui/* ports/imgui/cmake/* ports/imgui/patches/*.patch) ports/ports.lua ports/build.lua
IMGUI_OUTPUTS := $(call port_outputs,imgui)

LIBULTRASHIP_INPUTS := $(shell find ports/libultraship -type f) ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
LIBULTRASHIP_OUTPUTS := $(call port_outputs,libultraship)
LIBULTRASHIP_DEPS := imgui sdl2 mesa libzip zlib json tinyxml2 spdlog thread-pool prism monocypher stb dr_libs libogg libvorbis opus opusfile
empty :=
space := $(empty) $(empty)
LIBULTRASHIP_PREFIXES := $(subst $(space),;,$(foreach dep,$(LIBULTRASHIP_DEPS),$(abspath build/ports/$(dep)/stage/dev)));$(abspath build/ports/imgui/stage/sdl2-gl-dev);$(abspath build/ports/sdl2/stage/gl-dev)

HEXCURSE_INPUTS := $(wildcard ports/hexcurse/* ports/hexcurse/patches/*.patch) ports/ports.lua ports/build.lua

NCURSESW_INPUTS := $(wildcard ports/ncursesw/* ports/ncursesw/patches/*.patch) ports/ports.lua ports/build.lua

PKGCONF_INPUTS := $(wildcard ports/pkgconf/*.lua ports/pkgconf/Makefile ports/pkgconf/config.h \
                            ports/pkgconf/PORT-NOTICE ports/pkgconf/patches/*.patch) \
                  ports/ports.lua ports/build.lua

JQ_INPUTS := $(wildcard ports/jq/*.lua ports/jq/Makefile ports/jq/PORT-NOTICE ports/jq/patches/*.patch) \
             ports/ports.lua ports/build.lua

SDK_INPUTS := $(shell find build/sdk -type f)
FASTFETCH_INPUTS := $(wildcard ports/fastfetch/*.lua ports/fastfetch/*.cmake \
  ports/fastfetch/Makefile ports/fastfetch/PORT-NOTICE ports/fastfetch/patches/*.patch) \
  ports/ports.lua ports/build.lua ports/LICENSE

LUA_INPUTS := $(wildcard ports/lua/*.lua ports/lua/*.c ports/lua/*.h \
                        ports/lua/Makefile ports/lua/*.pc ports/lua/manifest.json ports/lua/patches/*.patch) ports/ports.lua ports/build.lua


TZDATA_INPUTS := $(wildcard ports/tzdata/*.lua) ports/ports.lua ports/build.lua

CA_CERTIFICATES_INPUTS := $(wildcard ports/ca-certificates/*) ports/ports.lua ports/build.lua

PCIIDS_INPUTS := $(wildcard ports/pciids/*) ports/ports.lua ports/build.lua

USBIDS_INPUTS := $(wildcard ports/usbids/*) ports/ports.lua ports/build.lua

FONTS_INPUTS := $(shell find ports/fonts -type f 2>/dev/null) \
                ports/ports.lua ports/build.lua $(wildcard ports/host/*)

SBASE_INPUTS := $(wildcard ports/sbase/*.lua ports/sbase/Makefile \
                          ports/sbase/patches/*.patch) ports/ports.lua ports/build.lua

PICOHTTPPARSER_INPUTS := $(wildcard ports/picohttpparser/*.lua ports/picohttpparser/Makefile ports/picohttpparser/*.pc) ports/ports.lua ports/build.lua

ZLIB_INPUTS := $(wildcard ports/zlib/*.lua ports/zlib/Makefile ports/zlib/*.pc ports/zlib/PORT-NOTICE) \
               ports/ports.lua ports/build.lua

LIBUV_INPUTS := $(wildcard ports/libuv/*.lua ports/libuv/Makefile ports/libuv/*.pc ports/libuv/PORT-NOTICE \
                         ports/libuv/include/uv/*.h ports/libuv/pyxis/*.c \
                         ports/libuv/pyxis/*.h ports/libuv/examples/* \
                         ports/libuv/patches/*.patch) ports/ports.lua ports/build.lua
LUA51_INPUTS := $(wildcard ports/lua51/*.lua ports/lua51/*.md ports/lua51/Makefile ports/lua51/*.pc ports/lua51/manifest.json \
                         ports/lua51/patches/*.patch) ports/ports.lua ports/build.lua ports/LICENSE

UTF8PROC_INPUTS := $(wildcard ports/utf8proc/*.lua ports/utf8proc/Makefile ports/utf8proc/*.pc ports/utf8proc/PORT-NOTICE) \
                   ports/ports.lua ports/build.lua
TREE_SITTER_INPUTS := $(wildcard ports/tree-sitter/*.lua ports/tree-sitter/Makefile ports/tree-sitter/*.pc \
                               ports/tree-sitter/PORT-NOTICE ports/tree-sitter/patches/*.patch) \
                     ports/ports.lua ports/build.lua
LUA_LANGUAGE_SERVER_INPUTS := $(shell find ports/lua-language-server -type f) \
                             ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md

NEOVIM_INPUTS := $(wildcard ports/neovim/*.lua ports/neovim/*.json ports/neovim/*.md \
                           ports/neovim/host/*.lua ports/neovim/runtime/*.vim \
                           ports/neovim/runtime/colors/*.vim ports/neovim/patches/*.patch) \
                $(shell find ports/neovim/runtime -type f) \
                ports/ports.lua ports/build.lua ports/LICENSE

LIBPNG_INPUTS := $(wildcard ports/libpng/*.lua ports/libpng/Makefile ports/libpng/*.pc \
                          ports/libpng/*.dfa ports/libpng/PORT-NOTICE) \
                 ports/ports.lua ports/build.lua

TINYXML2_INPUTS := $(wildcard ports/tinyxml2/*.lua ports/tinyxml2/Makefile ports/tinyxml2/*.pc ports/tinyxml2/*.h ports/tinyxml2/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
TINYXML2_INPUTS += $(wildcard ports/tinyxml2/README.md ports/tinyxml2/PORT-NOTICE \
  ports/tinyxml2/manual/*.c ports/tinyxml2/manual/*.cpp ports/tinyxml2/manual/Makefile \
  ports/tinyxml2/manual/CMakeLists.txt ports/tinyxml2/manual/README.md)
THREAD_POOL_INPUTS := $(wildcard ports/thread-pool/*.lua ports/thread-pool/Makefile ports/thread-pool/*.pc ports/thread-pool/*.h ports/thread-pool/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
THREAD_POOL_INPUTS += $(wildcard ports/thread-pool/README.md ports/thread-pool/PORT-NOTICE \
  ports/thread-pool/manual/*.c ports/thread-pool/manual/*.cpp ports/thread-pool/manual/Makefile \
  ports/thread-pool/manual/CMakeLists.txt ports/thread-pool/manual/README.md)
DR_LIBS_INPUTS := $(wildcard ports/dr_libs/*.lua ports/dr_libs/Makefile ports/dr_libs/*.pc ports/dr_libs/*.h ports/dr_libs/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
DR_LIBS_INPUTS += $(wildcard ports/dr_libs/README.md ports/dr_libs/PORT-NOTICE \
  ports/dr_libs/manual/*.c ports/dr_libs/manual/*.cpp ports/dr_libs/manual/Makefile \
  ports/dr_libs/manual/CMakeLists.txt ports/dr_libs/manual/README.md)
STB_INPUTS := $(wildcard ports/stb/*.lua ports/stb/Makefile ports/stb/*.pc ports/stb/*.h ports/stb/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
STB_INPUTS += $(wildcard ports/stb/README.md ports/stb/PORT-NOTICE \
  ports/stb/manual/*.c ports/stb/manual/*.cpp ports/stb/manual/Makefile \
  ports/stb/manual/CMakeLists.txt ports/stb/manual/README.md)
SPDLOG_INPUTS := $(wildcard ports/spdlog/*.lua ports/spdlog/Makefile ports/spdlog/*.pc ports/spdlog/*.h ports/spdlog/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
SPDLOG_INPUTS += $(wildcard ports/spdlog/README.md ports/spdlog/PORT-NOTICE \
  ports/spdlog/manual/*.c ports/spdlog/manual/*.cpp ports/spdlog/manual/Makefile \
  ports/spdlog/manual/CMakeLists.txt ports/spdlog/manual/README.md)
MONOCYPHER_INPUTS := $(wildcard ports/monocypher/*.lua ports/monocypher/Makefile ports/monocypher/*.pc ports/monocypher/*.h ports/monocypher/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
MONOCYPHER_INPUTS += $(wildcard ports/monocypher/README.md ports/monocypher/PORT-NOTICE \
  ports/monocypher/manual/*.c ports/monocypher/manual/*.cpp ports/monocypher/manual/Makefile \
  ports/monocypher/manual/CMakeLists.txt ports/monocypher/manual/README.md)
PRISM_INPUTS := $(wildcard ports/prism/*.lua ports/prism/Makefile ports/prism/*.pc ports/prism/*.h ports/prism/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
PRISM_INPUTS += $(wildcard ports/prism/README.md ports/prism/PORT-NOTICE \
  ports/prism/manual/*.c ports/prism/manual/*.cpp ports/prism/manual/Makefile \
  ports/prism/manual/CMakeLists.txt ports/prism/manual/README.md)
LIBZIP_INPUTS := $(wildcard ports/libzip/*.lua ports/libzip/Makefile ports/libzip/*.pc ports/libzip/*.h ports/libzip/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
LIBZIP_INPUTS += $(wildcard ports/libzip/README.md ports/libzip/PORT-NOTICE \
  ports/libzip/manual/*.c ports/libzip/manual/*.cpp ports/libzip/manual/Makefile \
  ports/libzip/manual/CMakeLists.txt ports/libzip/manual/README.md)
LIBOGG_INPUTS := $(wildcard ports/libogg/*.lua ports/libogg/Makefile ports/libogg/*.pc ports/libogg/*.h ports/libogg/*.py ports/libogg/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
LIBOGG_INPUTS += $(wildcard ports/libogg/README.md ports/libogg/PORT-NOTICE \
  ports/libogg/manual/*.c ports/libogg/manual/*.cpp ports/libogg/manual/Makefile \
  ports/libogg/manual/CMakeLists.txt ports/libogg/manual/README.md)
LIBVORBIS_INPUTS := $(wildcard ports/libvorbis/*.lua ports/libvorbis/Makefile ports/libvorbis/*.pc ports/libvorbis/*.h ports/libvorbis/*.py ports/libvorbis/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
LIBVORBIS_INPUTS += $(wildcard ports/libvorbis/README.md ports/libvorbis/PORT-NOTICE \
  ports/libvorbis/manual/*.c ports/libvorbis/manual/*.cpp ports/libvorbis/manual/Makefile \
  ports/libvorbis/manual/CMakeLists.txt ports/libvorbis/manual/README.md)
OPUS_INPUTS := $(wildcard ports/opus/*.lua ports/opus/Makefile ports/opus/*.pc ports/opus/*.h ports/opus/*.py ports/opus/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
OPUS_INPUTS += $(wildcard ports/opus/README.md ports/opus/PORT-NOTICE \
  ports/opus/manual/*.c ports/opus/manual/*.cpp ports/opus/manual/Makefile \
  ports/opus/manual/CMakeLists.txt ports/opus/manual/README.md)
OPUSFILE_INPUTS := $(wildcard ports/opusfile/*.lua ports/opusfile/Makefile ports/opusfile/*.pc ports/opusfile/patches/*.patch) \
                   ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
OPUSFILE_INPUTS += $(wildcard ports/opusfile/README.md ports/opusfile/PORT-NOTICE \
  ports/opusfile/manual/*.c ports/opusfile/manual/*.cpp ports/opusfile/manual/Makefile \
  ports/opusfile/manual/README.md)
JSON_INPUTS := $(wildcard ports/json/*.lua ports/json/Makefile ports/json/*.pc ports/json/patches/*.patch) \
               ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md
JSON_INPUTS += $(wildcard ports/json/README.md ports/json/PORT-NOTICE \
  ports/json/manual/*.c ports/json/manual/*.cpp ports/json/manual/Makefile \
  ports/json/manual/CMakeLists.txt ports/json/manual/README.md)
FMT_INPUTS := $(wildcard ports/fmt/*.lua ports/fmt/Makefile ports/fmt/*.pc ports/fmt/*.cmake \
                        ports/fmt/patches/*.patch) ports/ports.lua ports/build.lua

SDL2_INPUTS := $(wildcard ports/sdl2/*.lua ports/sdl2/Makefile ports/sdl2/*.pc ports/sdl2/SDL_config.h \
                         ports/sdl2/PORT-NOTICE ports/sdl2/pyxis/* ports/sdl2/cmake/* \
                         ports/sdl2/patches/*.patch) \
               ports/ports.lua ports/build.lua

MESA_INPUTS := $(wildcard ports/mesa/*.* ports/mesa/Makefile ports/mesa/PORT-NOTICE ports/mesa/manual/* \
                         ports/mesa/notices/* ports/mesa/patches/*.patch) \
               ports/ports.lua ports/build.lua ports/LICENSE ports/LICENSING.md

RAYLIB_INPUTS := $(wildcard ports/raylib/*.lua ports/raylib/Makefile ports/raylib/*.pc ports/raylib/*.h \
                           ports/raylib/PORT-NOTICE ports/raylib/THIRD-PARTY-LICENSES \
                           ports/raylib/cmake/* ports/raylib/examples/* ports/raylib/patches/*.patch) \
                 ports/ports.lua ports/build.lua

MINESWEEPER_INPUTS := $(wildcard ports/minesweeper/*.lua ports/minesweeper/Makefile \
                               ports/minesweeper/PORT-NOTICE ports/minesweeper/manifest.json \
                               ports/minesweeper/patches/*.patch) \
                     ports/ports.lua ports/build.lua

MBEDTLS_INPUTS := $(wildcard ports/mbedtls/*.lua ports/mbedtls/*.h ports/mbedtls/*.mk \
                           ports/mbedtls/*.cmake ports/mbedtls/Makefile ports/mbedtls/*.pc \
                           ports/mbedtls/PORT-NOTICE) ports/ports.lua ports/build.lua

CHOCOLATE_DOOM_INPUTS := $(wildcard ports/chocolate-doom/*.lua ports/chocolate-doom/Makefile \
                                   ports/chocolate-doom/PORT-NOTICE ports/chocolate-doom/manifest.json \
                                   ports/chocolate-doom/patches/*.patch) \
                         ports/ports.lua ports/build.lua

CHOCOLATE_QUAKE_INPUTS := $(wildcard ports/chocolate-quake/*.lua ports/chocolate-quake/Makefile \
                                    ports/chocolate-quake/PORT-NOTICE ports/chocolate-quake/manifest.json \
                                    ports/chocolate-quake/patches/*.patch) \
                          ports/ports.lua ports/build.lua

# EDuke32 is a personal-use program opt-in; see its PORT-NOTICE.
EDUKE32_INPUTS := $(wildcard ports/eduke32/*.lua ports/eduke32/Makefile \
                            ports/eduke32/PORT-NOTICE ports/eduke32/manifest.json \
                            ports/eduke32/patches/*.patch) \
                  ports/ports.lua ports/build.lua

# DevilutionX is a personal-use program opt-in; see its PORT-NOTICE.
DEVILUTIONX_INPUTS := $(wildcard ports/devilutionx/*.lua ports/devilutionx/Makefile \
                                ports/devilutionx/PORT-NOTICE ports/devilutionx/manifest.json \
                                ports/devilutionx/patches/*.patch) \
                      ports/ports.lua ports/build.lua

# Development archives retain these shared licence texts in their staged trees.
FMT_INPUTS += ports/LICENSE ports/LICENSING.md
LIBPNG_INPUTS += ports/LICENSE ports/LICENSING.md
LIBUV_INPUTS += ports/LICENSE ports/LICENSING.md
LUA_INPUTS += ports/LICENSE ports/LICENSING.md ports/lua/PORT-NOTICE
LUA51_INPUTS += ports/LICENSING.md
MBEDTLS_INPUTS += ports/LICENSE ports/LICENSING.md
PICOHTTPPARSER_INPUTS += ports/LICENSE ports/LICENSING.md
RAYLIB_INPUTS += ports/LICENSE ports/LICENSING.md
SDL2_INPUTS += ports/LICENSE ports/LICENSING.md
TREE_SITTER_INPUTS += ports/LICENSE ports/LICENSING.md
UTF8PROC_INPUTS += ports/LICENSE ports/LICENSING.md
ZLIB_INPUTS += ports/LICENSE ports/LICENSING.md

KILO_OUTPUTS := $(call port_outputs,kilo)
DOOM_OUTPUTS := $(call port_outputs,doom)
QUAKE_OUTPUTS := $(call port_outputs,quake)
IOQUAKE3_OUTPUTS := $(call port_outputs,ioquake3)
OPENARENA_OUTPUTS := $(call port_outputs,openarena)
BUSYBOX_OUTPUTS := $(call port_outputs,busybox)
LINKS_OUTPUTS := $(call port_outputs,links)
LUA_OUTPUTS := $(call port_outputs,lua)
JQ_OUTPUTS := $(call port_outputs,jq)
NCURSESW_OUTPUTS := $(call port_outputs,ncursesw)
HEXCURSE_OUTPUTS := $(call port_outputs,hexcurse)
PKGCONF_OUTPUTS := $(call port_outputs,pkgconf)
FASTFETCH_OUTPUTS := $(call port_outputs,fastfetch)
TZDATA_OUTPUTS := $(call port_outputs,tzdata)
CA_CERTIFICATES_OUTPUTS := $(call port_outputs,ca-certificates)
PCIIDS_OUTPUTS := $(call port_outputs,pciids)
USBIDS_OUTPUTS := $(call port_outputs,usbids)
FONTS_OUTPUTS := $(call port_outputs,fonts)
SBASE_OUTPUTS := $(call port_outputs,sbase)
PICOHTTPPARSER_OUTPUTS := $(call port_outputs,picohttpparser)
ZLIB_OUTPUTS := $(call port_outputs,zlib)
LIBUV_OUTPUTS := $(call port_outputs,libuv)
LUA51_OUTPUTS := $(call port_outputs,lua51)
UTF8PROC_OUTPUTS := $(call port_outputs,utf8proc)
TREE_SITTER_OUTPUTS := $(call port_outputs,tree-sitter)
NEOVIM_OUTPUTS := $(call port_outputs,neovim)
LUA_LANGUAGE_SERVER_OUTPUTS := $(call port_outputs,lua-language-server)
LIBPNG_OUTPUTS := $(call port_outputs,libpng)
TINYXML2_OUTPUTS := $(call port_outputs,tinyxml2)
THREAD_POOL_OUTPUTS := $(call port_outputs,thread-pool)
DR_LIBS_OUTPUTS := $(call port_outputs,dr_libs)
STB_OUTPUTS := $(call port_outputs,stb)
SPDLOG_OUTPUTS := $(call port_outputs,spdlog)
MONOCYPHER_OUTPUTS := $(call port_outputs,monocypher)
PRISM_OUTPUTS := $(call port_outputs,prism)
LIBZIP_OUTPUTS := $(call port_outputs,libzip)
LIBOGG_OUTPUTS := $(call port_outputs,libogg)
LIBVORBIS_OUTPUTS := $(call port_outputs,libvorbis)
OPUS_OUTPUTS := $(call port_outputs,opus)
OPUSFILE_OUTPUTS := $(call port_outputs,opusfile)
JSON_OUTPUTS := $(call port_outputs,json)
FMT_OUTPUTS := $(call port_outputs,fmt)
SDL2_OUTPUTS := $(call port_outputs,sdl2)
BLURGTEXT_OUTPUTS := $(call port_outputs,blurgtext)
MESA_OUTPUTS := $(call port_outputs,mesa)
RAYLIB_OUTPUTS := $(call port_outputs,raylib)
MINESWEEPER_OUTPUTS := $(call port_outputs,minesweeper)
WPA_RSN_INPUTS := $(wildcard ports/wpa-supplicant/* ports/wpa-supplicant/patches/*.patch) ports/ports.lua ports/build.lua
WPA_RSN_OUTPUTS := $(call port_outputs,wpa-supplicant)
MBEDTLS_OUTPUTS := $(call port_outputs,mbedtls)
DEVILUTIONX_OUTPUTS := $(call port_outputs,devilutionx)
CHOCOLATE_DOOM_OUTPUTS := $(call port_outputs,chocolate-doom)
CHOCOLATE_QUAKE_OUTPUTS := $(call port_outputs,chocolate-quake)
EDUKE32_OUTPUTS := $(call port_outputs,eduke32)

.PHONY: all
all: $(TINYXML2_OUTPUTS)
all: $(THREAD_POOL_OUTPUTS)
all: $(DR_LIBS_OUTPUTS)
all: $(STB_OUTPUTS)
all: $(SPDLOG_OUTPUTS)
all: $(MONOCYPHER_OUTPUTS)
all: $(PRISM_OUTPUTS)
all: $(LIBZIP_OUTPUTS)
all: $(LIBOGG_OUTPUTS)
all: $(LIBVORBIS_OUTPUTS)
all: $(OPUS_OUTPUTS)
all: $(OPUSFILE_OUTPUTS)
all: $(JSON_OUTPUTS)
all: $(WPA_RSN_OUTPUTS) $(MBEDTLS_OUTPUTS) $(PICOHTTPPARSER_OUTPUTS) $(ZLIB_OUTPUTS) $(LIBPNG_OUTPUTS) $(FMT_OUTPUTS) $(SDL2_OUTPUTS) \
     $(BUSYBOX_OUTPUTS) $(LINKS_OUTPUTS) $(JQ_OUTPUTS) $(PKGCONF_OUTPUTS) $(KILO_OUTPUTS) $(LUA_OUTPUTS) \
     $(TZDATA_OUTPUTS) $(SBASE_OUTPUTS) $(CA_CERTIFICATES_OUTPUTS) $(PCIIDS_OUTPUTS) \
     $(USBIDS_OUTPUTS) $(FASTFETCH_OUTPUTS)
all: $(IMGUI_OUTPUTS) $(LIBULTRASHIP_OUTPUTS)
all: $(NCURSESW_OUTPUTS) $(HEXCURSE_OUTPUTS)
all: $(RAYLIB_OUTPUTS) $(LIBUV_OUTPUTS) $(LUA51_OUTPUTS) $(UTF8PROC_OUTPUTS) $(TREE_SITTER_OUTPUTS) $(NEOVIM_OUTPUTS)
all: $(LUA_LANGUAGE_SERVER_OUTPUTS)

# This work tree is disposable build output. Port edits belong in ports/kilo,
# not the fetched source copy, which is replaced when its inputs change.
$(KILO_OUTPUTS) &: $(KILO_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; \
	  exit 1; }
	rm -rf build/ports/kilo
	$(LUA) ports/build.lua kilo --sdk $(abspath build/sdk) --work $(abspath build/ports/kilo)

$(LUA_OUTPUTS) &: $(LUA_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/lua
	$(LUA) ports/build.lua lua --sdk $(abspath build/sdk) --work $(abspath build/ports/lua)

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

.PHONY: fonts
fonts: $(FONTS_OUTPUTS)

# The port outputs each game package packs; only make game-packages asks for them.
.PHONY: game-ports-doom game-ports-quake game-ports-minesweeper game-ports-openarena game-ports-duke3d game-ports-diablo
game-ports-doom: $(DOOM_OUTPUTS) $(CHOCOLATE_DOOM_OUTPUTS)
game-ports-quake: $(QUAKE_OUTPUTS) $(CHOCOLATE_QUAKE_OUTPUTS)
game-ports-minesweeper: $(MINESWEEPER_OUTPUTS)
game-ports-openarena: $(IOQUAKE3_OUTPUTS) $(OPENARENA_OUTPUTS)
game-ports-duke3d: $(EDUKE32_OUTPUTS)
game-ports-diablo: $(DEVILUTIONX_OUTPUTS)

# Font files and notices are data; SDK content does not change their bytes.
$(FONTS_OUTPUTS) &: $(FONTS_INPUTS) scripts/ports.mk
	rm -rf build/ports/fonts
	$(LUA) ports/build.lua fonts --sdk $(abspath build/sdk) --work $(abspath build/ports/fonts)

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

$(PKGCONF_OUTPUTS) &: $(PKGCONF_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@printf '\033[1;32m  PORT\033[0m  pkgconf\n'
	rm -rf build/ports/pkgconf
	$(LUA) ports/build.lua pkgconf --sdk $(abspath build/sdk) --work $(abspath build/ports/pkgconf)

$(JQ_OUTPUTS) &: $(JQ_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/jq
	$(LUA) ports/build.lua jq --sdk $(abspath build/sdk) --work $(abspath build/ports/jq)

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

$(LUA51_OUTPUTS) &: $(LUA51_INPUTS) $(SDK_INPUTS) $(LIBUV_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/lua51
	$(LUA) ports/build.lua lua51 --sdk $(abspath build/sdk) --work $(abspath build/ports/lua51) --libuv $(abspath build/ports/libuv/stage/dev)

$(UTF8PROC_OUTPUTS) &: $(UTF8PROC_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/utf8proc
	$(LUA) ports/build.lua utf8proc --sdk $(abspath build/sdk) --work $(abspath build/ports/utf8proc)

$(TREE_SITTER_OUTPUTS) &: $(TREE_SITTER_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/tree-sitter
	$(LUA) ports/build.lua tree-sitter --sdk $(abspath build/sdk) --work $(abspath build/ports/tree-sitter)

$(NEOVIM_OUTPUTS) &: $(NEOVIM_INPUTS) $(SDK_INPUTS) $(LUA51_OUTPUTS) $(LIBUV_OUTPUTS) \
                    $(UTF8PROC_OUTPUTS) $(TREE_SITTER_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/neovim
	$(LUA) ports/build.lua neovim --sdk $(abspath build/sdk) --work $(abspath build/ports/neovim) \
	  --lua51 $(abspath build/ports/lua51/stage/dev) --libuv $(abspath build/ports/libuv/stage/dev) \
	  --utf8proc $(abspath build/ports/utf8proc/stage/dev) --tree-sitter $(abspath build/ports/tree-sitter/stage/dev)

$(LUA_LANGUAGE_SERVER_OUTPUTS) &: $(LUA_LANGUAGE_SERVER_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/lua-language-server
	$(LUA) ports/build.lua lua-language-server --sdk $(abspath build/sdk) --work $(abspath build/ports/lua-language-server)

$(LIBPNG_OUTPUTS) &: $(LIBPNG_INPUTS) $(SDK_INPUTS) $(ZLIB_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/libpng
	$(LUA) ports/build.lua libpng --sdk $(abspath build/sdk) --work $(abspath build/ports/libpng) --zlib $(abspath build/ports/zlib/stage/dev)

$(TINYXML2_OUTPUTS) &: $(TINYXML2_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/tinyxml2
	$(LUA) ports/build.lua tinyxml2 --sdk $(abspath build/sdk) --work $(abspath build/ports/tinyxml2)

$(THREAD_POOL_OUTPUTS) &: $(THREAD_POOL_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/thread-pool
	$(LUA) ports/build.lua thread-pool --sdk $(abspath build/sdk) --work $(abspath build/ports/thread-pool)

$(DR_LIBS_OUTPUTS) &: $(DR_LIBS_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/dr_libs
	$(LUA) ports/build.lua dr_libs --sdk $(abspath build/sdk) --work $(abspath build/ports/dr_libs)

$(STB_OUTPUTS) &: $(STB_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/stb
	$(LUA) ports/build.lua stb --sdk $(abspath build/sdk) --work $(abspath build/ports/stb)

$(SPDLOG_OUTPUTS) &: $(SPDLOG_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/spdlog
	$(LUA) ports/build.lua spdlog --sdk $(abspath build/sdk) --work $(abspath build/ports/spdlog)

$(MONOCYPHER_OUTPUTS) &: $(MONOCYPHER_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/monocypher
	$(LUA) ports/build.lua monocypher --sdk $(abspath build/sdk) --work $(abspath build/ports/monocypher)

$(PRISM_OUTPUTS) &: $(PRISM_INPUTS) $(SDK_INPUTS) $(SPDLOG_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/prism
	$(LUA) ports/build.lua prism --sdk $(abspath build/sdk) --work $(abspath build/ports/prism) --spdlog $(abspath build/ports/spdlog/stage/dev)

$(LIBZIP_OUTPUTS) &: $(LIBZIP_INPUTS) $(SDK_INPUTS) $(ZLIB_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/libzip
	$(LUA) ports/build.lua libzip --sdk $(abspath build/sdk) --work $(abspath build/ports/libzip) --zlib $(abspath build/ports/zlib/stage/dev)

$(LIBOGG_OUTPUTS) &: $(LIBOGG_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/libogg
	$(LUA) ports/build.lua libogg --sdk $(abspath build/sdk) --work $(abspath build/ports/libogg)

$(LIBVORBIS_OUTPUTS) &: $(LIBVORBIS_INPUTS) $(SDK_INPUTS) $(LIBOGG_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/libvorbis
	$(LUA) ports/build.lua libvorbis --sdk $(abspath build/sdk) --work $(abspath build/ports/libvorbis) --ogg $(abspath build/ports/libogg/stage/dev)

$(OPUS_OUTPUTS) &: $(OPUS_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/opus
	$(LUA) ports/build.lua opus --sdk $(abspath build/sdk) --work $(abspath build/ports/opus)

$(OPUSFILE_OUTPUTS) &: $(OPUSFILE_INPUTS) $(SDK_INPUTS) $(LIBOGG_OUTPUTS) $(OPUS_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/opusfile
	$(LUA) ports/build.lua opusfile --sdk $(abspath build/sdk) --work $(abspath build/ports/opusfile) --ogg $(abspath build/ports/libogg/stage/dev) --opus $(abspath build/ports/opus/stage/dev)

$(JSON_OUTPUTS) &: $(JSON_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/json
	$(LUA) ports/build.lua json --sdk $(abspath build/sdk) --work $(abspath build/ports/json)

$(FMT_OUTPUTS) &: $(FMT_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/fmt
	$(LUA) ports/build.lua fmt --sdk $(abspath build/sdk) --work $(abspath build/ports/fmt)

$(SDL2_OUTPUTS) &: $(SDL2_INPUTS) $(SDK_INPUTS) $(MESA_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/sdl2
	$(LUA) ports/build.lua sdl2 --sdk $(abspath build/sdk) --work $(abspath build/ports/sdl2) \
	  --mesa $(abspath build/ports/mesa/stage/dev)

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

$(WPA_RSN_OUTPUTS) &: $(WPA_RSN_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/wpa-supplicant
	$(LUA) ports/build.lua wpa-supplicant --sdk $(abspath build/sdk) --work $(abspath build/ports/wpa-supplicant)

$(RAYLIB_OUTPUTS) &: $(RAYLIB_INPUTS) $(SDK_INPUTS) $(SDL2_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/raylib
	$(LUA) ports/build.lua raylib --sdk $(abspath build/sdk) --work $(abspath build/ports/raylib) --sdl2 $(abspath build/ports/sdl2/stage/dev)

$(MINESWEEPER_OUTPUTS) &: $(MINESWEEPER_INPUTS) $(SDK_INPUTS) $(RAYLIB_OUTPUTS) $(SDL2_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/minesweeper
	$(LUA) ports/build.lua minesweeper --sdk $(abspath build/sdk) \
	  --work $(abspath build/ports/minesweeper) --raylib $(abspath build/ports/raylib/stage/dev) \
	  --sdl2 $(abspath build/ports/sdl2/stage/dev)

$(NCURSESW_OUTPUTS) &: $(NCURSESW_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/ncursesw
	$(LUA) ports/build.lua ncursesw --sdk $(abspath build/sdk) --work $(abspath build/ports/ncursesw)

# Go server builds are explicit; no default archive or aggregate port dependency.
.PHONY: caddy
caddy:
	@test -n "$(GO_PREFIX)" || { echo 'Caddy requires GO_PREFIX=ABSOLUTE_DIR' >&2; exit 1; }
	@test "$$(sed -n 's/^commit=//p' "$(GO_PREFIX)/share/pyxis-go/revision")" = \
	       "$$(sed -n 's/^go_commit=//p' toolchain/build-go.sh)" || { \
	  echo 'Caddy requires the Go revision pinned in toolchain/build-go.sh' >&2; exit 1; }
	rm -rf build/ports/caddy
	$(LUA) ports/build.lua caddy --sdk $(abspath build/sdk) --work $(abspath build/ports/caddy) --go "$(GO_PREFIX)"

$(HEXCURSE_OUTPUTS) &: $(HEXCURSE_INPUTS) $(SDK_INPUTS) $(NCURSESW_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/hexcurse
	$(LUA) ports/build.lua hexcurse --sdk $(abspath build/sdk) --work $(abspath build/ports/hexcurse) \
	  --ncursesw $(abspath build/ports/ncursesw/stage/dev)

# SDL's two build profiles share the Mesa development prefix; ordinary SDL
# consumers still link only the independent software archive.
.PHONY: mesa
mesa: $(MESA_OUTPUTS)
$(MESA_OUTPUTS) &: $(MESA_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/mesa
	$(LUA) ports/build.lua mesa --sdk $(abspath build/sdk) --work $(abspath build/ports/mesa)

$(IOQUAKE3_OUTPUTS) &: $(IOQUAKE3_INPUTS) $(SDK_INPUTS) $(SDL2_OUTPUTS) $(MESA_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/ioquake3
	$(LUA) ports/build.lua ioquake3 --sdk $(abspath build/sdk) --work $(abspath build/ports/ioquake3) \
	  --sdl2 $(abspath build/ports/sdl2/stage/dev) --sdl2-osmesa $(abspath build/ports/sdl2/stage/gl-dev) \
	  --mesa $(abspath build/ports/mesa/stage/dev)

$(OPENARENA_OUTPUTS) &: $(OPENARENA_INPUTS) scripts/ports.mk
	rm -rf build/ports/openarena
	$(LUA) ports/build.lua openarena --sdk $(abspath build/sdk) --work $(abspath build/ports/openarena)

.PHONY: blurgtext
blurgtext: $(BLURGTEXT_OUTPUTS)
$(BLURGTEXT_OUTPUTS) &: $(BLURGTEXT_INPUTS) $(SDK_INPUTS) $(SDL2_OUTPUTS) $(MESA_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/blurgtext
	$(LUA) ports/build.lua blurgtext --sdk $(abspath build/sdk) --work $(abspath build/ports/blurgtext) \
	  --sdl2 $(abspath build/ports/sdl2/stage/dev) --sdl2-osmesa $(abspath build/ports/sdl2/stage/gl-dev) \
	  --mesa $(abspath build/ports/mesa/stage/dev)

$(IMGUI_OUTPUTS) &: $(IMGUI_INPUTS) $(SDK_INPUTS) $(SDL2_OUTPUTS) $(MESA_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/imgui
	$(LUA) ports/build.lua imgui --sdk $(abspath build/sdk) --work $(abspath build/ports/imgui) \
	  --sdl2 $(abspath build/ports/sdl2/stage/dev) --sdl2-osmesa $(abspath build/ports/sdl2/stage/gl-dev) \
	  --mesa $(abspath build/ports/mesa/stage/dev)

.PHONY: libultraship
libultraship: $(LIBULTRASHIP_OUTPUTS)
$(LIBULTRASHIP_OUTPUTS) &: $(LIBULTRASHIP_INPUTS) $(SDK_INPUTS) $(IMGUI_OUTPUTS) $(SDL2_OUTPUTS) $(MESA_OUTPUTS) \
  $(LIBZIP_OUTPUTS) $(ZLIB_OUTPUTS) $(JSON_OUTPUTS) $(TINYXML2_OUTPUTS) $(SPDLOG_OUTPUTS) $(THREAD_POOL_OUTPUTS) \
  $(PRISM_OUTPUTS) $(MONOCYPHER_OUTPUTS) $(STB_OUTPUTS) $(DR_LIBS_OUTPUTS) $(LIBOGG_OUTPUTS) $(LIBVORBIS_OUTPUTS) \
  $(OPUS_OUTPUTS) $(OPUSFILE_OUTPUTS) scripts/ports.mk
	rm -rf build/ports/libultraship
	$(LUA) ports/build.lua libultraship --sdk $(abspath build/sdk) --work $(abspath build/ports/libultraship) \
	  --dependencies '$(LIBULTRASHIP_PREFIXES)'
