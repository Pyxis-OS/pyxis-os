#ifndef ARCH_INTEL_DMA_H
#define ARCH_INTEL_DMA_H

#include <kernel/boot.h>
#include <kernel/pci.h>

/* BSP bootstrap, before paging_init. Copies at most 64 KiB of validated DMAR;
 * no firmware pointer survives. Missing/unsupported coverage refuses later. */
void intel_dma_capture_boot(const struct boot_info *boot);

/* BSP/IF=0, after VM/heap and complete PCI discovery, before AP startup.
 * Segment zero, at most 32 units/256 scopes. Under the kernel VM owner, maps the unique
 * covering DRHD's status page RO/UC and requires translation disabled. No
 * remapper/configuration writes or DMA mapping policy are established here.
 * The caller must exclude later IOMMU policy changes for its DMA lifetime.
 * Failure keeps the GOP copy path; uncertain mapping cleanup fences retries. */
bool intel_dma_identity(const struct boot_info *boot, struct pci_address address);

#endif
