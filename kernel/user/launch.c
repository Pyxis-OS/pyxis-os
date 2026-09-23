#include <abi/launcher.h>
#include <abi/memory.h>
#include <abi/directory.h>
#include <abi/console.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/initrd.h>
#include <kernel/fs/initrd_tree.h>
#include <kernel/object/directory.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/object/launcher.h>
#include <kernel/object/memory.h>
#include <kernel/object/console.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/user.h>
#include <kernel/user/launch.h>
#include <kernel/user/startup.h>

/* Namespace roots survive shell exit; RAM contents remain until shutdown. */
static struct directory_object *application_root;
static struct directory_object *home_root;

void user_launch_initial(void)
{
  KASSERT(arch_cpu_index() == 0);
  size_t cpu_index = arch_cpu_count() > 1 ? 1 : 0;
  struct process *process = NULL;
  struct kernel_object *memory = NULL, *launcher = NULL;

  if (initrd_tree_create(&application_root) != INITRD_OK) {
    goto fail;
  }
  home_root = directory_create(DIRECTORY_RAM);
  if (!home_root) {
    goto fail;
  }
  struct initrd_file image;
  uintptr_t entry;
  if (initrd_lookup("shell.pxe", &image) != INITRD_OK ||
      user_process_load(arch_cpu_at(cpu_index)->space, image.data, image.size,
        &process, &entry) != MM_OK) {
    goto fail;
  }
  memory = memory_create();
  launcher = launcher_create();
  if (!memory || !launcher) {
    goto fail;
  }

  handle_t input, output, memory_handle, launcher_handle, app, home;
  struct kernel_object *console = &process->space->console->object;
  if (capability_install(&process->capabilities, console, CONSOLE_RIGHT_READ, &input) != CAP_OK ||
      capability_install(&process->capabilities, console, CONSOLE_RIGHT_WRITE, &output) != CAP_OK ||
      capability_install(&process->capabilities, memory, MEMORY_RIGHT_MANAGE, &memory_handle) != CAP_OK ||
      capability_install(&process->capabilities, launcher, LAUNCHER_RIGHT_LAUNCH, &launcher_handle) != CAP_OK) {
    goto fail;
  }
  uint64_t app_rights = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE |
                        DIRECTORY_RIGHT_READ_FILES;
  uint64_t home_rights = app_rights | DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_WRITE_FILES;
  if (capability_install(&process->capabilities, &application_root->object, app_rights, &app) != CAP_OK ||
      capability_install(&process->capabilities, &home_root->object, home_rights, &home) != CAP_OK) {
    goto fail;
  }
  object_release(memory);
  object_release(launcher);
  memory = NULL;
  launcher = NULL; /* The process's grants now own the stateless services. */

  const struct process_binding resources[] = {
    {"input", input},
    {"output", output},
    {"memory", memory_handle},
    {"launcher", launcher_handle},
  };
  const struct process_binding roots[] = {{"app", app}, {"home", home}};
  const char *arguments[] = {"shell"};
  const struct process_variable environment[] = {{"OS_NAME", "Pyxis OS"}};
  const struct process_startup startup = {
    .resources = resources,
    .resource_count = sizeof(resources) / sizeof(resources[0]),
    .roots = roots,
    .root_count = sizeof(roots) / sizeof(roots[0]),
    .working_directories = &home,
    .working_directory_count = 1,
    .working_path = "home://",
    .environment = environment,
    .environment_count = sizeof(environment) / sizeof(environment[0]),
    .argc = 1,
    .argv = arguments,
  };
  if (process_prepare_startup(process, &startup) != MM_OK) {
    goto fail;
  }
  klog("userspace: shell.pxe entry=%p, CPU %zu\n", (void *)entry, cpu_index);
  if (user_task_create_on(cpu_index, process, entry,
        USER_INITIAL_STACK_BASE + USER_INITIAL_STACK_SIZE) != MM_OK) {
    goto fail;
  }
  /* The scheduler owns the process. Its eventual cleanup leaves the space,
   * terminal contents and namespace roots alive; there is no shell restart. */
  return;

fail:
  if (launcher) {
    object_release(launcher);
  }
  if (memory) {
    object_release(memory);
  }
  if (process) {
    KASSERT(process_destroy(process) == MM_OK);
  }
  if (application_root) {
    object_release(&application_root->object);
    application_root = NULL;
  }
  if (home_root) {
    object_release(&home_root->object);
    home_root = NULL;
  }
  while (object_reap_pending()) {
    object_reap();
  }
  panic("cannot prepare initial shell");
}
