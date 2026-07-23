/* SPDX-License-Identifier: MIT */
#ifndef KBOX_GUEST_ENV_H
#define KBOX_GUEST_ENV_H

#include <stddef.h>

#include "kbox/cli.h"

/* Minimal, host-independent environment for the initial guest process. */
#define KBOX_GUEST_ENV_MAX 5

struct kbox_guest_env {
    const char *entries[KBOX_GUEST_ENV_MAX];
    size_t count;
};

/*
 * Initialize the environment passed to the first guest exec. Host variables
 * such as Android/Termux PATH, PREFIX, and LD_LIBRARY_PATH are intentionally
 * never inherited.
 */
int kbox_guest_env_init(struct kbox_guest_env *env,
                        enum kbox_syscall_mode syscall_mode);

#endif /* KBOX_GUEST_ENV_H */
