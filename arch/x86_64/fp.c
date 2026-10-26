#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/fp.h>
#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <stdatomic.h>

#define CPUID_XSTATE 0xdu
#define CPUID_EXTENDED_FLAGS 7u
#define CPUID_FEATURE_XSAVE (1u << 26)
#define CPUID_FEATURE_AVX (1u << 28)
#define CPUID_FEATURE_FMA (1u << 12)
#define CPUID_FEATURE_F16C (1u << 29)
#define CPUID_FEATURE_AVX2 (1u << 5)
#define CPUID_FEATURE_VAES (1u << 9)
#define CPUID_FEATURE_VPCLMULQDQ (1u << 10)
#define CPUID_FEATURE_GFNI (1u << 8)
#define CPUID_FEATURE_AVX_VNNI (1u << 4)
#define CPUID_FEATURE_AVX_IFMA (1u << 23)
#define CPUID_FEATURE_AVX_VNNI_INT8 (1u << 4)
#define CPUID_FEATURE_AVX_NE_CONVERT (1u << 5)
#define CPUID_FEATURE_AVX_VNNI_INT16 (1u << 10)
#define CPUID_EXTENDED_MAX 0x80000000u
#define CPUID_FEATURE_XOP (1u << 11)
#define CPUID_FEATURE_FMA4 (1u << 16)
#define CR4_OSXSAVE (UINT64_C(1) << 18)
#define XSTATE_X87 (1u << 0)
#define XSTATE_SSE (1u << 1)
#define XSTATE_AVX (1u << 2)
#define XSTATE_SELECTED (XSTATE_X87 | XSTATE_SSE | XSTATE_AVX)
#define XSTATE_SUPERVISOR (1u << 0)
#define CPUID_FEATURE_XSAVEOPT (1u << 0)
#define CPUID_FEATURE_XSAVEC (1u << 1)
#define CPUID_FEATURE_XGETBV1 (1u << 2)
#define FXSAVE_BYTES 512u
#define XSAVE_HEADER_BYTES 64u
#define XSAVE_ALIGNMENT 64u
#define FP_STARTUP_TIMEOUT_NS UINT64_C(5000000000)

static bool use_xsave;
static size_t state_bytes = FXSAVE_BYTES;
static atomic_bool selected;

static void cpuid_state(uint32_t leaf, uint32_t subleaf, uint32_t *eax,
    uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
  __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                   : "a"(leaf), "c"(subleaf));
}

static void enable_xsave(void)
{
  write_cr4(read_cr4() | CR4_OSXSAVE);
  __asm__ volatile("xsetbv" : : "c"(0), "a"(XSTATE_SELECTED), "d"(0) : "memory");
}

void arch_fp_detect(void)
{
  struct arch_fp_cpu *fp = &cpu_current()->fp;
  atomic_init(&fp->configured, false);
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_VENDOR, &eax, &ebx, &ecx, &edx);
  if (eax < CPUID_XSTATE) {
    return;
  }
  cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
  if ((ecx & (CPUID_FEATURE_XSAVE | CPUID_FEATURE_AVX)) !=
      (CPUID_FEATURE_XSAVE | CPUID_FEATURE_AVX)) {
    return;
  }
  fp->features1 = ecx & (CPUID_FEATURE_AVX | CPUID_FEATURE_FMA | CPUID_FEATURE_F16C);
  cpuid_state(CPUID_EXTENDED_FLAGS, 0, &eax, &ebx, &ecx, &edx);
  fp->features7 = (ebx & CPUID_FEATURE_AVX2) |
      (ecx & (CPUID_FEATURE_GFNI | CPUID_FEATURE_VAES | CPUID_FEATURE_VPCLMULQDQ));
  if (eax >= 1) {
    cpuid_state(CPUID_EXTENDED_FLAGS, 1, &eax, &ebx, &ecx, &edx);
    fp->features71 = eax & (CPUID_FEATURE_AVX_VNNI | CPUID_FEATURE_AVX_IFMA);
    fp->features71d = edx & (CPUID_FEATURE_AVX_VNNI_INT8 | CPUID_FEATURE_AVX_NE_CONVERT |
        CPUID_FEATURE_AVX_VNNI_INT16);
  }
  cpuid(CPUID_EXTENDED_MAX, &eax, &ebx, &ecx, &edx);
  if (eax >= CPUID_EXTENDED_FEATURES) {
    cpuid(CPUID_EXTENDED_FEATURES, &eax, &ebx, &ecx, &edx);
    fp->extended_features = ecx & (CPUID_FEATURE_XOP | CPUID_FEATURE_FMA4);
  }
  cpuid_state(CPUID_XSTATE, 0, &eax, &ebx, &ecx, &edx);
  if ((eax & XSTATE_SELECTED) != XSTATE_SELECTED) {
    return;
  }
  uint32_t maximum = ecx;
  /* OSXSAVE also unmasks optional user state-management instructions. */
  cpuid_state(CPUID_XSTATE, 1, &eax, &ebx, &ecx, &edx);
  fp->save_features = eax &
      (CPUID_FEATURE_XSAVEOPT | CPUID_FEATURE_XSAVEC | CPUID_FEATURE_XGETBV1);
  cpuid_state(CPUID_XSTATE, 2, &eax, &ebx, &ecx, &edx);
  if (!eax || ebx < FXSAVE_BYTES + XSAVE_HEADER_BYTES ||
      eax > UINT32_MAX - ebx || (ecx & XSTATE_SUPERVISOR) || edx) {
    return;
  }
  fp->ymm_offset = ebx;
  fp->ymm_size = eax;
  enable_xsave();
  cpuid_state(CPUID_XSTATE, 0, &eax, &ebx, &ecx, &edx);
  if (ebx < fp->ymm_offset + fp->ymm_size || ebx > maximum) {
    write_cr4(read_cr4() & ~CR4_OSXSAVE);
    return;
  }
  fp->state_size = ebx;
  fp->supported = true;
}

