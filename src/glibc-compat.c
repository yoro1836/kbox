/* SPDX-License-Identifier: MIT */
/* glibc-compat.c - Bionic compatibility shims for glibc symbols.
 *
 * The prebuilt LKL library (liblkl.a) is compiled with glibc and
 * references glibc-specific symbols that do not exist in Android's
 * Bionic libc. This file provides minimal implementations that
 * bridge the gap. Only compiled when IS_ANDROID is set.
 */

#ifdef __ANDROID__

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int *__errno_location(void)
{
    return __errno();
}

void __longjmp_chk(jmp_buf env, int val)
{
    longjmp(env, val);
}

void __assert_fail(const char *expr,
                   const char *file,
                   unsigned int line,
                   const char *func)
{
    fprintf(stderr, "Assertion failed: %s (%s: %s: %u)\n", expr, file, func,
            line);
    abort();
}

/* format-ok: passthrough shim for glibc __fprintf_chk */
int __fprintf_chk(FILE *stream, int flag, const char *fmt, ...)
{
    va_list ap;
    (void) flag;
    va_start(ap, fmt);
    int ret = vfprintf(stream, fmt, ap); /* format-ok */
    va_end(ap);
    return ret;
}

int __isoc99_sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsscanf(str, fmt, ap);
    va_end(ap);
    return ret;
}

long int __fdelt_chk(long int fd)
{
    if (fd < 0 || fd >= 1024)
        abort();
    return fd / (sizeof(unsigned long) * 8);
}

#endif /* __ANDROID__ */
