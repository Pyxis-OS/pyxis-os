#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/descriptors.h>
#include <arch/hwp.h>
#include <arch/smp.h>
#include <kernel/log.h>

#define INTEL_VENDOR_EBX 0x756e6547u
#define INTEL_VENDOR_EDX 0x49656e69u
#define INTEL_VENDOR_ECX 0x6c65746eu
#define CPUID_POWER 6
#define CPUID_MSR (1u << 5)
#define CPUID_HWP (1u << 7)
#define CPUID_HWP_ACTIVITY (1u << 9)
#define CPUID_HWP_EPP (1u << 10)
#define CPUID_HWP_PACKAGE (1u << 11)
#define CPUID_HWP_FLEXIBLE (1u << 17)
#define CPUID_HWP_CONTROL (1u << 22)

#define IA32_PM_ENABLE 0x770
#define IA32_HWP_CAPABILITIES 0x771
#define IA32_HWP_REQUEST 0x774
#define IA32_HWP_CTL 0x776
#define HWP_ENABLE UINT64_C(1)
#define HWP_CAPABILITY_MASK UINT64_C(0xff)
#define HWP_LOWEST_SHIFT 24
#define HWP_MAXIMUM_SHIFT 8
#define HWP_EPP_SHIFT 24
#define HWP_EPP_BALANCED 128
#define HWP_REQUEST_PERFORMANCE_MASK UINT64_C(0xffffff)
#define HWP_REQUEST_EPP_MASK (UINT64_C(0xff) << HWP_EPP_SHIFT)
#define HWP_REQUEST_ACTIVITY_MASK (UINT64_C(0x3ff) << 32)
#define HWP_REQUEST_PACKAGE (UINT64_C(1) << 42)
#define HWP_REQUEST_VALID_MASK (UINT64_C(0x1f) << 59)
#define HWP_CTL_INVERT_PACKAGE UINT64_C(1)

extern bool arch_hwp_read_msr(uint32_t number, uint64_t *value);
extern bool arch_hwp_write_msr(uint32_t number, uint64_t value);
extern const char arch_hwp_read_site[], arch_hwp_read_failed[];
extern const char arch_hwp_write_site[], arch_hwp_write_failed[];

bool arch_hwp_fault(struct exception_frame *frame)
{
  if (frame->vector != EXCEPTION_GENERAL_PROTECTION || frame->error ||
      (frame->cs & SELECTOR_RPL_MASK)) {
    return false;
  }
  if (frame->rip == (uintptr_t)arch_hwp_read_site) {
    frame->rip = (uintptr_t)arch_hwp_read_failed;
    return true;
  }
  if (frame->rip == (uintptr_t)arch_hwp_write_site) {
    frame->rip = (uintptr_t)arch_hwp_write_failed;
    return true;
  }
  return false;
}

static bool read_setting(struct arch_hwp_state *state, enum arch_hwp_step step,
    uint32_t msr, uint64_t *value)
{
  state->step = step;
  state->msr = msr;
  return arch_hwp_read_msr(msr, value);
}

static bool write_setting(struct arch_hwp_state *state, enum arch_hwp_step step,
    uint32_t msr, uint64_t value)
{
  state->step = step;
  state->msr = msr;
  return arch_hwp_write_msr(msr, value);
}

