# Dynamic FD Table Sizing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use compose:subagent (recommended) or compose:execute to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make kbox's FD table size dynamic based on the actual RLIMIT_NOFILE, so it works on Android (limit=32768) and desktop Linux (limit=65536+).

**Architecture:** Convert KBOX_FD_BASE and KBOX_FD_TABLE_MAX from compile-time macros to runtime variables initialized from getrlimit(RLIMIT_NOFILE) at startup.

**Tech Stack:** C (gnu11), GNU Make, GitHub Actions

## Global Constraints

- KBOX_FD_BASE=32768, KBOX_FD_TABLE_MAX=4096 on desktop Linux (existing behavior)
- On Android, RLIMIT_NOFILE hard limit is 32768
- Guest FDs must fit within the actual RLIMIT_NOFILE
- Host FDs (below KBOX_FD_BASE) must remain usable
- 280 unit tests must continue to pass

---

## Task 1: Add runtime FD table configuration

**Files:**
- Create: `include/kbox/fd-config.h`
- Modify: `src/fd-table.h`
- Modify: `src/image.c`
- Modify: `src/seccomp-dispatch.c`
- Modify: `src/seccomp-supervisor.c`
- Modify: `src/dispatch-internal.h`

**Interfaces:**
- Consumes: getrlimit(RLIMIT_NOFILE) at startup
- Produces: kbox_fd_base, kbox_fd_table_max variables available globally

- [ ] **Step 1: Create fd-config.h with runtime variables**

```c
/* include/kbox/fd-config.h - Runtime FD table configuration.
 *
 * KBOX_FD_BASE and KBOX_FD_TABLE_MAX are computed at startup from
 * getrlimit(RLIMIT_NOFILE) so kbox works on Android (limit=32768)
 * and desktop Linux (limit=65536+).
 */

#ifndef KBOX_FD_CONFIG_H
#define KBOX_FD_CONFIG_H

#include <sys/resource.h>

extern unsigned long kbox_fd_base;
extern unsigned long kbox_fd_table_max;

/* Must be called once at startup before any FD table operations. */
void kbox_fd_config_init(void);

/* Derived constants (computed from kbox_fd_base/kbox_fd_table_max). */
static inline unsigned long kbox_fd_fast_base(void) {
    return kbox_fd_base + (kbox_fd_table_max / 2);
}
static inline unsigned long kbox_fd_hostonly_base(void) {
    return kbox_fd_base + ((kbox_fd_table_max * 3) / 4);
}
static inline unsigned long kbox_fd_table_capacity(void) {
    return kbox_fd_table_max + 1024 + (kbox_fd_base - 1024);
}

#endif
```

- [ ] **Step 2: Create fd-config.c with initialization**

```c
/* src/fd-config.c - Runtime FD table configuration implementation. */

#include <stdio.h>
#include <sys/resource.h>

#include "kbox/fd-config.h"

unsigned long kbox_fd_base = 32768;
unsigned long kbox_fd_table_max = 4096;

void kbox_fd_config_init(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
        return;

    /* Reserve FDs 0..1023 for host use (stdin/stdout/stderr + well-known).
     * Guest FDs start after that.
     */
    unsigned long available = (unsigned long) rl.rlim_cur;
    unsigned long reserve = 1024;

    if (available <= reserve + 1024) {
        fprintf(stderr, "kbox: RLIMIT_NOFILE=%lu too low for FD table\n",
                available);
        return;
    }

    /* Use available space minus reserve for the FD table.
     * Keep KBOX_FD_BASE high enough to leave room for host FDs.
     */
    kbox_fd_base = available / 2;
    kbox_fd_table_max = available - kbox_fd_base;

    /* Ensure minimum table size */
    if (kbox_fd_table_max < 1024)
        kbox_fd_table_max = 1024;

    fprintf(stderr, "kbox: FD config: base=%lu table_max=%lu (RLIMIT_NOFILE=%lu)\n",
            kbox_fd_base, kbox_fd_table_max, available);
}
```

- [ ] **Step 3: Update fd-table.h to use runtime variables**

Replace the macros at lines 18-27 with includes of fd-config.h. The static inline helpers remain.

- [ ] **Step 4: Update image.c to call kbox_fd_config_init() and remove debug logging**

In main() or the early init path, call `kbox_fd_config_init()` before any FD operations.

- [ ] **Step 5: Update seccomp-dispatch.c, seccomp-supervisor.c, dispatch-internal.h**

Replace `KBOX_FD_BASE` references with `kbox_fd_base` and `KBOX_FD_TABLE_MAX` with `kbox_fd_table_max`.

- [ ] **Step 6: Build and run unit tests**

Run: `make clean && make defconfig && make BUILD=release -j$(nproc)`
Run: `make check-unit`
Expected: 280/280 tests pass

- [ ] **Step 7: Commit**

Commit with descriptive message about dynamic FD table sizing.
