# Pinned protocol engine and private Caelum port; no host include paths.
LWIP_CORE := init def inet_chksum ip mem memp netif pbuf stats sys \
             tcp tcp_in tcp_out timeouts ipv4/ip4 ipv4/ip4_addr
LWIP_SOURCES := $(addprefix third_party/lwip/src/core/,$(addsuffix .c,$(LWIP_CORE)))
LWIP_OBJECTS := $(patsubst %.c,build/%.o,$(LWIP_SOURCES)) \
                build/kernel/net/lwip/port.o build/kernel/net/lwip/bridge.o \
                build/kernel/net/lwip/connection.o build/kernel/net/lwip/identity.o \
                build/kernel/net/lwip/control.o build/kernel/net/lwip/receive.o \
                build/kernel/net/lwip/send.o \
                build/third_party/siphash/siphash.o
LWIP_INCLUDES := -Ikernel/net/lwip/include -Ithird_party/lwip/src/include -Ithird_party/siphash

OBJECTS += $(LWIP_OBJECTS)

$(LWIP_OBJECTS): CPPFLAGS += $(LWIP_INCLUDES)
# Compiler freestanding headers only: accidental host headers must fail here.
$(LWIP_OBJECTS): CFLAGS += -nostdinc -isystem $(shell $(CC) -print-file-name=include)
