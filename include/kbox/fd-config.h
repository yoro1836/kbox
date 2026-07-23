/* SPDX-License-Identifier: MIT */
/* fd-config.h - Runtime FD table configuration.
 *
 * KBOX_FD_BASE and KBOX_FD_TABLE_MAX are computed at startup from
 * getrlimit(RLIMIT_NOFILE) so kbox works on Android (limit=32768)
 * and desktop Linux (limit=65536+).
 */

#ifndef KBOX_FD_CONFIG_H
#define KBOX_FD_CONFIG_H

extern long kbox_fd_base;
extern long kbox_fd_table_max;

/* Must be called once at startup before any FD table operations. */
void kbox_fd_config_init(void);

/* Derived constants (computed from kbox_fd_base/kbox_fd_table_max). */
static inline long kbox_fd_fast_base(void)
{
    return kbox_fd_base + (kbox_fd_table_max / 2);
}

static inline long kbox_fd_hostonly_base(void)
{
    return kbox_fd_base + ((kbox_fd_table_max * 3) / 4);
}

static inline long kbox_fd_table_capacity(void)
{
    return kbox_fd_table_max + 1024 + (kbox_fd_base - 1024);
}

#endif
