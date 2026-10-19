#ifndef KERNEL_COMMAND_SEARCH_H
#define KERNEL_COMMAND_SEARCH_H

#include <abi/command_search.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>

/* BSP owned constructor policy, exposed by INFO rather than fixed in the ABI. */
#define COMMAND_LIST_ENTRIES_LIMIT 16

/* BSP, IF=0, exclusive preparation. Moves grants on success only; the returned
 * provisional logical owner must be released after installing an owned grant. */
struct kernel_object *command_list_create(const struct command_list_create_entry *entries,
    struct capability_grant *grants, size_t count);
void command_list_release(struct kernel_object *object);
/* IF=0, no held guards. Logical closure releases members synchronously even
 * while storage references remain. Retain fails after the last logical close. */
bool command_list_grant_retain(struct kernel_object *object);
void command_list_grant_release(struct kernel_object *object);
bool command_list_entry_valid(const struct command_list_create_entry *entry);

struct syscall_result command_list_create_call(uintptr_t entries_address,
    size_t count, uintptr_t reply_address);
struct syscall_result command_list_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
