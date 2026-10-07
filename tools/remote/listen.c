#define _POSIX_C_SOURCE 200809L

#include "listen.h"
#include <remote/beacon.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BEACON_INTERVAL_MS 1000

static int64_t monotonic_ms(void)
{
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) < 0) {
    return -1;
  }
  return (int64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static int nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int listen_host(const char *name, const char *host, uint16_t port,
    const char *beacon_address, const volatile sig_atomic_t *interrupted)
{
  struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(port)};
  struct sockaddr_in destination = {
    .sin_family = AF_INET, .sin_port = htons(REMOTE_BEACON_PORT)
  };
  if (inet_pton(AF_INET, host, &local.sin_addr) != 1 ||
      inet_pton(AF_INET, beacon_address, &destination.sin_addr) != 1) {
    fprintf(stderr, "pyxis-remote: listen host and beacon address must be numeric IPv4 addresses\n");
    return -1;
  }
  unsigned char beacon[REMOTE_BEACON_MAX];
  size_t beacon_size = remote_beacon_encode(beacon, name, port);
  if (!beacon_size) {
    fprintf(stderr, "pyxis-remote: invalid beacon name or TCP port\n");
    return -1;
  }
  int listener = -1;
  int advertiser = -1;
  int connected = -1;
  const char *operation = "listen socket";
  listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    goto failed;
  }
  int enabled = 1;
  operation = "listen socket options";
  if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0 ||
      nonblocking(listener) < 0) {
    goto failed;
  }
  operation = "listen bind";
  if (bind(listener, (struct sockaddr *)&local, sizeof(local)) < 0) {
    goto failed;
  }
  operation = "listen";
  if (listen(listener, 1) < 0) {
    goto failed;
  }
  operation = "beacon socket";
  advertiser = socket(AF_INET, SOCK_DGRAM, 0);
  if (advertiser < 0) {
    goto failed;
  }
  operation = "beacon socket options";
  if (setsockopt(advertiser, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) < 0 ||
      nonblocking(advertiser) < 0) {
    goto failed;
  }
  local.sin_port = 0;
  operation = "beacon bind";
  if (bind(advertiser, (struct sockaddr *)&local, sizeof(local)) < 0) {
    goto failed;
  }

  int64_t next_beacon = 0;
  while (!*interrupted) {
    operation = "beacon clock";
    int64_t now = monotonic_ms();
    if (now < 0) {
      goto failed;
    }
    if (now >= next_beacon) {
      operation = "beacon send";
      ssize_t sent = sendto(advertiser, beacon, beacon_size, 0,
          (struct sockaddr *)&destination, sizeof(destination));
      if (sent < 0 && errno == EINTR) {
        continue;
      }
      if (sent < 0) {
        goto failed;
      }
      if ((size_t)sent != beacon_size) {
        errno = EIO;
        goto failed;
      }
      next_beacon = now + BEACON_INTERVAL_MS;
    }
    struct pollfd interest = {.fd = listener, .events = POLLIN};
    operation = "listen poll";
    int count = poll(&interest, 1, (int)(next_beacon - now));
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      goto failed;
    }
    if (*interrupted) {
      break;
    }
    if (interest.revents & POLLIN) {
      operation = "accept";
      connected = accept(listener, NULL, NULL);
      if (connected < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        continue;
      }
      if (connected < 0) {
        goto failed;
      }
      operation = "accepted socket options";
      if (nonblocking(connected) < 0) {
        goto failed;
      }
      close(advertiser);
      close(listener);
      return connected;
    }
    if (interest.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      errno = EIO;
      goto failed;
    }
  }
  close(advertiser);
  close(listener);
  return -1;

failed:
  int failure = errno;
  if (connected >= 0) {
    close(connected);
  }
  if (advertiser >= 0) {
    close(advertiser);
  }
  if (listener >= 0) {
    close(listener);
  }
  if (!*interrupted) {
    fprintf(stderr, "pyxis-remote: %s: %s\n", operation, strerror(failure));
  }
  return -1;
}
