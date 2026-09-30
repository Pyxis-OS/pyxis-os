#ifndef KERNEL_USER_READINESS_H
#define KERNEL_USER_READINESS_H

#include <kernel/user/wait.h>

/* Owning BSP worker, IF=1. TCP interests require network-worker context.
 * Service unlinks before completion and never accesses completed storage. */
bool readiness_service(struct bsp_request **active);
bool readiness_next_deadline(struct bsp_request *active, uint64_t *deadline);
/* BSP, IF=0; no queue or subsystem retains the request when completed. */
void readiness_complete(struct readiness_request *request, enum call_status status);

/* BSP executor, IF=0. Only requests containing TCP interests enter this queue. */
void net_readiness_submit(struct readiness_request *request);
/* Network worker, IF=1. Transport and direction ownership remain worker-only. */
uint64_t tcp_readiness_events(struct readiness_interest *interest);

#endif
