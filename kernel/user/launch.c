#include <kernel/object/terminal.h>
#include <kernel/object/endpoint.h>
#include <abi/pipe.h>
#include <kernel/object/pipe.h>
#include <kernel/object/retention.h>
#include <abi/random.h>
#include <kernel/object/random.h>
#include <abi/mount.h>
#include <kernel/object/mount.h>
#include <kernel/block.h>
#include <kernel/virtio/pci.h>
#include <kernel/object/space.h>
#include <kernel/object/profile.h>
#include <abi/clock.h>
#include <abi/net_config.h>
#include <kernel/object/net_config.h>
#include <kernel/object/clock.h>
#include <kernel/object/system_info.h>
#include <kernel/object/input_policy.h>
#include <kernel/object/log.h>
#include <abi/log.h>
#include <abi/launcher.h>
#include <abi/memory.h>
#include <abi/directory.h>
#include <abi/console.h>
#include <abi/file.h>
#include <pxe/shebang.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/initrd.h>
#include <kernel/boot_files.h>
#include <kernel/object/disk.h>
#include <kernel/object/power.h>
#include <kernel/object/uefi.h>
#include <abi/uefi.h>
#include <kernel/object/screen_capture.h>
#include <kernel/object/process_view.h>
#include <abi/acpi_tables.h>
#include <kernel/object/bluetooth_hci.h>
#include <kernel/object/wifi.h>
#include <abi/wifi.h>
#include <abi/power.h>
#include <kernel/fs/initrd_tree.h>
#include <kernel/object/directory.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/object/memory.h>
#include <kernel/object/thread.h>
#include <kernel/object/console.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/user.h>
#include <kernel/user/launch.h>
#include <kernel/user/startup.h>

/* Namespace roots survive init and session exit; RAM contents remain until shutdown. */
static struct directory_object *boot_root;
static struct directory_object *tmp_root;
/* Boot init's private RAM directory, from which it makes configured RAM volumes. */
static struct directory_object *ram_root;
static const char stock_boot_init_uri[] = USER_DEFAULT_BOOT_INIT;

/* Boot exposes only the immutable archive. Match its exact entry names;
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
  if (header.length <= USER_BOOT_ROOT_PREFIX_LENGTH ||
      memcmp(header.interpreter, USER_BOOT_ROOT_PREFIX, USER_BOOT_ROOT_PREFIX_LENGTH)) {
    return INITRD_UNSUPPORTED;
  }
  memcpy(interpreter, header.interpreter, header.length);
  interpreter[header.length] = '\0';
  *script = *image;
  /* No recursive interpretation: the selected bytes go directly to PXE. */
  return initrd_lookup(interpreter + USER_BOOT_ROOT_PREFIX_LENGTH, image);
}

/* Boot init's arguments: its image (twice for a script), then at most
 * --installed, --pool N, --default-config and --remote-beacon NAME, with room
 * to spare. */
#define BOOT_INIT_ARGUMENT_CAPACITY 12

static void add_argument(const char **arguments, size_t *count, const char *argument)
{
  KASSERT(*count < BOOT_INIT_ARGUMENT_CAPACITY);
  arguments[(*count)++] = argument;
}

