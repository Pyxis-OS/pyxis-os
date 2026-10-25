HOSTCC ?= cc
HOSTAR ?= ar
CPPFLAGS := -I../include
CFLAGS := -std=gnu23 -O2 -g -Wall -Wextra -Wshadow -Wstrict-prototypes

SHA256_ROOT := ../userspace
SHA256_SOURCES := libsha256/sha256.c third_party/tf_psa_sha256/core.c
ifneq ($(filter x86_64% amd64%,$(shell $(HOSTCC) -dumpmachine)),)
SHA256_SOURCES += libsha256/arch/x86_64/sha_ni.c libsha256/arch/x86_64/dispatch.c
SHA256_CFLAGS := -march=x86-64 -mno-avx -mno-avx2 -mno-avx512f
else
SHA256_SOURCES += libsha256/arch/portable/dispatch.c
endif
SHA256_OBJECTS := $(patsubst %.c,../build/tools/sha256/%.o,$(SHA256_SOURCES))
SHA256_LIBRARY := ../build/tools/libsha256.a
SHA256_NOTICES := ../build/tools/licenses/libsha256/.stamp
SHA256_LICENSE_SOURCES := LICENSE third_party/tf_psa_sha256/LICENSE \
                         third_party/tf_psa_sha256/UPSTREAM.md \
                         libsha256/arch/x86_64/LICENSE.go-bsd libsha256/arch/x86_64/UPSTREAM.md

.PHONY: all remote log gdb-bridge machine-hostname clean
all: remote log

remote: ../build/tools/pyxis-remote

log: ../build/tools/pyxis-log

gdb-bridge: ../build/tools/pyxis-gdb-bridge

../build/tools/pyxis-gdb-bridge: gdb-bridge/main.c gdb-bridge/rsp.c gdb-bridge/rsp.h ../include/debug/protocol.h $(SHA256_ROOT)/include/sha256.h $(SHA256_LIBRARY) | $(SHA256_NOTICES)
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) -I$(SHA256_ROOT)/include $(CFLAGS) gdb-bridge/main.c gdb-bridge/rsp.c $(SHA256_LIBRARY) -o $@

machine-hostname: ../build/tools/machine-hostname

../build/tools/machine-hostname: machine-hostname.c ../build/sdk/share/pyxis/machine_settings/machine_settings.h ../build/sdk/share/pyxis/machine_settings/machine_settings.c
	@mkdir -p $(@D)
	$(HOSTCC) -I../build/sdk/share/pyxis/machine_settings $(CFLAGS) $< ../build/sdk/share/pyxis/machine_settings/machine_settings.c -o $@

../build/tools/pyxis-log: log.c ../include/remote/log.h ../include/abi/log.h
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) $< -o $@

../build/tools/pyxis-remote: remote/main.c remote/listen.c remote/listen.h remote/presentation.c remote/transfer.c remote/paste.c remote/paste.h remote/transfer.h remote/presentation.h remote/buffer.h ../include/remote/terminal.h ../include/remote/beacon.h ../include/abi/process.h ../include/abi/syscall.h ../include/terminal/style.h ../lib/terminal_style.c ../include/terminal/text.h ../lib/terminal_text.c $(SHA256_ROOT)/include/sha256.h $(SHA256_LIBRARY) | $(SHA256_NOTICES)
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) -I$(SHA256_ROOT)/include $(CFLAGS) remote/main.c remote/listen.c remote/presentation.c remote/transfer.c remote/paste.c $(SHA256_LIBRARY) ../lib/terminal_style.c ../lib/terminal_text.c -o $@

../build/tools/sha256/%.o: $(SHA256_ROOT)/%.c $(SHA256_ROOT)/include/sha256.h $(SHA256_ROOT)/libsha256/internal.h Makefile
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) -I$(SHA256_ROOT)/include $(CFLAGS) $(SHA256_CFLAGS) -c $< -o $@

$(SHA256_LIBRARY): $(SHA256_OBJECTS) Makefile
	@mkdir -p $(@D)
	rm -f $@
	$(HOSTAR) rcs $@ $(SHA256_OBJECTS)

$(SHA256_NOTICES): $(addprefix $(SHA256_ROOT)/,$(SHA256_LICENSE_SOURCES)) Makefile
	@mkdir -p $(@D)
	@for source in $(SHA256_LICENSE_SOURCES); do \
	  mkdir -p "$(@D)/$$(dirname "$$source")"; \
	  cp "$(SHA256_ROOT)/$$source" "$(@D)/$$source" || exit; \
	done
	@touch $@

clean:
	rm -rf ../build/tools
