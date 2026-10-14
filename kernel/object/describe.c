#include <abi/describe.h>
#include <abi/file.h>
#include <kernel/object/capability.h>
#include <kernel/object/describe.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/file.h>
#include <kernel/object/forwarder.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

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
  if (object->type == OBJECT_FILE) {
    struct file_object *file = (struct file_object *)object;
    reply->description.flags = RESOURCE_REREADABLE | RESOURCE_COMPLETE;
    if (file_cached_length(file, &reply->description.length)) {
      reply->description.flags |= RESOURCE_LENGTH_KNOWN;
    }
    if (file->immutable_snapshot) {
      reply->properties |= DESCRIBE_IMMUTABLE_SNAPSHOT;
    }
  }
  return CALL_OK;
}

struct syscall_result describe_call(uint64_t handle, uint64_t flags,
    uintptr_t destination)
{
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
  if (object->type == OBJECT_FORWARDER) {
    result = forwarder_snapshot(object, &leaf);
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
