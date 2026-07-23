/* SPDX-License-Identifier: MIT */
#ifndef KBOX_GUEST_ENV_H
#define KBOX_GUEST_ENV_H

#include <stddef.h>

/* Environment from the guest rootfs's /etc/environment. */
#define KBOX_GUEST_ENV_MAX 64
#define KBOX_GUEST_ENV_VALUE_MAX 256

struct kbox_guest_env {
    char values[KBOX_GUEST_ENV_MAX][KBOX_GUEST_ENV_VALUE_MAX];
    const char *entries[KBOX_GUEST_ENV_MAX + 1];
    size_t count;
};

/* Reset to an empty environment. */
void kbox_guest_env_reset(struct kbox_guest_env *env);

/* Parse NAME=VALUE records from an /etc/environment file. */
int kbox_guest_env_parse(struct kbox_guest_env *env,
                         const char *contents,
                         size_t contents_len);

#endif /* KBOX_GUEST_ENV_H */
