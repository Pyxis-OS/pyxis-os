#include <arch/cpu.h>
#include <arch/random.h>

#define CPUID_STRUCTURED_FEATURES 7
#define CPUID_FEATURE_RDRAND (1u << 30)
#define CPUID_FEATURE_RDSEED (1u << 18)
#define RDSEED_ATTEMPTS 32u
#define RDRAND_ATTEMPTS 10u
#define CPU_RANDOM_SELF_TEST_WORDS 4u

enum cpu_random_instruction { CPU_RDRAND, CPU_RDSEED, CPU_RANDOM_INSTRUCTIONS };

/* Only the BSP entropy worker accesses feature and health state. History spans
 * the boot self-test, calls and partial tails; rejected bytes never escape. */
static struct {
  bool supported[CPU_RANDOM_INSTRUCTIONS];
  bool previous_valid[CPU_RANDOM_INSTRUCTIONS];
  uint64_t previous[CPU_RANDOM_INSTRUCTIONS];
  bool last_valid, failed;
  uint64_t last;
} cpu_random;

static enum arch_random_result read_word(enum cpu_random_instruction instruction,
    uint64_t *word)
{
  if (cpu_random.failed) {
    return ARCH_RANDOM_FAILED;
  }
  if (!cpu_random.supported[instruction]) {
    return ARCH_RANDOM_EMPTY;
  }
  unsigned attempts = instruction == CPU_RDSEED ? RDSEED_ATTEMPTS : RDRAND_ATTEMPTS;
  for (unsigned i = 0; i < attempts; ++i) {
    uint64_t value;
    bool ready;
    if (instruction == CPU_RDSEED) {
      __asm__ volatile("rdseed %0" : "=r"(value), "=@ccc"(ready));
    } else {
      __asm__ volatile("rdrand %0" : "=r"(value), "=@ccc"(ready));
    }
    if (!ready) {
      __asm__ volatile("pause");
      continue;
    }
    if (!value || value == UINT64_MAX ||
        (cpu_random.previous_valid[instruction] &&
         value == cpu_random.previous[instruction]) ||
        (cpu_random.last_valid && value == cpu_random.last)) {
      cpu_random.failed = true;
      return ARCH_RANDOM_FAILED;
    }
    cpu_random.previous_valid[instruction] = true;
    cpu_random.previous[instruction] = value;
    cpu_random.last_valid = true;
    cpu_random.last = value;
    *word = value;
    return ARCH_RANDOM_OK;
  }
  return ARCH_RANDOM_EMPTY;
}

bool arch_random_init(void)
{
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_VENDOR, &eax, &ebx, &ecx, &edx);
  uint32_t maximum = eax;
  if (maximum >= CPUID_BASIC_FEATURES) {
    cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
    cpu_random.supported[CPU_RDRAND] = (ecx & CPUID_FEATURE_RDRAND) != 0;
  }
  if (maximum >= CPUID_STRUCTURED_FEATURES) {
    cpuid(CPUID_STRUCTURED_FEATURES, &eax, &ebx, &ecx, &edx);
    cpu_random.supported[CPU_RDSEED] = (ebx & CPUID_FEATURE_RDSEED) != 0;
  }
  if (!cpu_random.supported[CPU_RDRAND] && !cpu_random.supported[CPU_RDSEED]) {
    return false;
  }
  for (unsigned instruction = 0; instruction < CPU_RANDOM_INSTRUCTIONS; ++instruction) {
    if (!cpu_random.supported[instruction]) {
      continue;
    }
    for (unsigned i = 0; i < CPU_RANDOM_SELF_TEST_WORDS; ++i) {
      uint64_t word;
      if (read_word(instruction, &word) != ARCH_RANDOM_OK) {
        cpu_random.failed = true;
        return false;
      }
    }
  }
  return true;
}

bool arch_random_has_rdseed(void)
{
  return cpu_random.supported[CPU_RDSEED];
}

enum arch_random_result arch_random_word(uint64_t *word)
{
  enum arch_random_result result = read_word(CPU_RDSEED, word);
  if (result == ARCH_RANDOM_EMPTY) {
    result = read_word(CPU_RDRAND, word);
  }
  return result;
}
