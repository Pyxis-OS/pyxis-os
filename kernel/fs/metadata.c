#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/fs/metadata.h>
#include <kernel/object/clock.h>
#include <kernel/panic.h>

static uint64_t next_id = FILE_DOMAIN_PRIVATE_BYTES + 1;
static uint64_t ram_domain, archive_domain;

bool fs_metadata_allocate_id(uint64_t *id)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (next_id == UINT64_MAX) {
    return false;
  }
  *id = next_id++;
  return true;
}

void fs_metadata_touch(struct fs_metadata *metadata)
{
  int64_t now = 0;
  metadata->modified_valid = clock_wall_nanoseconds(&now);
  metadata->modified_ns = now;
}

bool fs_metadata_create(struct fs_metadata *metadata, bool archive)
{
  *metadata = (struct fs_metadata){0};
  uint64_t *domain = archive ? &archive_domain : &ram_domain;
  if ((!*domain && !fs_metadata_allocate_id(domain)) ||
      !fs_metadata_allocate_id(&metadata->object)) {
    return false;
  }
  metadata->domain = *domain;
  if (!archive) {
    fs_metadata_touch(metadata);
  }
  return true;
}

void fs_metadata_mtime(int64_t nanoseconds, struct file_info_reply *reply)
{
  const int64_t per_second = INT64_C(1000000000);
  int64_t seconds = nanoseconds / per_second;
  int64_t remainder = nanoseconds % per_second;
  if (remainder < 0) {
    --seconds;
    remainder += per_second;
  }
  reply->valid |= FILE_INFO_MTIME_VALID;
  reply->modified_seconds = seconds;
  reply->modified_nanoseconds = (uint64_t)remainder;
}

void fs_metadata_info(const struct fs_metadata *metadata, struct file_info_reply *reply)
{
  reply->valid |= FILE_INFO_DOMAIN_VALID | FILE_INFO_OBJECT_VALID;
  reply->domain = metadata->domain;
  reply->object = metadata->object;
  if (metadata->modified_valid) {
    fs_metadata_mtime(metadata->modified_ns, reply);
  }
}
