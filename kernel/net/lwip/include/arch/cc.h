#ifndef CAELUM_LWIP_CC_H
#define CAELUM_LWIP_CC_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/log.h>
#include <kernel/panic.h>

/* All lwIP access is serialized by one BSP task with IF=1, including callbacks and
 * frees. These hooks do not make the raw API safe for APs or interrupt entry. */
void caelum_lwip_assert_context(void);
void *caelum_lwip_malloc(size_t bytes);
void *caelum_lwip_calloc(size_t count, size_t bytes);
void caelum_lwip_free(void *pointer);

#define LWIP_ASSERT_CORE_LOCKED() caelum_lwip_assert_context()
#define LWIP_PLATFORM_ASSERT(message) panic("lwIP: %s (%s:%d)", message, __FILE__, __LINE__)
#define LWIP_PLATFORM_DIAG(arguments) klog arguments
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_NO_LIMITS_H 1
#define INT_MAX __INT_MAX__
#define X8_F "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "zu"

#endif
