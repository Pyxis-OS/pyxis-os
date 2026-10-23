/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_ACPI_WIFI_NAMESPACE_H
#define KERNEL_ACPI_WIFI_NAMESPACE_H
#include <kernel/acpi/wifi.h>
#include <uacpi/namespace.h>
#include <uacpi/uacpi.h>

#define ENDPOINT_ALIAS_CAPACITY 16
struct companion_search {
  uint64_t address;
  size_t visits;
  size_t count;
  uacpi_namespace_node *matches[ENDPOINT_ALIAS_CAPACITY];
  bool disabled;
  enum acpi_wifi_state state;
};

/* Worker only. Missing-root classification and alias logging preserve the
 * Intel consumer contract without adding Realtek qualification logs. */
enum acpi_wifi_state acpi_wifi_find_companion(const struct pci_device *device,
    struct companion_search *endpoints, enum acpi_wifi_state missing_root, bool log_aliases);
enum acpi_wifi_state acpi_wifi_evaluate(uacpi_namespace_node *node, const char *method,
    const uacpi_object_array *arguments, uacpi_object **object);
bool acpi_wifi_take_source(enum acpi_wifi_state *state, enum acpi_wifi_state incoming,
    bool equal);
#endif
