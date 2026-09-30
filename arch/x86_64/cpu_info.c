#include <arch/cpu.h>
#include <arch/cpu_info.h>
#include <kernel/memory.h>
#include <kernel/string.h>

#define CPUID_EXTENDED_MAX UINT32_C(0x80000000)
#define CPUID_BRAND_FIRST UINT32_C(0x80000002)
#define CPUID_BRAND_LAST UINT32_C(0x80000004)
#define CPUID_LEAF_BYTES 16

void
arch_cpu_brand(char brand[ARCH_CPU_BRAND_BYTES + 1])
{
  memset(brand, 0, ARCH_CPU_BRAND_BYTES + 1);
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_EXTENDED_MAX, &eax, &ebx, &ecx, &edx);
  if (eax < CPUID_BRAND_LAST) {
    return;
  }
  for (uint32_t leaf = CPUID_BRAND_FIRST; leaf <= CPUID_BRAND_LAST; ++leaf) {
    uint32_t words[4];
    cpuid(leaf, &words[0], &words[1], &words[2], &words[3]);
    memcpy(brand + (leaf - CPUID_BRAND_FIRST) * CPUID_LEAF_BYTES, words, sizeof(words));
  }
  /* CPUID brands can be space padded. Keep absent/all-space brands explicit. */
  size_t start = 0;
  while (brand[start] == ' ') {
    ++start;
  }
  size_t length = strlen(brand + start);
  while (length && brand[start + length - 1] == ' ') {
    --length;
  }
  memmove(brand, brand + start, length);
  memset(brand + length, 0, ARCH_CPU_BRAND_BYTES + 1 - length);
}