void user_launch_boot_init(const char *image_uri, const struct mount_config *mount_config,
    uint32_t pool_partition, bool pool_malformed, bool install, bool default_config,
    const char *remote_beacon)
{
  KASSERT(arch_cpu_index() == 0);
  struct space *space = space_caelum();
  struct process *process = NULL;
  struct kernel_object *memory = NULL, *launcher = NULL, *clock = NULL;
  struct kernel_object *threads = NULL;
  struct kernel_object *terminal_service = NULL, *retention_factory = NULL;
  struct kernel_object *system_info = NULL;
  struct kernel_object *input_policy = NULL;
  struct kernel_object *log = NULL;
  struct kernel_object *disks = NULL;
  struct kernel_object *fat_mounts = NULL;
  struct kernel_object *power = NULL;
  struct kernel_object *uefi = NULL;
  struct kernel_object *screen_capture = NULL;
  struct kernel_object *process_view = NULL;
  struct directory_object *acpi_tables = NULL;
  struct kernel_object *bluetooth_hci = NULL, *wifi = NULL;
  struct file_object *boot_kernel = NULL, *boot_archive = NULL;
  struct file_object *script_file = NULL;
  struct kernel_object *factory = NULL, *profile = NULL, *pipe = NULL, *service = NULL;
  struct kernel_object *mount = NULL, *net_registry = NULL, *net_policy = NULL, *random = NULL;

  if (!boot_root) {
    if (initrd_tree_create(&boot_root) != INITRD_OK) {
      goto fail;
    }
    tmp_root = directory_create(DIRECTORY_RAM);
    ram_root = directory_create(DIRECTORY_RAM);
    if (!tmp_root || !ram_root) {
      goto fail;
    }
  }
  struct initrd_file image, script;
  char interpreter[SHEBANG_PREFIX_SIZE];
  uintptr_t entry, stack_top;
  enum initrd_result selection = select_image(image_uri + USER_BOOT_ROOT_PREFIX_LENGTH,
      &image, &script, interpreter);
  if (selection != INITRD_OK) {
    kerror("userspace: cannot select %s (initrd result %u)\n", image_uri, (unsigned)selection);
    goto fail;
  }
  if (user_process_load(space, image.data, image.size, 0,
        &process, &entry, &stack_top) != MM_OK) {
    goto fail;
  }
  memory = memory_create();
  threads = thread_service_create();
  launcher = launcher_create();
  clock = clock_create();
  system_info = system_info_create();
  log = log_create();
  net_registry = net_registry_create();
  net_policy = net_policy_create();
  random = random_create();
  power = power_create();
  uefi = uefi_create();
  screen_capture = screen_capture_create();
  process_view = process_view_create();
  bluetooth_hci = bluetooth_hci_create();
  wifi = wifi_create(process);
  profile = profile_create();
  pipe = pipe_service_create();
  service = endpoint_service_create();
  terminal_service = terminal_service_create();
  retention_factory = retention_factory_create();
  if (!memory || !threads || !launcher || !clock || !system_info || !log || !net_registry || !net_policy || !random || !power || !uefi || !screen_capture || !process_view || !bluetooth_hci || !wifi || !profile || !pipe || !service || !terminal_service || !retention_factory) {
    goto fail;
  }

  handle_t memory_handle, launcher_handle, boot, tmp, ram;
  handle_t threads_handle;
  handle_t clock_handle, net_registry_handle, net_policy_handle, random_handle;
  handle_t profile_handle, pipe_handle, service_handle, terminal_service_handle;
  handle_t retention_factory_handle;
  handle_t system_info_handle, power_handle, uefi_handle, log_handle, screen_capture_handle;
  handle_t process_view_handle;
  handle_t bluetooth_hci_handle, wifi_handle;
  handle_t input_policy_handle = HANDLE_INVALID;
  handle_t script_handle = HANDLE_INVALID;
  handle_t standard_output, standard_error;
  struct kernel_object *console = &process->space->console->object;
  uint64_t system_info_rights = SYSTEM_INFO_RIGHT_READ;
  uint64_t memory_rights = MEMORY_RIGHT_MANAGE;
  if (!install && strlen(image_uri) == sizeof(stock_boot_init_uri) - 1 &&
      !memcmp(image_uri, stock_boot_init_uri, sizeof(stock_boot_init_uri))) {
    system_info_rights |= SYSTEM_INFO_RIGHT_SET_HOSTNAME_ONCE;
    /* Explicit trusted bootstrap authority; ordinary MANAGE grants stay private. */
    memory_rights |= MEMORY_RIGHT_CREATE_SHARED;
    input_policy = input_policy_create();
    if (!input_policy || capability_install(&process->capabilities, input_policy,
        INPUT_POLICY_RIGHT_INSTALL_ONCE, 0, &input_policy_handle) != CAP_OK) {
      goto fail;
    }
    object_release(input_policy);
    input_policy = NULL;
  }
  if (capability_install(&process->capabilities, retention_factory,
        RETENTION_FACTORY_RIGHT_CREATE, 0, &retention_factory_handle) != CAP_OK ||
      capability_install(&process->capabilities, terminal_service, TERMINAL_SERVICE_RIGHT_CREATE, 0, &terminal_service_handle) != CAP_OK ||
      capability_install(&process->capabilities, service, ENDPOINT_SERVICE_RIGHT_CREATE, 0, &service_handle) != CAP_OK ||
      capability_install(&process->capabilities, profile, PROFILE_RIGHT_MEMORY | PROFILE_RIGHT_HOST, 0, &profile_handle) != CAP_OK ||
      capability_install(&process->capabilities, pipe, PIPE_SERVICE_RIGHT_CREATE, 0, &pipe_handle) != CAP_OK ||
      capability_install(&process->capabilities, console, CONSOLE_RIGHT_WRITE, 0, &standard_output) != CAP_OK ||
      capability_install(&process->capabilities, console, CONSOLE_RIGHT_WRITE, 0, &standard_error) != CAP_OK ||
      capability_install(&process->capabilities, memory, memory_rights, 0, &memory_handle) != CAP_OK ||
      capability_install(&process->capabilities, threads, THREAD_RIGHT_MANAGE, 0, &threads_handle) != CAP_OK ||
      capability_install(&process->capabilities, net_registry, NET_REGISTRY_RIGHT_READ, 0, &net_registry_handle) != CAP_OK ||
      capability_install(&process->capabilities, random, RANDOM_RIGHT_READ, 0, &random_handle) != CAP_OK ||
      capability_install(&process->capabilities, power, POWER_RIGHTS, 0, &power_handle) != CAP_OK ||
      capability_install(&process->capabilities, uefi, UEFI_RIGHTS, 0, &uefi_handle) != CAP_OK ||
      capability_install(&process->capabilities, screen_capture,
          SCREEN_CAPTURE_RIGHT_CAPTURE, 0, &screen_capture_handle) != CAP_OK ||
      capability_install(&process->capabilities, process_view,
          PROCESS_VIEW_RIGHT_LIST, 0, &process_view_handle) != CAP_OK ||
      capability_install(&process->capabilities, wifi,
          WIFI_RIGHTS | WIFI_RIGHT_SUPPLICANT, 0, &wifi_handle) != CAP_OK ||
      capability_install(&process->capabilities, bluetooth_hci,
          BLUETOOTH_HCI_RIGHT_CONTROL, 0, &bluetooth_hci_handle) != CAP_OK ||
      capability_install(&process->capabilities, net_policy, NET_POLICY_RIGHT_MANAGE, 0, &net_policy_handle) != CAP_OK ||
      capability_install(&process->capabilities, clock, CLOCK_RIGHTS, 0, &clock_handle) != CAP_OK ||
      capability_install(&process->capabilities, system_info, system_info_rights, 0, &system_info_handle) != CAP_OK ||
      capability_install(&process->capabilities, log, LOG_RIGHT_READ | LOG_RIGHT_WRITE, 0, &log_handle) != CAP_OK ||
      capability_install(&process->capabilities, launcher,
          LAUNCHER_RIGHT_LAUNCH, 0, &launcher_handle) != CAP_OK) {
    goto fail;
  }
  handle_t factory_handle;
  factory = space_factory_create();
  if (!factory || capability_install(&process->capabilities, factory,
        SPACE_FACTORY_RIGHT_CREATE, 0, &factory_handle) != CAP_OK) {
    goto fail;
  }
  object_release(factory);
  factory = NULL;
  uint64_t boot_rights = DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE |
                         DIRECTORY_RIGHT_READ_FILES;
  /* FILESYSTEM_INFO lets tmp:// report that it is RAM-backed, which pkg
   * upgrade's boot publisher workspace requires. */
  uint64_t tmp_rights = boot_rights | DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_WRITE_FILES |
                        DIRECTORY_RIGHT_REMOVE | DIRECTORY_RIGHT_FILESYSTEM_INFO;
  if (capability_install(&process->capabilities, &boot_root->object, boot_rights, 0, &boot) != CAP_OK ||
      capability_install(&process->capabilities, &tmp_root->object, tmp_rights, 0, &tmp) != CAP_OK ||
      capability_install(&process->capabilities, &ram_root->object, tmp_rights, 0, &ram) != CAP_OK) {
    goto fail;
  }
  handle_t mount_handle = HANDLE_INVALID;
  if (virtio_fs_pci_present()) {
    mount = mount_create();
    if (!mount || capability_install(&process->capabilities, mount,
          MOUNT_RIGHT_OPEN_ROOT, 0, &mount_handle) != CAP_OK) {
      goto fail;
    }
    object_release(mount);
    mount = NULL;
  }
  handle_t esp_mount_handle = HANDLE_INVALID;
  mount = mount_create_boot_esp(mount_config);
  if (mount) {
    if (capability_install(&process->capabilities, mount, MOUNT_RIGHT_OPEN_ROOT |
        MOUNT_RIGHT_OBSERVE | MOUNT_RIGHT_UPGRADE_BOOT_ESP, 0, &esp_mount_handle) != CAP_OK) {
      goto fail;
    }
    object_release(mount);
    mount = NULL;
  }
  handle_t ram_npfs_handle = HANDLE_INVALID;
  mount = mount_create_ram_npfs();
  if (mount) {
    if (capability_install(&process->capabilities, mount,
          MOUNT_RIGHT_OPEN_ROOT | MOUNT_RIGHT_OBSERVE | MOUNT_RIGHT_WRITE,
          0, &ram_npfs_handle) != CAP_OK) ram_npfs_handle = HANDLE_INVALID;
    object_release(mount);
    mount = NULL;
  }
  handle_t native_mount_handle = HANDLE_INVALID;
  if (mount_config->enabled && (block_device_count() || !block_inventory_complete())) {
    mount = mount_create_native(mount_config);
    if (!mount || capability_install(&process->capabilities, mount,
          MOUNT_RIGHT_OPEN_ROOT | MOUNT_RIGHT_OBSERVE | MOUNT_RIGHT_WRITE,
          0, &native_mount_handle) != CAP_OK) {
      goto fail;
    }
    object_release(mount);
    mount = NULL;
  }
  handle_t disks_handle = HANDLE_INVALID;
  handle_t fat_mounts_handle;
  fat_mounts = disks_create();
  if (!fat_mounts || capability_install(&process->capabilities, fat_mounts,
        DISKS_RIGHT_ENUMERATE | DISKS_RIGHT_MOUNT_READ_ONLY | DISKS_RIGHT_MOUNT_READ_WRITE,
        0, &fat_mounts_handle) != CAP_OK) {
    goto fail;
  }
  object_release(fat_mounts);
  fat_mounts = NULL;
  /* The table root exists on every boot; without an ACPI worker it answers UNAVAILABLE. */
  handle_t acpi_tables_handle;
  acpi_tables = directory_create(DIRECTORY_ACPI);
  if (!acpi_tables || capability_install(&process->capabilities, &acpi_tables->object,
        ACPI_TABLES_RIGHTS, 0, &acpi_tables_handle) != CAP_OK) {
    goto fail;
  }
  object_release(&acpi_tables->object);
  acpi_tables = NULL;
  handle_t kernel_handle = HANDLE_INVALID, archive_handle = HANDLE_INVALID;
  if (install) {
    disks = disks_create();
    if (!disks || capability_install(&process->capabilities, disks,
        DISKS_RIGHTS, 0, &disks_handle) != CAP_OK) {
      goto fail;
    }
    object_release(disks);
    disks = NULL;
  }
  if (install || (strlen(image_uri) == sizeof(stock_boot_init_uri) - 1 &&
      !memcmp(image_uri, stock_boot_init_uri, sizeof(stock_boot_init_uri)))) {
    /* Exact immutable loaded bytes, private to trusted early selection on a
     * normal boot. A path or build revision cannot certify the boot pair. */
    boot_kernel = file_create_initrd(boot_files_kernel());
    boot_archive = file_create_initrd(boot_files_archive());
    if (!boot_kernel || !boot_archive ||
        capability_install(&process->capabilities, &boot_kernel->object,
            FILE_RIGHT_READ, 0, &kernel_handle) != CAP_OK ||
        capability_install(&process->capabilities, &boot_archive->object,
            FILE_RIGHT_READ, 0, &archive_handle) != CAP_OK) {
      goto fail;
    }
    object_release(&boot_kernel->object);
    boot_kernel = NULL;
    object_release(&boot_archive->object);
    boot_archive = NULL;
  }
  if (script.data) {
    script_file = file_create_initrd(&script);
    if (!script_file || capability_install(&process->capabilities, &script_file->object,
          FILE_RIGHT_READ, 0, &script_handle) != CAP_OK) {
      goto fail;
    }
    object_release(&script_file->object);
    script_file = NULL;
  }
  object_release(terminal_service);
  terminal_service = NULL;
  object_release(retention_factory);
  retention_factory = NULL;
  object_release(service);
  service = NULL;
  object_release(profile);
  profile = NULL;
  object_release(pipe);
  pipe = NULL;
  object_release(random);
  random = NULL;
  object_release(power);
  power = NULL;
  object_release(uefi);
  uefi = NULL;
  object_release(screen_capture);
  screen_capture = NULL;
  object_release(process_view);
  process_view = NULL;
  object_release(bluetooth_hci);
  bluetooth_hci = NULL;
  object_release(wifi);
  wifi = NULL;
  object_release(net_registry);
  net_registry = NULL;
  object_release(net_policy);
  net_policy = NULL;
  object_release(system_info);
  system_info = NULL;
  object_release(log);
  log = NULL;
  object_release(clock);
  clock = NULL;
  object_release(memory);
  object_release(threads);
  threads = NULL;
  object_release(launcher);
  memory = NULL;
  launcher = NULL; /* The process's grants now own the stateless services. */

  struct process_environment_entry entries[41] = {
    {"memory", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, memory_handle, 0},
    {"threads", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, threads_handle, 0},
    {"launcher", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, launcher_handle, 0},
    {"clock", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, clock_handle, 0},
    {"system_info", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, system_info_handle, 0},
    {"log", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, log_handle, 0},
    {"random", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, random_handle, 0},
    {"net_registry", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, net_registry_handle, 0},
    {"net_policy", ENVIRONMENT_CAP, 0, net_policy_handle, 0},
    {"profile", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, profile_handle, 0},
    {"pipe", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, pipe_handle, 0},
    {"service", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, service_handle, 0},
    {"terminal", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, terminal_service_handle, 0},
    {"power", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, power_handle, 0},
    {"uefi", ENVIRONMENT_CAP, 0, uefi_handle, 0},
    {"screen_capture", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, screen_capture_handle, 0},
    {"acpi_tables", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, acpi_tables_handle, 0},
    {"bluetooth_hci", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, bluetooth_hci_handle, 0},
    {"fat_mounts", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, fat_mounts_handle, 0},
  };
  size_t entry_count = 19;
  entries[entry_count++] = (struct process_environment_entry){"wifi", ENVIRONMENT_CAP, 0, wifi_handle, 0};
  entries[entry_count++] = (struct process_environment_entry){"retention_factory",
    ENVIRONMENT_CAP, 0, retention_factory_handle, 0};
  /* Not exported: boot init hands it on explicitly, and bundles request it. */
  entries[entry_count++] = (struct process_environment_entry){"process_view",
    ENVIRONMENT_CAP, 0, process_view_handle, 0};
  if (input_policy_handle != HANDLE_INVALID) {
    entries[entry_count++] = (struct process_environment_entry){"input_policy", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, input_policy_handle, 0};
  }
  entries[entry_count++] = (struct process_environment_entry){"space_factory", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, factory_handle, 0};
  entries[entry_count++] = (struct process_environment_entry){"ram", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, ram, 0};
  if (script_handle != HANDLE_INVALID) {
    entries[entry_count++] = (struct process_environment_entry){"script", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, script_handle, 0};
  }
  if (mount_handle != HANDLE_INVALID) {
    entries[entry_count++] = (struct process_environment_entry){"host_mount", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, mount_handle, 0};
  }
  if (esp_mount_handle != HANDLE_INVALID) {
    entries[entry_count++] = (struct process_environment_entry){"esp_mount", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, esp_mount_handle, 0};
  }
  if (ram_npfs_handle != HANDLE_INVALID) {
    entries[entry_count++] = (struct process_environment_entry){"ram_npfs", ENVIRONMENT_CAP, 0, ram_npfs_handle, 0};
  }
  if (native_mount_handle != HANDLE_INVALID) {
    entries[entry_count++] = (struct process_environment_entry){"native_mount", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, native_mount_handle, 0};
  }
  if (disks_handle != HANDLE_INVALID) {
    entries[entry_count++] = (struct process_environment_entry){"disks", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, disks_handle, 0};
  }
  if (kernel_handle != HANDLE_INVALID) {
    uint64_t flags = install ? ENVIRONMENT_EXPORTED : 0;
    entries[entry_count++] = (struct process_environment_entry){"boot_kernel", ENVIRONMENT_CAP, flags, kernel_handle, 0};
    entries[entry_count++] = (struct process_environment_entry){"boot_archive", ENVIRONMENT_CAP, flags, archive_handle, 0};
  }
  entries[entry_count++] = (struct process_environment_entry){"boot", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, boot, 0};
  entries[entry_count++] = (struct process_environment_entry){"tmp", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, tmp, 0};
  entries[entry_count++] = (struct process_environment_entry){"stdout", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, standard_output, 0};
  entries[entry_count++] = (struct process_environment_entry){"stderr", ENVIRONMENT_CAP, ENVIRONMENT_EXPORTED, standard_error, 0};
  entries[entry_count++] = (struct process_environment_entry){
    "OS_NAME", ENVIRONMENT_TEXT, ENVIRONMENT_EXPORTED, (uintptr_t)"Pyxis OS", sizeof("Pyxis OS") - 1,
  };
  /* Boot init learns the bound disk, its pool and the rescue selection from its
   * arguments. */
  const char *arguments[BOOT_INIT_ARGUMENT_CAPACITY] = {
    script.data ? interpreter : image_uri, image_uri,
  };
  size_t argument_count = script.data ? 2 : 1;
  char pool[11];
  if (mount_config->enabled) {
    add_argument(arguments, &argument_count, "--installed");
  }
  if (mount_config->enabled && pool_malformed) {
    add_argument(arguments, &argument_count, "--pool-malformed");
  } else if (mount_config->enabled && pool_partition) {
    size_t digits = 0;
    for (uint32_t value = pool_partition; value; value /= 10) {
      ++digits;
    }
    pool[digits] = '\0';
    for (uint32_t value = pool_partition; value; value /= 10) {
      pool[--digits] = (char)('0' + value % 10);
    }
    add_argument(arguments, &argument_count, "--pool");
    add_argument(arguments, &argument_count, pool);
  }
  if (default_config) {
    add_argument(arguments, &argument_count, "--default-config");
  }
  if (remote_beacon) {
    add_argument(arguments, &argument_count, "--remote-beacon");
    add_argument(arguments, &argument_count, remote_beacon);
  }
  const struct process_startup startup = {
    .entries = entries,
    .entry_count = entry_count,
    .working_directories = &tmp,
    .working_directory_count = 1,
    .working_path = "tmp://",
    .argc = argument_count,
    .argv = arguments,
  };
  if (process_prepare_startup(process, &startup) != MM_OK) {
    goto fail;
  }
  process_set_label(process, argument_count ? arguments[0] : image_uri);
  kinfo("userspace: boot init %s entry=%p\n", image_uri, (void *)entry);
  if (user_task_create(process, entry, stack_top) != MM_OK) {
    goto fail;
  }
  /* The scheduler owns the process. Its eventual cleanup leaves the space,
   * terminal contents and filesystem roots alive; there is no init restart. */
  return;

fail:
  if (retention_factory) {
    object_release(retention_factory);
  }
  if (acpi_tables) {
    object_release(&acpi_tables->object);
  }
  if (input_policy) {
    object_release(input_policy);
  }
  if (screen_capture) {
    object_release(screen_capture);
  }
  if (process_view) {
    object_release(process_view);
  }
  if (wifi) {
    object_release(wifi);
  }
  if (bluetooth_hci) {
    object_release(bluetooth_hci);
  }
  if (log) {
    object_release(log);
  }
  if (disks) {
    object_release(disks);
  }
  if (fat_mounts) {
    object_release(fat_mounts);
  }
  if (boot_kernel) {
    object_release(&boot_kernel->object);
  }
  if (boot_archive) {
    object_release(&boot_archive->object);
  }
  if (system_info) {
    object_release(system_info);
  }
  if (terminal_service) {
    object_release(terminal_service);
  }
  if (service) {
    object_release(service);
  }
  if (pipe) {
    object_release(pipe);
  }
  if (profile) {
    object_release(profile);
  }
  if (factory) {
    object_release(factory);
  }
  if (random) {
    object_release(random);
  }
  if (power) {
    object_release(power);
  }
  if (uefi) {
    object_release(uefi);
  }
  if (net_registry) {
    object_release(net_registry);
  }
  if (net_policy) {
    object_release(net_policy);
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
  if (threads) {
    object_release(threads);
  }
  if (process) {
    KASSERT(process_destroy(process) == MM_OK);
  }
  if (boot_root) {
    object_release(&boot_root->object);
    boot_root = NULL;
  }
  if (tmp_root) {
    object_release(&tmp_root->object);
    tmp_root = NULL;
  }
  if (ram_root) {
    object_release(&ram_root->object);
    ram_root = NULL;
  }
  while (object_reap_pending()) {
    object_reap();
  }
  panic("cannot prepare boot init %s", image_uri);
}
