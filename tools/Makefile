HOSTCC ?= cc
CPPFLAGS := -I../include
CFLAGS := -std=gnu23 -O2 -g -Wall -Wextra -Wshadow -Wstrict-prototypes

.PHONY: all elf2pxe clean
all: elf2pxe

elf2pxe: ../build/tools/elf2pxe

../build/tools/elf2pxe: elf2pxe.c ../include/pxe/p1f.h
	@mkdir -p $(@D)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) $< -o $@

clean:
	rm -rf ../build/tools
