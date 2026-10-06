HOSTCC ?= cc
CPPFLAGS := -I../include
CFLAGS := -std=gnu23 -O2 -g -Wall -Wextra -Wshadow -Wstrict-prototypes

.PHONY: all elf2pxe remote clean
all: elf2pxe

elf2pxe: ../build/tools/elf2pxe

remote: ../build/tools/pyxis-remote

../build/tools/pyxis-remote: remote/main.c remote/presentation.c remote/transfer.c remote/transfer.h remote/sha256.h remote/vendor/sha256.c remote/presentation.h remote/buffer.h ../include/remote/terminal.h
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) remote/main.c remote/presentation.c remote/transfer.c remote/vendor/sha256.c -o $@

../build/tools/elf2pxe: elf2pxe.c ../include/pxe/p1f.h
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) $< -o $@

clean:
	rm -rf ../build/tools
