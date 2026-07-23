/* SPDX-License-Identifier: MIT */

#include "guest-env.h"

int kbox_guest_env_init(struct kbox_guest_env *env,
                        enum kbox_syscall_mode syscall_mode)
{
    const char *mode;

    if (!env)
        return -1;

    switch (syscall_mode) {
    case KBOX_SYSCALL_MODE_SECCOMP:
        mode = "KBOX_SYSCALL_MODE=seccomp";
        break;
    case KBOX_SYSCALL_MODE_TRAP:
        mode = "KBOX_SYSCALL_MODE=trap";
        break;
    case KBOX_SYSCALL_MODE_REWRITE:
        mode = "KBOX_SYSCALL_MODE=rewrite";
        break;
    case KBOX_SYSCALL_MODE_AUTO:
        mode = "KBOX_SYSCALL_MODE=auto";
        break;
    default:
        return -1;
    }

    env->entries[0] = "HOME=/root";
    env->entries[1] =
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    env->entries[2] = "TERM=xterm-256color";
    env->entries[3] = mode;
    env->entries[4] = NULL;
    env->count = 4;
    return 0;
}
