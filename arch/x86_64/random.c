#include <arch/cpu.h>
#include <arch/random.h>
#include <kernel/log.h>

#define CPUID_STRUCTURED_FEATURES 7
#define CPUID_FEATURE_RDRAND (1u << 30)
#define CPUID_FEATURE_RDSEED (1u << 18)
#define RDSEED_ATTEMPTS 32u
#define RDRAND_ATTEMPTS 10u
#define CPU_RANDOM_SELF_TEST_WORDS 4u

/* Only the BSP entropy worker accesses feature and health state. History spans
 * the boot self-test, calls and partial tails; rejected bytes never escape. */
static struct {
  bool supported[ARCH_RANDOM_INSTRUCTIONS];
  bool disabled[ARCH_RANDOM_INSTRUCTIONS];
  bool previous_valid[ARCH_RANDOM_INSTRUCTIONS];
  uint64_t previous[ARCH_RANDOM_INSTRUCTIONS];
  bool last_valid;
  enum arch_random_instruction last_instruction;
  uint64_t last;
} cpu_random;

bool arch_random_enabled(enum arch_random_instruction instruction)
{
  return cpu_random.supported[instruction] && !cpu_random.disabled[instruction];
}

static void disable_instruction(enum arch_random_instruction instruction, const char *reason)
{
  if (!cpu_random.disabled[instruction]) {
    cpu_random.disabled[instruction] = true;
    klog("random: %s %s; disabled until reboot\n",
        instruction == ARCH_RANDOM_RDSEED ? "RDSEED" : "RDRAND", reason);
  }
}

static enum arch_random_result read_word(enum arch_random_instruction instruction,
    uint64_t *word)
{
  if (!arch_random_enabled(instruction)) {
    return ARCH_RANDOM_EMPTY;
  }
  unsigned attempts = instruction == ARCH_RANDOM_RDSEED ? RDSEED_ATTEMPTS : RDRAND_ATTEMPTS;
  for (unsigned i = 0; i < attempts; ++i) {
    uint64_t value;
    bool ready;
    if (instruction == ARCH_RANDOM_RDSEED) {
      __asm__ volatile("rdseed %0" : "=r"(value), "=@ccc"(ready));
    } else {
      __asm__ volatile("rdrand %0" : "=r"(value), "=@ccc"(ready));
    }
    if (!ready) {
      __asm__ volatile("pause");
      continue;
    }
    if (cpu_random.last_valid && cpu_random.last_instruction != instruction &&
        value == cpu_random.last) {
      disable_instruction(ARCH_RANDOM_RDRAND, "cross-instruction repeat");
      disable_instruction(ARCH_RANDOM_RDSEED, "cross-instruction repeat");
      return ARCH_RANDOM_FAILED;
    }
    if (!value || value == UINT64_MAX ||
        (cpu_random.previous_valid[instruction] &&
         value == cpu_random.previous[instruction])) {
      disable_instruction(instruction, "health check failed");
      return ARCH_RANDOM_FAILED;
    }
    cpu_random.previous_valid[instruction] = true;
    cpu_random.previous[instruction] = value;
    cpu_random.last_valid = true;
    cpu_random.last_instruction = instruction;
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
    cpu_random.supported[ARCH_RANDOM_RDRAND] = (ecx & CPUID_FEATURE_RDRAND) != 0;
  }
  if (maximum >= CPUID_STRUCTURED_FEATURES) {
    cpuid(CPUID_STRUCTURED_FEATURES, &eax, &ebx, &ecx, &edx);
    cpu_random.supported[ARCH_RANDOM_RDSEED] = (ebx & CPUID_FEATURE_RDSEED) != 0;
  }
  if (!cpu_random.supported[ARCH_RANDOM_RDRAND] && !cpu_random.supported[ARCH_RANDOM_RDSEED]) {
    return false;
  }
  for (unsigned instruction = 0; instruction < ARCH_RANDOM_INSTRUCTIONS; ++instruction) {
    if (!arch_random_enabled(instruction)) {
      continue;
    }
    for (unsigned i = 0; i < CPU_RANDOM_SELF_TEST_WORDS; ++i) {
      uint64_t word;
      if (read_word(instruction, &word) != ARCH_RANDOM_OK) {
        disable_instruction(instruction, "failed boot self-test");
        break;
      }
    }
  }
  return arch_random_enabled(ARCH_RANDOM_RDSEED) || arch_random_enabled(ARCH_RANDOM_RDRAND);
}

enum arch_random_result arch_random_word(uint64_t *word)
{
  enum arch_random_result result = read_word(ARCH_RANDOM_RDSEED, word);
  if (result == ARCH_RANDOM_EMPTY) {
    result = read_word(ARCH_RANDOM_RDRAND, word);
  }
  return result;
}
