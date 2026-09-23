#include <abi/launcher.h>
#include <abi/memory.h>
#include <abi/directory.h>
#include <abi/console.h>
#include <abi/file.h>
#include <pxe/shebang.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/initrd.h>
#include <kernel/fs/initrd_tree.h>
#include <kernel/object/directory.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/memory.h>
#include <kernel/object/file.h>
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

/* Boot exposes only the immutable app archive. Match its exact entry names;
 * userspace path walking and its wider namespace policy stay in libpyxis. */
static enum initrd_result select_image(const char *name, struct initrd_file *image,
    struct initrd_file *script, char interpreter[SHEBANG_PREFIX_SIZE])
{
  *script = (struct initrd_file){0};
  enum initrd_result status = initrd_lookup(name, image);
  if (status != INITRD_OK) {
    return status;
  }
  struct shebang header;
  enum shebang_result format = shebang_parse(image->data, image->size, &header);
  if (format == SHEBANG_NONE) {
    return INITRD_OK;
  }
  if (format != SHEBANG_OK) {
    return INITRD_INVALID;
  }
  if (header.length <= 6 || memcmp(header.interpreter, "app://", 6)) {
    return INITRD_UNSUPPORTED;
  }
  memcpy(interpreter, header.interpreter, header.length);
  interpreter[header.length] = '\0';
  *script = *image;
  /* No recursive interpretation: the selected bytes go directly to PXE. */
  return initrd_lookup(interpreter + 6, image);
}

void user_launch_initial(void)
{
  KASSERT(arch_cpu_index() == 0);
  size_t cpu_index = arch_cpu_count() > 1 ? 1 : 0;
  struct process *process = NULL;
  struct kernel_object *memory = NULL, *launcher = NULL;
  struct file_object *script_file = NULL;

  if (initrd_tree_create(&application_root) != INITRD_OK) {
    goto fail;
  }
  home_root = directory_create(DIRECTORY_RAM);
  if (!home_root) {
    goto fail;
  }
  struct initrd_file image, script;
  char interpreter[SHEBANG_PREFIX_SIZE];
  uintptr_t entry;
  enum initrd_result selection = select_image("shell.pxe", &image, &script, interpreter);
  if (selection != INITRD_OK) {
    klog("userspace: cannot select boot image (initrd result %u)\n", (unsigned)selection);
    goto fail;
  }
  if (user_process_load(arch_cpu_at(cpu_index)->space, image.data, image.size,
        &process, &entry) != MM_OK) {
    goto fail;
  }
  memory = memory_create();
  launcher = launcher_create();
  if (!memory || !launcher) {
    goto fail;
  }

  handle_t input, output, memory_handle, launcher_handle, app, home;
  handle_t script_handle = HANDLE_INVALID;
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
  if (script.data) {
    script_file = file_create_initrd(&script);
    if (!script_file || capability_install(&process->capabilities, &script_file->object,
          FILE_RIGHT_READ, &script_handle) != CAP_OK) {
      goto fail;
    }
    object_release(&script_file->object);
    script_file = NULL;
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
    {"script", script_handle},
  };
  const struct process_binding roots[] = {{"app", app}, {"home", home}};
  const char *arguments[] = {script.data ? interpreter : "shell", "app://shell.pxe"};
  const struct process_variable environment[] = {{"OS_NAME", "Pyxis OS"}};
  const struct process_startup startup = {
    .resources = resources,
    .resource_count = sizeof(resources) / sizeof(resources[0]) - (script.data ? 0 : 1),
    .roots = roots,
    .root_count = sizeof(roots) / sizeof(roots[0]),
    .working_directories = &home,
    .working_directory_count = 1,
    .working_path = "home://",
    .environment = environment,
    .environment_count = sizeof(environment) / sizeof(environment[0]),
    .argc = script.data ? 2 : 1,
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
  if (script_file) {
    object_release(&script_file->object);
  }
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
