#ifndef AX200_STATION_PRIVATE_H
#define AX200_STATION_PRIVATE_H

#include <kernel/wifi.h>

/* Sole BSP radio worker, IF=1. Helpers yield through the existing transport. */
bool ax200_radio_command(uint8_t group, uint8_t opcode, const void *payload,
    size_t bytes, void *reply, size_t capacity, size_t *received, uint64_t deadline);
bool ax200_radio_target_scan(struct wifi_connect_work *work);
bool ax200_radio_transmit_policy(unsigned channel, uint64_t deadline, const char **failure);
uint32_t ax200_radio_rx_antennas(void);
uint32_t ax200_radio_tx_antennas(void);
bool ax200_radio_tx_allowed(unsigned channel);
/* Outside RX delivery; unresolved notice bursts retire a link, not the radio. */
enum call_status ax200_radio_refresh(uint64_t deadline);
bool ax200_radio_faulted(void);
void ax200_radio_detail(const char *detail);
void ax200_radio_quarantine(const char *reason);
void ax200_radio_connection_clear(struct wifi_connect_work *work);
void ax200_station_observation_begin(void);
void ax200_station_run(struct wifi_connect_work *work);
void ax200_station_notify(uint8_t group, uint8_t opcode, const uint8_t *payload, size_t bytes);
uint64_t ax200_station_interface_id(void);

#endif