static bool compatible(const struct arch_fp_cpu *a, const struct arch_fp_cpu *b)
{
  return a->supported && b->supported && a->features1 == b->features1 &&
      a->features7 == b->features7 && a->features71 == b->features71 &&
      a->features71d == b->features71d && a->extended_features == b->extended_features &&
      a->save_features == b->save_features &&
      a->ymm_offset == b->ymm_offset && a->ymm_size == b->ymm_size &&
      a->state_size == b->state_size;
}

static void apply_selected(void)
{
  if (!use_xsave) {
    write_cr4(read_cr4() & ~CR4_OSXSAVE);
  }
  atomic_store_explicit(&cpu_current()->fp.configured, true, memory_order_release);
}

void arch_fp_ap_wait(void)
{
  while (!atomic_load_explicit(&selected, memory_order_acquire)) {
    /* The bootstrap periodic timer is active, as in the scheduler's boot wait. */
    cpu_wait_interrupt();
  }
  apply_selected();
}

void arch_fp_finalize(void)
{
  KASSERT(arch_cpu_index() == 0);
  const struct arch_fp_cpu *bsp = &cpu_current()->fp;
  use_xsave = bsp->supported;
  size_t rejected = 0;
  for (size_t i = 1; i < arch_cpu_count() && use_xsave; ++i) {
    if (!compatible(bsp, &arch_cpu_at(i)->fp)) {
      use_xsave = false;
      rejected = i;
    }
  }
  if (use_xsave) {
    state_bytes = bsp->state_size;
    void *storage = state_bytes <= SIZE_MAX - (XSAVE_ALIGNMENT - 1) ?
        kmalloc(state_bytes + XSAVE_ALIGNMENT - 1) : NULL;
    if (!storage) {
      use_xsave = false;
    }
    kfree(storage);
  }
  if (!use_xsave) {
    state_bytes = FXSAVE_BYTES;
    kwarn("x86_64: XSAVE/AVX unavailable or incompatible at CPU %zu; FXSAVE retained\n",
          rejected);
  }
  atomic_store_explicit(&selected, true, memory_order_release);
  apply_selected();
  uint64_t deadline = arch_monotonic_ns() + FP_STARTUP_TIMEOUT_NS;
  for (size_t i = 1; i < arch_cpu_count(); ++i) {
    while (!atomic_load_explicit(&arch_cpu_at(i)->fp.configured, memory_order_acquire)) {
      if (arch_monotonic_ns() >= deadline) {
        panic("CPU %zu FP startup rendezvous timed out", i);
      }
      __asm__ volatile("pause");
    }
  }
  if (use_xsave) {
    kinfo("x86_64: eager XSAVE, XCR0=0x7, %zu bytes on %zu CPUs\n",
          state_bytes, arch_cpu_count());
  }
}

size_t arch_fp_size(void)
{
  KASSERT(atomic_load_explicit(&selected, memory_order_acquire));
  return state_bytes;
}

void arch_fp_save(void *area)
{
  if (use_xsave) {
    __asm__ volatile("xsave64 (%0)" : : "r"(area), "a"(XSTATE_SELECTED), "d"(0) : "memory");
  } else {
    __asm__ volatile("fxsave64 (%0)" : : "r"(area) : "memory");
  }
}

void arch_fp_restore(const void *area)
{
  if (use_xsave) {
    __asm__ volatile("xrstor64 (%0)" : : "r"(area), "a"(XSTATE_SELECTED), "d"(0) : "memory");
  } else {
    __asm__ volatile("fxrstor64 (%0)" : : "r"(area) : "memory");
  }
}
