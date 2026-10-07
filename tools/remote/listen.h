#ifndef REMOTE_LISTEN_H
#define REMOTE_LISTEN_H

#include <signal.h>
#include <stdint.h>

int listen_host(const char *name, const char *host, uint16_t port,
    const char *beacon_address, const volatile sig_atomic_t *interrupted);

#endif
