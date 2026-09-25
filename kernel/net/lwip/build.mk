# Optional build/link investigation. The ordinary kernel and ISO do not include
# this profile, and no boot code initializes it or changes packet dispatch.
LWIP_CORE := init def inet_chksum ip mem memp netif pbuf stats sys \
             tcp tcp_in tcp_out timeouts ipv4/ip4 ipv4/ip4_addr
LWIP_SOURCES := $(addprefix third_party/lwip/src/core/,$(addsuffix .c,$(LWIP_CORE)))
LWIP_OBJECTS := $(patsubst %.c,build/%.o,$(LWIP_SOURCES)) build/kernel/net/lwip/port.o
LWIP_INCLUDES := -Ikernel/net/lwip/include -Ithird_party/lwip/src/include

.PHONY: lwip-port
lwip-port: build/caelum-lwip.elf

$(LWIP_OBJECTS): CPPFLAGS += $(LWIP_INCLUDES)
# Compiler freestanding headers only: accidental host headers must fail here.
$(LWIP_OBJECTS): CFLAGS += -nostdinc -isystem $(shell $(CC) -print-file-name=include)

build/caelum-lwip.elf: $(OBJECTS) $(LWIP_OBJECTS) arch/x86_64/linker.ld
	$(CC) $(subst build/caelum.map,build/caelum-lwip.map,$(LDFLAGS)) -o $@ $(OBJECTS) $(LWIP_OBJECTS)

-include $(LWIP_OBJECTS:.o=.d)
