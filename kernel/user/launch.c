#include <abi/file.h>
#include <abi/memory.h>
#include <abi/directory.h>
#include <abi/console.h>
#include <abi/endpoint.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/image.h>
#include <kernel/initrd.h>
#include <kernel/fs/initrd_tree.h>
#include <kernel/object/directory.h>
#include <kernel/log.h>
#include <kernel/mm/vm.h>
#include <kernel/object/file.h>
#include <kernel/object/memory.h>
#include <kernel/object/console.h>
#include <kernel/object/endpoint.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/user.h>
#include <kernel/user/launch.h>
#include <kernel/user/startup.h>

#define INITIAL_STACK_BASE UINT64_C(0x800000)
#define INITIAL_STACK_SIZE PAGE_SIZE

struct initial_program {
  struct process *process;
  const char *name;
  uintptr_t entry;
  size_t cpu_index;
  handle_t output, content, endpoint, application_root, home, memory;
};

/* Shared read-only application namespace retains the tree across process exit. */
static struct directory_object *application_root;
/* One shared RAM namespace, retained until shutdown regardless of process exit. */
static struct directory_object *home_root;

static bool load_program(const char *name, size_t cpu_index,
                         struct initial_program *program)
{
  struct initrd_file image;
  if (initrd_lookup(name, &image) != INITRD_OK) {
    return false;
  }
  struct vm_space *space;
  if (image_load(image.data, image.size, &space, &program->entry) != IMAGE_OK) {
    return false;
  }
  if (vm_alloc_at(space, INITIAL_STACK_BASE, INITIAL_STACK_SIZE,
        PAGE_USER | PAGE_WRITE) != MM_OK) {
    KASSERT(vm_space_destroy(space) == MM_OK);
    return false;
  }
  if (process_create(arch_cpu_at(cpu_index)->space, space,
        &program->process) != MM_OK) {
    KASSERT(vm_space_destroy(space) == MM_OK);
    return false;
  }
  program->name = name;
  program->cpu_index = cpu_index;
  struct process *process = program->process;
  if (capability_install(&process->capabilities, &process->space->console->object,
        CONSOLE_RIGHT_WRITE, &program->output) != CAP_OK) {
    return false;
  }
  klog("userspace: %s entry=%p, CPU %zu\n", name, (void *)program->entry, cpu_index);
  return true;
}

