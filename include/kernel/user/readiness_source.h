#ifndef KERNEL_USER_READINESS_SOURCE_H
#define KERNEL_USER_READINESS_SOURCE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

struct readiness_registration;

/* Embedded in a waitable source, initialized to zero before publication.
 * Observation references, not logical grants, keep the containing source alive.
 * Links/cursors belong to the readiness-set queue guard, never the actor lock. */
struct readiness_source {
  atomic_size_t watchers;
  struct readiness_registration *first;
  size_t domains[2];
  struct readiness_source *next[2];
  struct readiness_source **previous[2];
  struct readiness_registration *cursor[2];
  bool again[2];
};

/* Any CPU, preserves IF. Caller retains source storage and has released its
 * actor lock. No allocation, callback or walk through the subscriptions. */
void readiness_source_notify(struct readiness_source *source);

#endif
