#include <arch/smp.h>
#include <kernel/boot/options.h>
#include <kernel/net/log_udp.h>
#include <kernel/panic.h>
#include <kernel/user/launch.h>

void user_launch_initial(const struct boot_options *options)
{
  KASSERT(arch_cpu_index() == 0);
  if (options->log_udp) {
    net_log_udp_enable();
  }
  /* Boot init runs in Caelum's space and creates every other space. */
  user_launch_boot_init(options->init, &options->mount, options->install,
      options->default_config, options->remote_beacon);
}
