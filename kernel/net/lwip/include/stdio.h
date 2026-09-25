#ifndef CAELUM_LWIP_STDIO_H
#define CAELUM_LWIP_STDIO_H

/* mem.c includes this unconditionally, but only its optional overflow checker
 * uses snprintf. The selected profile routes diagnostics through arch/cc.h. */
#if MEM_OVERFLOW_CHECK || MEMP_OVERFLOW_CHECK
#error "lwIP overflow diagnostics require an explicit freestanding snprintf port"
#endif

#endif
