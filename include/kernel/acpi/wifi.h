/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_ACPI_WIFI_H
#define KERNEL_ACPI_WIFI_H
#include <kernel/pci.h>

enum acpi_wifi_state {
  ACPI_WIFI_PENDING,
  ACPI_WIFI_ABSENT,
  ACPI_WIFI_DISABLED,
  ACPI_WIFI_VALID,
  ACPI_WIFI_UNSUPPORTED,
  ACPI_WIFI_MALFORMED,
  ACPI_WIFI_FAILED,
};

#define ACPI_WIFI_SAR_CHAINS 4
#define ACPI_WIFI_SAR_BANDS 11
#define ACPI_WIFI_GEO_PROFILES 8
#define ACPI_WIFI_GEO_BANDS 3
#define ACPI_WIFI_DSM_FUNCTIONS 13

struct acpi_wifi_sar {
  enum acpi_wifi_state state;
  uint8_t revision, chains, bands;
  uint32_t flags;
  /* WRDS profile 1; raw byte limits in 1/8 dBm, not a TX grant. */
  uint8_t limits[ACPI_WIFI_SAR_CHAINS][ACPI_WIFI_SAR_BANDS];
};

struct acpi_wifi_ewrd {
  enum acpi_wifi_state state;
  uint8_t revision, chains, bands, profiles;
  uint8_t limits[3][ACPI_WIFI_SAR_CHAINS][ACPI_WIFI_SAR_BANDS];
};

struct acpi_wifi_geo_band {
  uint8_t max, offset_a, offset_b;
};

struct acpi_wifi_geo {
  enum acpi_wifi_state state;
  uint8_t revision, profiles, bands;
  struct acpi_wifi_geo_band table[ACPI_WIFI_GEO_PROFILES][ACPI_WIFI_GEO_BANDS];
};

struct acpi_wifi_ppag {
  enum acpi_wifi_state state;
  uint8_t revision, bands;
  uint32_t flags;
  int8_t gain[2][ACPI_WIFI_SAR_BANDS];
};

struct acpi_wifi_integer {
  enum acpi_wifi_state state;
  uint32_t value;
};

struct acpi_wifi_policy {
  struct pci_address address;
  /* GLAI 0 permits ACPI; 1/2 selects locked UEFI tables, unsupported here. */
  struct acpi_wifi_integer glai;
  struct acpi_wifi_sar sar;
  struct acpi_wifi_ewrd ewrd;
  struct acpi_wifi_geo geo;
  struct acpi_wifi_ppag ppag;
  struct {
    enum acpi_wifi_state state;
    uint32_t supported;
    /* Intel Wi-Fi UUID, revision 0; indexed by its firmware function ID.
     * Only LARI v6 functions 1-4 and 6-9 are reached by this capture. */
    struct acpi_wifi_integer function[ACPI_WIFI_DSM_FUNCTIONS];
  } dsm;
};

/* BSP, IF=0. One immutable, owned AX200 PCI-companion capture. Copies all
 * completed outcomes; PENDING leaves POLICY untouched. No AML pointers escape.
 * VALID means discovery completed, not TX admission: consumers must check the
 * individual sources, their revisions and firmware applicability. ABSENT is
 * possible only after complete discovery, never after a lookup/evaluation error.
 * No refresh or AML calls from the radio worker are supported. */
enum acpi_wifi_state acpi_wifi_snapshot(struct acpi_wifi_policy *policy);
#endif
