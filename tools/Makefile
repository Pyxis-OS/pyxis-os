HOSTCC ?= cc
CPPFLAGS := -I../include
CFLAGS := -std=gnu23 -O2 -g -Wall -Wextra -Wshadow -Wstrict-prototypes

.PHONY: all remote log gdb-bridge machine-hostname clean
all: remote log

remote: ../build/tools/pyxis-remote

log: ../build/tools/pyxis-log

gdb-bridge: ../build/tools/pyxis-gdb-bridge

../build/tools/pyxis-gdb-bridge: gdb-bridge/main.c gdb-bridge/rsp.c gdb-bridge/rsp.h ../include/debug/protocol.h remote/sha256.h remote/vendor/sha256.c
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) gdb-bridge/main.c gdb-bridge/rsp.c remote/vendor/sha256.c -o $@

machine-hostname: ../build/tools/machine-hostname

../build/tools/machine-hostname: machine-hostname.c ../build/sdk/share/pyxis/machine_settings/machine_settings.h ../build/sdk/share/pyxis/machine_settings/machine_settings.c
	@mkdir -p $(@D)
	$(HOSTCC) -I../build/sdk/share/pyxis/machine_settings $(CFLAGS) $< ../build/sdk/share/pyxis/machine_settings/machine_settings.c -o $@

../build/tools/pyxis-log: log.c ../include/remote/log.h
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) $< -o $@

../build/tools/pyxis-remote: remote/main.c remote/listen.c remote/listen.h remote/presentation.c remote/transfer.c remote/paste.c remote/paste.h remote/transfer.h remote/sha256.h remote/vendor/sha256.c remote/presentation.h remote/buffer.h ../include/remote/terminal.h ../include/remote/beacon.h
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) remote/main.c remote/listen.c remote/presentation.c remote/transfer.c remote/paste.c remote/vendor/sha256.c -o $@

clean:
	rm -rf ../build/tools