void user_launch_initial(void)
{
  KASSERT(arch_cpu_index() == 0);
  size_t client_cpu = arch_cpu_count() > 1 ? 1 : 0;
  size_t server_cpu = arch_cpu_count() > 2 ? 2 : client_cpu;
  struct initial_program programs[3] = {0};
  struct kernel_object *memory = NULL;
  struct initial_program *hello = &programs[0];
  struct initial_program *server = &programs[1];
  struct initial_program *client = &programs[2];

  if (initrd_tree_create(&application_root) != INITRD_OK) {
    goto fail;
  }
  home_root = directory_create(DIRECTORY_RAM);
  if (!home_root) {
    goto fail;
  }
  memory = memory_create();
  if (!memory) {
    goto fail;
  }
  if (!load_program("hello.pxe", client_cpu, hello) ||
      !load_program("server.pxe", server_cpu, server) ||
      !load_program("client.pxe", client_cpu, client)) {
    goto fail;
  }

  struct initrd_file text;
  if (initrd_lookup("share/hello.txt", &text) != INITRD_OK) {
    goto fail;
  }
  struct file_object *file = file_create_initrd(&text);
  if (!file) {
    goto fail;
  }
  enum capability_result result = capability_install(&client->process->capabilities,
      &file->object, FILE_RIGHT_READ, &client->content);
  object_release(&file->object);
  if (result != CAP_OK) {
    goto fail;
  }
  result = capability_install(&hello->process->capabilities, &application_root->object,
      DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | DIRECTORY_RIGHT_READ_FILES,
      &hello->application_root);
  if (result != CAP_OK) {
    goto fail;
  }

  struct endpoint *caller, *service;
  if (!endpoint_pair_create(&caller, &service)) {
    goto fail;
  }
  result = capability_install(&client->process->capabilities, &caller->object,
      ENDPOINT_RIGHT_CALL, &client->endpoint);
  handle_t service_grant = HANDLE_INVALID;
  if (result == CAP_OK) {
    result = capability_install(&client->process->capabilities, &service->object,
        ENDPOINT_RIGHT_CALL | ENDPOINT_RIGHT_RECEIVE | ENDPOINT_RIGHT_REPLY,
        &service_grant);
  }
  object_release(&caller->object);
  object_release(&service->object);
  if (result != CAP_OK) {
    goto fail;
  }

  /* Both tables are still private to the launcher. Attenuate a real source
   * grant, then remove it before the client can run with service authority. */
  result = capability_grant(&server->process->capabilities,
      &client->process->capabilities, service_grant,
      ENDPOINT_RIGHT_RECEIVE | ENDPOINT_RIGHT_REPLY, &server->endpoint);
  KASSERT(capability_close(&client->process->capabilities, service_grant) == CAP_OK);
  if (result != CAP_OK) {
    goto fail;
  }

  for (size_t i = 0; i < 3; ++i) {
    struct initial_program *program = &programs[i];
    result = capability_install(&program->process->capabilities, memory,
        MEMORY_RIGHT_MANAGE, &program->memory);
    if (result != CAP_OK) {
      goto fail;
    }
    struct process_binding resources[4];
    size_t count = 0;
    resources[count++] = (struct process_binding){"output", program->output};
    resources[count++] = (struct process_binding){"memory", program->memory};
    if (program->content != HANDLE_INVALID) {
      resources[count++] = (struct process_binding){"content", program->content};
    }
    if (program->endpoint != HANDLE_INVALID) {
      resources[count++] = (struct process_binding){"endpoint", program->endpoint};
    }
    result = capability_install(&program->process->capabilities, &home_root->object,
        DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | DIRECTORY_RIGHT_READ_FILES |
        DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_WRITE_FILES, &program->home);
    if (result != CAP_OK) {
      goto fail;
    }
    struct process_binding roots[2];
    size_t root_count = 0;
    if (program->application_root != HANDLE_INVALID) {
      roots[root_count++] = (struct process_binding){"app", program->application_root};
    }
    roots[root_count++] = (struct process_binding){"home", program->home};
    const char *arguments[] = {program->name};
    const struct process_variable environment[] = {{"OS_NAME", "Pyxis OS"}};
    const struct process_startup startup = {
      .resources = resources,
      .resource_count = count,
      .roots = roots,
      .root_count = root_count,
      .working_directories = &program->application_root,
      .working_directory_count = program->application_root != HANDLE_INVALID ? 1 : 0,
      .working_path = program->application_root != HANDLE_INVALID ? "app://" : NULL,
      .environment = environment,
      .environment_count = 1,
      .argc = 1,
      .argv = arguments,
    };
    if (process_prepare_startup(program->process, &startup) != MM_OK) {
      goto fail;
    }
  }
  object_release(memory);
  memory = NULL; /* Startup handles now own the stateless service. */
  for (size_t i = 0; i < 3; ++i) {
    struct initial_program *program = &programs[i];
    if (user_task_create_on(program->cpu_index, program->process, program->entry,
          INITIAL_STACK_BASE + INITIAL_STACK_SIZE) != MM_OK) {
      goto fail;
    }
    program->process = NULL; /* Ownership transferred to its scheduler queue. */
  }
  return;

fail:
  if (memory) {
    object_release(memory);
  }
  for (size_t i = 0; i < 3; ++i) {
    if (programs[i].process) {
      KASSERT(process_destroy(programs[i].process) == MM_OK);
    }
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
  /* Already submitted tasks remain owned by their queues. Scheduling has not
   * started; a boot failure halts without exposing a partially prepared peer. */
  panic("cannot prepare initial userspace programs");
}