void arch_hwp_init(void)
{
  struct arch_hwp_state *state = &cpu_current()->hwp;
  uint32_t eax, ebx, ecx, edx;
  cpuid(0, &eax, &ebx, &ecx, &edx);
  if (eax < CPUID_POWER || ebx != INTEL_VENDOR_EBX ||
      edx != INTEL_VENDOR_EDX || ecx != INTEL_VENDOR_ECX) {
    return;
  }
  cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
  if (!(edx & CPUID_MSR)) {
    return;
  }
  cpuid(CPUID_POWER, &eax, &ebx, &ecx, &edx);
  if (!(eax & CPUID_HWP)) {
    return;
  }
  state->features = eax;
  state->status = ARCH_HWP_FAILED;
  if (!read_setting(state, ARCH_HWP_READ_ENABLE, IA32_PM_ENABLE, &state->enable)) {
    return;
  }
  if (!(state->enable & HWP_ENABLE) &&
      !write_setting(state, ARCH_HWP_WRITE_ENABLE, IA32_PM_ENABLE, HWP_ENABLE)) {
    return;
  }
  if (!read_setting(state, ARCH_HWP_CONFIRM_ENABLE, IA32_PM_ENABLE, &state->enable) ||
      !(state->enable & HWP_ENABLE)) {
    return;
  }
  /* Capabilities/request/control are accessible only after HWP is enabled. */
  if (!read_setting(state, ARCH_HWP_READ_CAPABILITIES, IA32_HWP_CAPABILITIES,
        &state->capabilities)) {
    return;
  }
  uint64_t lowest = (state->capabilities >> HWP_LOWEST_SHIFT) & HWP_CAPABILITY_MASK;
  uint64_t highest = state->capabilities & HWP_CAPABILITY_MASK;
  state->step = ARCH_HWP_CHECK_CAPABILITIES;
  if (!highest || lowest > highest) {
    return;
  }
  if ((state->features & CPUID_HWP_CONTROL) &&
      !read_setting(state, ARCH_HWP_READ_CONTROL, IA32_HWP_CTL, &state->control)) {
    return;
  }
  state->step = ARCH_HWP_CHECK_CONTROL;
  if ((state->control & HWP_CTL_INVERT_PACKAGE) &&
      !(state->features & CPUID_HWP_PACKAGE)) {
    return;
  }
  uint64_t request = lowest | (highest << HWP_MAXIMUM_SHIFT);
  uint64_t mask = HWP_REQUEST_PERFORMANCE_MASK;
  if (state->features & CPUID_HWP_EPP) {
    request |= (uint64_t)HWP_EPP_BALANCED << HWP_EPP_SHIFT;
    mask |= HWP_REQUEST_EPP_MASK;
  }
  if (state->features & CPUID_HWP_ACTIVITY) {
    mask |= HWP_REQUEST_ACTIVITY_MASK;
  }
  if (state->features & CPUID_HWP_PACKAGE) {
    mask |= HWP_REQUEST_PACKAGE;
    /* Equal selector/polarity bits select local fields, including when
     * firmware inverted package control. Preserve HWP_CTL itself. */
    if (state->control & HWP_CTL_INVERT_PACKAGE) {
      request |= HWP_REQUEST_PACKAGE;
    }
  }
  if (state->features & CPUID_HWP_FLEXIBLE) {
    mask |= HWP_REQUEST_VALID_MASK;
  }
  state->request = request;
  if (!write_setting(state, ARCH_HWP_WRITE_REQUEST, IA32_HWP_REQUEST, request) ||
      !read_setting(state, ARCH_HWP_READ_REQUEST, IA32_HWP_REQUEST, &state->observed)) {
    return;
  }
  state->step = ARCH_HWP_CONFIRM_REQUEST;
  if ((state->observed ^ request) & mask) {
    return;
  }
  state->status = ARCH_HWP_APPLIED;
}

static const char *step_name(enum arch_hwp_step step)
{
  switch (step) {
  case ARCH_HWP_READ_ENABLE: return "enable-read";
  case ARCH_HWP_WRITE_ENABLE: return "enable-write";
  case ARCH_HWP_CONFIRM_ENABLE: return "enable-confirm";
  case ARCH_HWP_READ_CAPABILITIES: return "capabilities-read";
  case ARCH_HWP_CHECK_CAPABILITIES: return "capabilities-range";
  case ARCH_HWP_READ_CONTROL: return "control-read";
  case ARCH_HWP_CHECK_CONTROL: return "local-control";
  case ARCH_HWP_WRITE_REQUEST: return "request-write";
  case ARCH_HWP_READ_REQUEST: return "request-read";
  case ARCH_HWP_CONFIRM_REQUEST: return "request-confirm";
  }
  return "unknown";
}

void arch_hwp_report(void)
{
  size_t applied = 0, unavailable = 0, failed = 0, no_epp = 0;
  for (size_t index = 0; index < arch_cpu_count(); ++index) {
    const struct cpu_local *cpu = arch_cpu_at(index);
    const struct arch_hwp_state *state = &cpu->hwp;
    switch (state->status) {
    case ARCH_HWP_UNAVAILABLE:
      ++unavailable;
      break;
    case ARCH_HWP_APPLIED:
      ++applied;
      no_epp += !(state->features & CPUID_HWP_EPP);
      break;
    case ARCH_HWP_FAILED:
      ++failed;
      kwarn("x86_64: CPU %zu APIC %u HWP failed at %s (MSR 0x%x); boot continues\n",
          index, cpu->lapic_id, step_name(state->step), state->msr);
      break;
    }
  }
  kinfo("x86_64: HWP applied=%zu unavailable=%zu failed=%zu EPP-unavailable=%zu\n",
      applied, unavailable, failed, no_epp);
}
