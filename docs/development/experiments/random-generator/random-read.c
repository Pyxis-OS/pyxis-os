#include <clock.h>
#include <random.h>
#include <startup.h>
#include <stdint.h>
#include <stdio.h>

#define READS 512
#define WARMUP_READS 16
#define DEADLINE_NS UINT64_C(5000000000)

int main(void)
{
  handle_t clock = startup_resource("clock");
  handle_t random = startup_resource("random");
  if (clock == HANDLE_INVALID || random == HANDLE_INVALID) {
    puts("random baseline: missing clock/random grant");
    return 1;
  }

  const size_t lengths[] = {0, 1, 32, 256};
  uint8_t bytes[256];
  for (size_t size = 0; size < sizeof(lengths) / sizeof(lengths[0]); ++size) {
    uint64_t sum = 0, minimum = UINT64_MAX, maximum = 0;
    uint64_t start = 0, end = 0;
    for (size_t i = 0; i < WARMUP_READS + READS; ++i) {
      uint64_t before, after;
      if (clock_now(clock, &before) != CALL_OK || before > UINT64_MAX - DEADLINE_NS) {
        puts("random baseline: clock failed");
        return 1;
      }
      if (i == WARMUP_READS) {
        start = before;
      }
      enum call_status status = random_read(random, bytes, lengths[size], before + DEADLINE_NS);
      if (clock_now(clock, &after) != CALL_OK || after < before) {
        puts("random baseline: clock failed");
        return 1;
      }
      if (status != CALL_OK) {
        printf("random baseline: length=%zu iteration=%zu status=%u\n",
            lengths[size], i, (unsigned)status);
        return 1;
      }
      if (i >= WARMUP_READS) {
        uint64_t elapsed = after - before;
        sum += elapsed;
        if (elapsed < minimum) {
          minimum = elapsed;
        }
        if (elapsed > maximum) {
          maximum = elapsed;
        }
        end = after;
      }
    }
    printf("length=%zu reads=%u sum_ns=%llu mean_ns=%llu min_ns=%llu max_ns=%llu loop_ns=%llu\n",
        lengths[size], READS, (unsigned long long)sum,
        (unsigned long long)(sum / READS), (unsigned long long)minimum,
        (unsigned long long)maximum, (unsigned long long)(end - start));
  }
  return 0;
}
