/* SPDX-License-Identifier: MIT */
/* fd-config.c - Runtime FD table configuration implementation. */

#include <stdio.h>
#include <sys/resource.h>

#include "fd-table.h"
#include "kbox/fd-config.h"

/* Runtime bounds (used for limits, not struct sizes). */
long kbox_fd_base = KBOX_FD_BASE_MAX;
long kbox_fd_table_max = KBOX_FD_TABLE_MAX_MAX;

void kbox_fd_config_init(void)
{
    struct rlimit rl;
    unsigned long available;
    unsigned long required =
        (unsigned long) KBOX_FD_BASE_MAX + KBOX_FD_TABLE_MAX_MAX;
    unsigned long table_max;
    unsigned long base;

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_max == RLIM_INFINITY)
        return;

    /* set_launch_rlimits() raises the soft limit to the hard limit before
     * guest execution, so size the virtual range against that ceiling.
     */
    available = (unsigned long) rl.rlim_max;
    if (available >= required) {
        fprintf(stderr,
                "kbox: FD config: base=%ld table_max=%ld "
                "(RLIMIT_NOFILE=%lu >= required %lu)\n",
                kbox_fd_base, kbox_fd_table_max, available, required);
        return;
    }

    /* Keep the compile-time defaults when the host cannot provide a useful
     * guest range. set_launch_rlimits() will reject the configuration if the
     * hard limit also cannot provide the default range.
     */
    if (available < KBOX_LOW_FD_MAX + KBOX_FD_TABLE_MIN) {
        fprintf(stderr,
                "kbox: RLIMIT_NOFILE=%lu too low for a %d-FD guest table\n",
                available, KBOX_FD_TABLE_MIN);
        return;
    }

    /* Keep all guest FDs below the hard limit used by the launch and never
     * exceed the statically allocated entries[] array. The base also stays
     * at or below KBOX_FD_BASE_MAX, so the fixed mid_fds[] backing store
     * remains valid.
     */
    table_max = available - KBOX_LOW_FD_MAX;
    if (table_max > KBOX_FD_TABLE_MAX_MAX)
        table_max = KBOX_FD_TABLE_MAX_MAX;
    base = available - table_max;

    kbox_fd_base = (long) base;
    kbox_fd_table_max = (long) table_max;

    fprintf(stderr,
            "kbox: FD config: base=%ld table_max=%ld "
            "(RLIMIT_NOFILE=%lu < required %lu, resized)\n",
            kbox_fd_base, kbox_fd_table_max, available, required);
}
