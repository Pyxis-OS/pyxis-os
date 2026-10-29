#include <abi/describe.h>
#include <abi/file.h>
#include <kernel/object/capability.h>
#include <kernel/object/describe.h>
#include <kernel/mm/shared.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/file.h>
#include <kernel/object/forwarder.h>
#include <kernel/memory.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

bool description_string_valid(const char *text, size_t length, size_t capacity)
{
  if (length >= capacity) {
    return false;
  }
  for (size_t offset = 0; offset < length;) {
    uint32_t value = (uint8_t)text[offset++];
    if (!value) {
      return false;
    }
    if (value < 0x80) {
      continue;
    }
    unsigned trailing;
    uint32_t minimum;
    if (value >= 0xc2 && value <= 0xdf) {
      trailing = 1;
      minimum = 0x80;
      value &= 0x1f;
    } else if (value >= 0xe0 && value <= 0xef) {
      trailing = 2;
      minimum = 0x800;
      value &= 0x0f;
    } else if (value >= 0xf0 && value <= 0xf4) {
      trailing = 3;
      minimum = 0x10000;
      value &= 7;
    } else {
      return false;
    }
    if (trailing > length - offset) {
      return false;
    }
    while (trailing--) {
      uint8_t byte = text[offset++];
      if ((byte & 0xc0) != 0x80) {
        return false;
      }
      value = (value << 6) | (byte & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
      return false;
    }
  }
  for (size_t i = length; i < capacity; ++i) {
    if (text[i]) {
      return false;
    }
  }
  return true;
}


static enum call_status metadata_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_LIMIT:
  case CAP_FULL: return CALL_LIMIT;
  default: return CALL_BAD_REQUEST;
  }
}

static enum call_status describe_object(struct kernel_object *object,
    struct describe_reply *reply, struct file_object **body)
{
  if (object->type == OBJECT_ENDPOINT_EXPORT) {
    enum call_status status = endpoint_description_capture(object,
        &reply->description, body);
    if (status == CALL_ENDPOINT_CLOSED) {
      reply->description = (struct resource_description){.outcome = RESOURCE_IO_ERROR};
      return CALL_OK;
    }
    return status;
  }
  if (object->type == OBJECT_SHARED_REGION) {
    reply->description.flags = RESOURCE_LENGTH_KNOWN;
    reply->description.length = shared_region_size(shared_region_from_object(object));
  } else if (object->type == OBJECT_FILE) {
    struct file_object *file = (struct file_object *)object;
    file_description(file, &reply->description, &reply->properties);
  }
  return CALL_OK;
}

struct syscall_result describe_call(uint64_t handle, uintptr_t destination,
    uint64_t capacity, uint64_t flags)
{
  if (capacity < sizeof(struct describe_reply)) {
    return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
  }
  if (flags & ~DESCRIBE_INCLUDE_ERROR_BODY) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct process *process = process_current();
  if (!process) {
    return (struct syscall_result){CALL_BAD_HANDLE, 0};
  }
  struct capability_reference reference;
  enum capability_result result = capability_acquire_metadata(&process->capabilities,
      handle, &reference);
  if (result != CAP_OK) {
    return (struct syscall_result){metadata_status(result), 0};
  }
  if (!user_buffer_check(destination, sizeof(struct describe_reply), USER_BUFFER_WRITE)) {
    capability_release(&reference);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct describe_reply reply = {
    .protocol = object_protocol(reference.object),
    .kind = object_effective_type(reference.object) == OBJECT_ENDPOINT_EXPORT ?
        HANDLE_KIND_EXPORTED : HANDLE_KIND_NATIVE,
    .rights = reference.rights,
    .transport = reference.transport,
  };
  struct kernel_object *object = reference.object;
  struct kernel_object *leaf = NULL;
  struct file_object *body = NULL;
  enum call_status status = CALL_OK;
  uint64_t restrictions = reference.restrictions;
  if (object->type == OBJECT_FORWARDER) {
    uint64_t lineage = 0;
    result = forwarder_snapshot(object, &leaf, &lineage);
    restrictions |= lineage;
    if (result == CAP_DENIED) {
      reply.description.outcome = RESOURCE_DENIED;
    } else if (result != CAP_OK) {
      status = metadata_status(result);
    } else if (!leaf) {
      reply.description.outcome = RESOURCE_NOT_FOUND;
    } else {
      status = describe_object(leaf, &reply,
          (flags & DESCRIBE_INCLUDE_ERROR_BODY) ? &body : NULL);
    }
  } else {
    status = describe_object(object, &reply,
        (flags & DESCRIBE_INCLUDE_ERROR_BODY) ? &body : NULL);
  }
  if (leaf) {
    object_release(leaf);
  }
  capability_release(&reference);
  if (restrictions & HANDLE_RESTRICT_DETAIL) {
    /* Outcome, flags, length, media type and the error body stay visible. */
    memset(reply.description.detail, 0, sizeof(reply.description.detail));
    reply.description.detail_size = 0;
    reply.properties |= DESCRIBE_DETAIL_WITHHELD;
  }

  struct capability_grant grant = {0};
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (status == CALL_OK && body) {
    /* These are the issuer's copied bytes, not a new OPEN child. Keeping a body
     * cannot keep an export alive or let provider exit/CUT erase those bytes. */
    result = capability_grant_retain(&body->object, FILE_RIGHT_READ, 0, &grant);
    if (result == CAP_OK) {
      result = capability_validate_grants(&process->capabilities, &grant, 1);
    }
    if (result == CAP_OK) {
      result = capability_reserve(&process->capabilities, 1, &reservation, &slot);
    }
    status = metadata_status(result);
    if (status == CALL_OK) {
      capability_install_reserved(&reservation, &slot, &grant, 1, &reply.error_body);
    }
  }
  if (body) {
    object_release(&body->object);
  }
  capability_grant_release(&grant);
  capability_reservation_release(&reservation, &slot);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(destination, &reply, sizeof(reply),
      &reply.error_body, reply.error_body != HANDLE_INVALID ? 1 : 0)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
