#include <abi/udp.h>
#include <abi/tcp.h>
#include <kernel/object/tcp.h>
#include <abi/random.h>
#include <kernel/object/random.h>
#include <kernel/object/udp.h>
#include <abi/mount.h>
#include <kernel/object/mount.h>
#include <kernel/virtio/pci.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/space.h>
#include <abi/clock.h>
#include <abi/echo.h>
#include <abi/net_config.h>
#include <kernel/object/net_config.h>
#include <kernel/object/echo.h>
#include <kernel/object/clock.h>
#include <abi/launcher.h>
#include <abi/display.h>
#include <kernel/object/display.h>
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

/* Namespace roots survive init and session exit; RAM contents remain until shutdown. */
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

void user_launch_init(size_t cpu_index, const char *image_uri)
{
  KASSERT(arch_cpu_index() == 0);
  struct process *process = NULL;
  struct kernel_object *memory = NULL, *launcher = NULL, *clock = NULL;
  struct file_object *script_file = NULL;
  struct kernel_object *space_control = NULL;
  struct kernel_object *mount = NULL, *echo = NULL, *net_config = NULL, *udp = NULL, *tcp = NULL, *random = NULL;

  if (!application_root) {
    if (initrd_tree_create(&application_root) != INITRD_OK) {
      goto fail;
    }
    home_root = directory_create(DIRECTORY_RAM);
    if (!home_root) {
      goto fail;
    }
  }
  struct initrd_file image, script;
  char interpreter[SHEBANG_PREFIX_SIZE];
  uintptr_t entry;
  enum initrd_result selection = select_image(image_uri + 6, &image, &script, interpreter);
  if (selection != INITRD_OK) {
    klog("userspace: cannot select %s (initrd result %u)\n", image_uri, (unsigned)selection);
    goto fail;
  }
  if (user_process_load(arch_cpu_at(cpu_index)->space, image.data, image.size,
        &process, &entry) != MM_OK) {
    goto fail;
  }
  memory = memory_create();
  launcher = launcher_create();
  clock = clock_create();
  echo = echo_create();
  net_config = net_config_create();
  udp = udp_service_create();
  tcp = tcp_service_create();
  random = random_create();
  if (!memory || !launcher || !clock || !echo || !net_config || !udp || !tcp || !random) {
    goto fail;
  }

  handle_t input, output, memory_handle, launcher_handle, display_handle, app, home;
  handle_t clock_handle, keyboard_handle, echo_handle, net_config_handle, udp_handle, tcp_handle, random_handle;
  handle_t script_handle = HANDLE_INVALID;
  struct kernel_object *console = &process->space->console->object;
  if (capability_install(&process->capabilities, console, CONSOLE_RIGHT_READ, &input) != CAP_OK ||
      capability_install(&process->capabilities, console, CONSOLE_RIGHT_WRITE, &output) != CAP_OK ||
      capability_install(&process->capabilities, memory, MEMORY_RIGHT_MANAGE, &memory_handle) != CAP_OK ||
      capability_install(&process->capabilities, &process->space->keyboard->object,
          KEYBOARD_RIGHT_INPUT, &keyboard_handle) != CAP_OK ||
      capability_install(&process->capabilities, net_config, NET_CONFIG_RIGHTS, &net_config_handle) != CAP_OK ||
      capability_install(&process->capabilities, random, RANDOM_RIGHT_READ, &random_handle) != CAP_OK ||
      capability_install(&process->capabilities, tcp, TCP_SERVICE_RIGHT_CONNECT, &tcp_handle) != CAP_OK ||
      capability_install(&process->capabilities, udp, UDP_SERVICE_RIGHT_OPEN, &udp_handle) != CAP_OK ||
      capability_install(&process->capabilities, echo, ECHO_RIGHT_SEND, &echo_handle) != CAP_OK ||
      capability_install(&process->capabilities, clock, CLOCK_RIGHTS, &clock_handle) != CAP_OK ||
      capability_install(&process->capabilities, launcher, LAUNCHER_RIGHT_LAUNCH, &launcher_handle) != CAP_OK ||
      capability_install(&process->capabilities, &process->space->display->object,
          DISPLAY_RIGHT_DRAW, &display_handle) != CAP_OK) {
    goto fail;
  }
  handle_t space_handle = HANDLE_INVALID;
  /* Caelum stays kernel-owned, including the single-CPU shared-TTY fallback. */
  if (cpu_index != 0) {
    space_control = space_control_create(process->space);
    if (!space_control || capability_install(&process->capabilities, space_control,
          SPACE_RIGHT_SET_TITLE, &space_handle) != CAP_OK) {
      goto fail;
    }
    object_release(space_control);
    space_control = NULL;
  }
  uint64_t app_rights = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE |
                        DIRECTORY_RIGHT_READ_FILES;
  uint64_t home_rights = app_rights | DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_WRITE_FILES |
                         DIRECTORY_RIGHT_REMOVE;
  if (capability_install(&process->capabilities, &application_root->object, app_rights, &app) != CAP_OK ||
      capability_install(&process->capabilities, &home_root->object, home_rights, &home) != CAP_OK) {
    goto fail;
  }
  handle_t mount_handle = HANDLE_INVALID;
  if (virtio_fs_pci_present()) {
    mount = mount_create();
    if (!mount || capability_install(&process->capabilities, mount,
          MOUNT_RIGHT_OPEN_ROOT, &mount_handle) != CAP_OK) {
      goto fail;
    }
    object_release(mount);
    mount = NULL;
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
  object_release(random);
  random = NULL;
  object_release(udp);
  udp = NULL;
  object_release(tcp);
  tcp = NULL;
  object_release(net_config);
  net_config = NULL;
  object_release(echo);
  echo = NULL;
  object_release(clock);
  clock = NULL;
  object_release(memory);
  object_release(launcher);
  memory = NULL;
  launcher = NULL; /* The process's grants now own the stateless services. */

  struct process_binding resources[15] = {
    {"input", input},
    {"output", output},
    {"memory", memory_handle},
    {"launcher", launcher_handle},
    {"display", display_handle},
    {"clock", clock_handle},
    {"echo", echo_handle},
    {"udp", udp_handle},
    {"tcp", tcp_handle},
    {"random", random_handle},
    {"net_config", net_config_handle},
    {"keyboard", keyboard_handle},
  };
  size_t resource_count = 12;
  if (space_handle != HANDLE_INVALID) {
    resources[resource_count++] = (struct process_binding){"space", space_handle};
  }
  if (script_handle != HANDLE_INVALID) {
    resources[resource_count++] = (struct process_binding){"script", script_handle};
  }
  if (mount_handle != HANDLE_INVALID) {
    resources[resource_count++] = (struct process_binding){"host_mount", mount_handle};
  }
  const struct process_binding roots[] = {{"app", app}, {"home", home}};
  const char *arguments[] = {script.data ? interpreter : image_uri, image_uri};
  const struct process_variable environment[] = {{"OS_NAME", "Pyxis OS"}};
  const struct process_startup startup = {
    .resources = resources,
    .resource_count = resource_count,
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
  klog("userspace: %s entry=%p, CPU %zu\n", image_uri, (void *)entry, cpu_index);
  if (user_task_create_on(cpu_index, process, entry,
        USER_INITIAL_STACK_BASE + USER_INITIAL_STACK_SIZE) != MM_OK) {
    goto fail;
  }
  /* The scheduler owns the process. Its eventual cleanup leaves the space,
   * terminal contents and namespace roots alive; there is no init restart. */
  return;

fail:
  if (space_control) {
    object_release(space_control);
  }
  if (tcp) {
    object_release(tcp);
  }
  if (random) {
    object_release(random);
  }
  if (udp) {
    object_release(udp);
  }
  if (net_config) {
    object_release(net_config);
  }
  if (echo) {
    object_release(echo);
  }
  if (mount) {
    object_release(mount);
  }
  if (clock) {
    object_release(clock);
  }
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
  panic("cannot prepare init %s on CPU %zu", image_uri, cpu_index);
}
