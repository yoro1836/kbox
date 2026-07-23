/* SPDX-License-Identifier: MIT */

#include <string.h>
#include <sys/mman.h>

#include "loader-stack.h"

/* Use mmap(MAP_ANONYMOUS) instead of malloc/calloc.  In trap mode,
 * the stack builder runs from a SIGSYS signal handler where the guest
 * may hold glibc heap locks.  mmap is async-signal-safe.
 */
static void *signal_safe_alloc(size_t size)
{
    void *p;

    if (size == 0)
        return NULL;
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
             -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static void *signal_safe_zalloc(size_t size)
{
    /* MAP_ANONYMOUS pages are zero-filled by the kernel. */
    return signal_safe_alloc(size);
}

static void signal_safe_free(void *p, size_t size)
{
    if (p && size > 0)
        munmap(p, size);
}

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_EXECFN 31

#define LOADER_STACK_AUXV_BASE 13

static int add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    return __builtin_add_overflow(a, b, out);
}

static uint64_t align_down_u64(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

static int is_power_of_two_u64(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static void write_u64(unsigned char *p, uint64_t value)
{
    memcpy(p, &value, sizeof(value));
}

static int place_blob(unsigned char *buf,
                      uint64_t base,
                      uint64_t *cursor,
                      const void *src,
                      size_t len,
                      uint64_t *addr_out)
{
    if ((uint64_t) len > *cursor - base)
        return -1;
    *cursor -= (uint64_t) len;
    memcpy(buf + (*cursor - base), src, len);
    *addr_out = *cursor;
    return 0;
}

static int place_cstr(unsigned char *buf,
                      uint64_t base,
                      uint64_t *cursor,
                      const char *s,
                      uint64_t *addr_out)
{
    return place_blob(buf, base, cursor, s, strlen(s) + 1, addr_out);
}

void kbox_loader_stack_image_reset(struct kbox_loader_stack_image *image)
{
    if (!image)
        return;
    signal_safe_free(image->data, image->capacity);
    memset(image, 0, sizeof(*image));
}

int kbox_loader_build_initial_stack(const struct kbox_loader_stack_spec *spec,
                                    struct kbox_loader_stack_image *image)
{
    uint64_t stack_base;
    uint64_t cursor;
    uint64_t table_start;
    uint64_t table_size;
    uint64_t auxc;
    uint64_t words;
    uint64_t image_size;
    uint64_t offset;
    unsigned char *buf = NULL;
    size_t buf_size;
    uint64_t *argv_addrs = NULL;
    size_t argv_addrs_size = 0;
    uint64_t *env_addrs = NULL;
    size_t env_addrs_size = 0;
    struct kbox_loader_auxv_entry *auxv = NULL;
    size_t auxv_size = 0;
    size_t auxi = 0;

    if (!spec || !image || !spec->main_plan || !spec->argv || spec->argc == 0)
        return -1;
    if (spec->envc > 0 && !spec->envp)
        return -1;
    kbox_loader_stack_image_reset(image);
    if (!is_power_of_two_u64(spec->page_size) || spec->stack_size == 0)
        return -1;
    if (spec->stack_top < spec->stack_size)
        return -1;

    stack_base = spec->stack_top - spec->stack_size;
    cursor = spec->stack_top;

    buf_size = (size_t) spec->stack_size;
    buf = signal_safe_zalloc(buf_size);
    if (!buf)
        return -1;

    if (__builtin_mul_overflow(spec->argc, sizeof(*argv_addrs),
                               &argv_addrs_size))
        goto fail;
    if (__builtin_mul_overflow(spec->envc ? spec->envc : 1, sizeof(*env_addrs),
                               &env_addrs_size))
        goto fail;
    {
        size_t auxv_count = LOADER_STACK_AUXV_BASE + spec->extra_auxv_count +
                            (spec->interp_plan ? 1 : 0);
        if (__builtin_mul_overflow(auxv_count, sizeof(*auxv), &auxv_size))
            goto fail;
    }
    argv_addrs = signal_safe_zalloc(argv_addrs_size);
    env_addrs = signal_safe_zalloc(env_addrs_size);
    auxv = signal_safe_zalloc(auxv_size);
    if (!argv_addrs || !env_addrs || !auxv)
        goto fail;

    if (place_blob(buf, stack_base, &cursor,
                   spec->random_bytes
                       ? spec->random_bytes
                       : (const unsigned char[KBOX_LOADER_RANDOM_SIZE]){0},
                   KBOX_LOADER_RANDOM_SIZE, &image->random_addr) < 0)
        goto fail;

    if (place_cstr(buf, stack_base, &cursor,
                   spec->execfn ? spec->execfn : spec->argv[0],
                   &image->execfn_addr) < 0)
        goto fail;

    for (size_t i = spec->envc; i > 0; i--) {
        if (place_cstr(buf, stack_base, &cursor, spec->envp[i - 1],
                       &env_addrs[i - 1]) < 0)
            goto fail;
    }

    for (size_t i = spec->argc; i > 0; i--) {
        if (place_cstr(buf, stack_base, &cursor, spec->argv[i - 1],
                       &argv_addrs[i - 1]) < 0)
            goto fail;
    }

    {
        uint64_t phdr_addr;

        if (__builtin_add_overflow(spec->main_load_bias,
                                   spec->main_plan->phdr_vaddr, &phdr_addr))
            goto fail;
        auxv[auxi++] = (struct kbox_loader_auxv_entry){
            .key = AT_PHDR,
            .value = phdr_addr,
        };
    }
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_PHENT,
        .value = spec->main_plan->phentsize,
    };
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_PHNUM,
        .value = spec->main_plan->phnum,
    };
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_PAGESZ,
        .value = spec->page_size,
    };
    if (spec->interp_plan) {
        auxv[auxi++] = (struct kbox_loader_auxv_entry){
            .key = AT_BASE,
            .value = spec->interp_load_bias,
        };
    }
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_FLAGS,
        .value = 0,
    };
    {
        uint64_t entry_addr;

        if (__builtin_add_overflow(spec->main_load_bias, spec->main_plan->entry,
                                   &entry_addr))
            goto fail;
        auxv[auxi++] = (struct kbox_loader_auxv_entry){
            .key = AT_ENTRY,
            .value = entry_addr,
        };
    }
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_UID,
        .value = spec->uid,
    };
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_EUID,
        .value = spec->euid,
    };
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_GID,
        .value = spec->gid,
    };
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_EGID,
        .value = spec->egid,
    };
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_SECURE,
        .value = spec->secure ? 1u : 0u,
    };
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_RANDOM,
        .value = image->random_addr,
    };
    auxv[auxi++] = (struct kbox_loader_auxv_entry){
        .key = AT_EXECFN,
        .value = image->execfn_addr,
    };

    for (size_t i = 0; i < spec->extra_auxv_count; i++)
        auxv[auxi++] = spec->extra_auxv[i];

    auxc = (uint64_t) auxi;
    if (add_overflow_u64(1 + spec->argc + 1 + spec->envc + 1, 2 * (auxc + 1),
                         &words))
        goto fail;
    if (__builtin_mul_overflow(words, (uint64_t) sizeof(uint64_t), &table_size))
        goto fail;
    if (cursor < stack_base + table_size)
        goto fail;

    table_start = align_down_u64(cursor - table_size, 16);
    if (table_start < stack_base)
        goto fail;

    image_size = spec->stack_top - table_start;
    if (image_size > spec->stack_size)
        goto fail;

    image->initial_sp = table_start;
    image->size = (size_t) image_size;

    offset = 0;
    write_u64(buf + (table_start - stack_base) + offset, spec->argc);
    offset += sizeof(uint64_t);
    for (size_t i = 0; i < spec->argc; i++, offset += sizeof(uint64_t))
        write_u64(buf + (table_start - stack_base) + offset, argv_addrs[i]);
    write_u64(buf + (table_start - stack_base) + offset, 0);
    offset += sizeof(uint64_t);
    for (size_t i = 0; i < spec->envc; i++, offset += sizeof(uint64_t))
        write_u64(buf + (table_start - stack_base) + offset, env_addrs[i]);
    write_u64(buf + (table_start - stack_base) + offset, 0);
    offset += sizeof(uint64_t);
    for (size_t i = 0; i < auxi; i++) {
        write_u64(buf + (table_start - stack_base) + offset, auxv[i].key);
        offset += sizeof(uint64_t);
        write_u64(buf + (table_start - stack_base) + offset, auxv[i].value);
        offset += sizeof(uint64_t);
    }
    write_u64(buf + (table_start - stack_base) + offset, AT_NULL);
    offset += sizeof(uint64_t);
    write_u64(buf + (table_start - stack_base) + offset, 0);

    image->capacity = image->size;
    image->data = signal_safe_alloc(image->capacity);
    if (!image->data)
        goto fail;
    memcpy(image->data, buf + (table_start - stack_base), image->size);

    signal_safe_free(buf, buf_size);
    signal_safe_free(argv_addrs, argv_addrs_size);
    signal_safe_free(env_addrs, env_addrs_size);
    signal_safe_free(auxv, auxv_size);
    return 0;

fail:
    signal_safe_free(buf, buf_size);
    signal_safe_free(argv_addrs, argv_addrs_size);
    signal_safe_free(env_addrs, env_addrs_size);
    signal_safe_free(auxv, auxv_size);
    kbox_loader_stack_image_reset(image);
    return -1;
}
