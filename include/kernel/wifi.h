#ifndef KERNEL_WIFI_H
#define KERNEL_WIFI_H

#include <abi/wifi.h>
#include <abi/syscall.h>
#include <stdatomic.h>
#include <kernel/net/interface.h>
#include <stddef.h>
#include <kernel/object/capability.h>

struct wifi_auth;

struct boot_info;
/* Retained by the operation object while queued/active. BSP updates/drain only;
 * cancel is atomic because logical CLOSE can originate on any CPU. */
struct wifi_scan_work {
  uint64_t generation, deadline_ns;
  uint32_t channels;
  atomic_bool cancel;
};

/* The radio retains a storage reference until wifi_connection_retired, even
 * after setup succeeds. Owning grants, not that reference, keep the link. */
struct wifi_connect_work {
  uint64_t generation, deadline_ns;
  uint64_t interface_id;
  uint32_t controller_id;
  struct wifi_target target;
  struct wifi_auth *auth; /* Link-owned storage, never an owning radio grant. */
  struct capability_grant credential; /* Exact sealed FILE grant, including CUT lineage. */
  atomic_bool cancel;
};

void wifi_prepare(const struct boot_info *boot);
void wifi_start(void);
/* BSP interrupt dispatch; selected backend context is retained for late MSI. */
void wifi_interrupt(void);
/* BSP/IF=0. Snapshot only; does not initialize or scan hardware. */
void wifi_driver_status(struct wifi_status_full_reply *status);
enum call_status wifi_driver_policy(const struct wifi_policy_request *policy);
enum call_status wifi_driver_scan(struct wifi_scan_work *work);
/* Any CPU, IF=0: latch/notify only; the BSP worker aborts and drains. */
void wifi_driver_cancel(struct wifi_scan_work *work);
/* BSP/IF=0: elect effects only while the admitted lineage remains live. */
bool wifi_scan_begin(struct wifi_scan_work *work);
enum call_status wifi_scan_stop_reason(struct wifi_scan_work *work);
/* BSP/IF=0, after confirmed completion/drain or controller quarantine. Copies
 * records into the operation-owned result backing; no DMA pointer survives. */
void wifi_scan_finished(struct wifi_scan_work *work, enum call_status status,
    uint32_t channels, const struct wifi_bss_record *records, size_t count,
    uint32_t dropped_records);

/* BSP/IF=0. Admission only; no firmware waits. Success requires one setup
 * callback and one later retirement callback (or failure+retirement). */
enum call_status wifi_driver_connect(struct wifi_connect_work *work);
void wifi_driver_disconnect(struct wifi_connect_work *work);
bool wifi_connect_begin(struct wifi_connect_work *work);
enum call_status wifi_connect_stop_reason(struct wifi_connect_work *work);
/* BSP/IF=0; only after confirmed station/key retirement. */
enum call_status wifi_connect_refresh_auth(struct wifi_connect_work *work);
void wifi_connection_draining(struct wifi_connect_work *work);
void wifi_connection_state(struct wifi_connect_work *work, bool associated,
    bool authenticated, bool data_ready);
void wifi_connect_finished(struct wifi_connect_work *work, enum call_status status);
void wifi_connection_retired(struct wifi_connect_work *work, enum call_status status);

#define WIFI_ETHERNET_BYTES 1514u
struct wifi_network_snapshot {
  uint64_t generation;
  uint32_t controller_id, security;
  bool present, prepared, ready, mac_valid;
  uint8_t mac[6];
};
/* BSP/IF=0; copies only. The radio worker owns firmware and DMA. Queue calls
 * copy complete Ethernet frames and never wait. Generation is the network
 * incarnation, independent of the connection object's authority generation. */
struct wifi_network_statistics {
  uint64_t tx_queued, tx_completed, tx_failed, tx_dropped, rx_queued, rx_dropped;
  uint32_t tx_pending, tx_high_water, rx_high_water, frame_bytes, ring_entries;
};
void wifi_driver_network_snapshot(struct wifi_network_snapshot *snapshot);
void wifi_driver_network_statistics(struct wifi_network_statistics *statistics);
bool wifi_driver_network_bind(uint64_t interface_id);
enum net_result wifi_driver_transmit(uint64_t generation, const void *frame, size_t bytes);
bool wifi_driver_receive(uint64_t generation, void *frame, size_t capacity, size_t *bytes);

#endif
