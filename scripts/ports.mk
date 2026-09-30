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

SBASE_INPUTS := $(wildcard ports/sbase/*.lua ports/sbase/Makefile \
                          ports/sbase/patches/*.patch) ports/ports.lua ports/build.lua
SBASE_OUTPUTS := $(addprefix build/ports/sbase/stage/,bin/cksum.pxe bin/tee.pxe \
  share/licenses/sbase/LICENSE share/licenses/sbase/arg.h)

PICOHTTPPARSER_INPUTS := $(wildcard ports/picohttpparser/*.lua ports/picohttpparser/Makefile) ports/ports.lua ports/build.lua
PICOHTTPPARSER_OUTPUTS := $(addprefix build/ports/picohttpparser/stage/,dev/include/picohttpparser.h dev/lib/libpicohttpparser.a share/licenses/picohttpparser/picohttpparser.h)

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

.PHONY: all
all: $(MBEDTLS_OUTPUTS) $(PICOHTTPPARSER_OUTPUTS) $(DOOM_IMAGE) $(DOOM_LICENSE) $(KILO_IMAGE) $(KILO_LICENSE) \
     $(LUA_IMAGE) $(LUA_LICENSE) $(LUA_DEVELOP) $(TCC_OUTPUTS) $(TZDATA_OUTPUTS) $(SBASE_OUTPUTS) \
     $(CA_CERTIFICATES_OUTPUTS) $(FASTFETCH_OUTPUTS)

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

# Pinned trust data is independent of target SDK contents.
$(CA_CERTIFICATES_OUTPUTS) &: $(CA_CERTIFICATES_INPUTS) scripts/ports.mk
	rm -rf build/ports/ca-certificates
	$(LUA) ports/build.lua ca-certificates --sdk $(abspath build/sdk) --work $(abspath build/ports/ca-certificates)

$(SBASE_OUTPUTS) &: $(SBASE_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	@command -v $(LUA) >/dev/null 2>&1 || { \
	  echo 'Missing Lua 5.4: install it or set LUA=lua5.4.' >&2; exit 1; }
	rm -rf build/ports/sbase
	$(LUA) ports/build.lua sbase --sdk $(abspath build/sdk) --work $(abspath build/ports/sbase)

$(PICOHTTPPARSER_OUTPUTS) &: $(PICOHTTPPARSER_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/picohttpparser
	$(LUA) ports/build.lua picohttpparser --sdk $(abspath build/sdk) --work $(abspath build/ports/picohttpparser)

$(MBEDTLS_OUTPUTS) &: $(MBEDTLS_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/mbedtls
	$(LUA) ports/build.lua mbedtls --sdk $(abspath build/sdk) --work $(abspath build/ports/mbedtls)

$(FASTFETCH_OUTPUTS) &: $(FASTFETCH_INPUTS) $(SDK_INPUTS) scripts/ports.mk
	rm -rf build/ports/fastfetch
	$(LUA) ports/build.lua fastfetch --sdk $(abspath build/sdk) --work $(abspath build/ports/fastfetch)
