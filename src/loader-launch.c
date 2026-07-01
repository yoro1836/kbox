/* SPDX-License-Identifier: MIT */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "loader-launch.h"

static int read_fd_all(int fd, unsigned char **buf_out, size_t *len_out)
{
    struct stat st;
    unsigned char *buf = NULL;
    size_t len;
    size_t total = 0;

    if (fd < 0 || !buf_out || !len_out)
        return -1;
    if (fstat(fd, &st) < 0 || st.st_size < 0)
        return -1;
    len = (size_t) st.st_size;
    if (len == 0)
        return -1;
    /* Use mmap(MAP_ANONYMOUS) instead of malloc.  In trap mode,
     * read_fd_all may be called from a signal handler where glibc's
     * malloc is not safe (the guest process may hold malloc locks).
     * mmap is async-signal-safe and avoids the shared heap.
     */
    buf = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
               -1, 0);
    if (buf == MAP_FAILED)
        return -1;

    while (total < len) {
        ssize_t nr = pread(fd, buf + total, len - total, (off_t) total);

        if (nr < 0) {
            if (errno == EINTR)
                continue;
            munmap(buf, len);
            return -1;
        }
        if (nr == 0) {
            munmap(buf, len);
            return -1;
        }
        total += (size_t) nr;
    }

    *buf_out = buf;
    *len_out = len;
    return 0;
}

void kbox_loader_launch_reset(struct kbox_loader_launch *launch)
{
    if (!launch)
        return;
    kbox_loader_image_reset(&launch->image);
    kbox_loader_layout_reset(&launch->layout);
    if (launch->interp_elf && launch->interp_elf_len > 0)
        munmap(launch->interp_elf, launch->interp_elf_len);
    if (launch->main_elf && launch->main_elf_len > 0)
        munmap(launch->main_elf, launch->main_elf_len);
    memset(launch, 0, sizeof(*launch));
}

int kbox_loader_prepare_launch(const struct kbox_loader_launch_spec *spec,
                               struct kbox_loader_launch *launch)
{
    struct kbox_loader_layout_spec layout_spec;
    struct kbox_loader_image_spec image_spec;

    if (!spec || !launch || spec->exec_fd < 0 || !spec->argv || spec->argc == 0)
        return -1;

    kbox_loader_launch_reset(launch);

    if (read_fd_all(spec->exec_fd, &launch->main_elf, &launch->main_elf_len) <
        0) {
        fprintf(stderr, "kbox: loader: read_fd_all(exec_fd=%d) failed\n",
                spec->exec_fd);
        goto fail;
    }
    if (spec->interp_fd >= 0 &&
        read_fd_all(spec->interp_fd, &launch->interp_elf,
                    &launch->interp_elf_len) < 0) {
        fprintf(stderr, "kbox: loader: read_fd_all(interp_fd=%d) failed\n",
                spec->interp_fd);
        goto fail;
    }

    memset(&layout_spec, 0, sizeof(layout_spec));
    layout_spec.main_elf = launch->main_elf;
    layout_spec.main_elf_len = launch->main_elf_len;
    layout_spec.interp_elf = launch->interp_elf;
    layout_spec.interp_elf_len = launch->interp_elf_len;
    layout_spec.argv = spec->argv;
    layout_spec.argc = spec->argc;
    layout_spec.envp = spec->envp;
    layout_spec.envc = spec->envc;
    layout_spec.execfn = spec->execfn;
    layout_spec.random_bytes = spec->random_bytes;
    layout_spec.extra_auxv = spec->extra_auxv;
    layout_spec.extra_auxv_count = spec->extra_auxv_count;
    layout_spec.page_size = spec->page_size;
    layout_spec.stack_top = spec->stack_top;
    layout_spec.stack_size = spec->stack_size;
    layout_spec.main_load_bias = spec->main_load_bias;
    layout_spec.interp_load_bias = spec->interp_load_bias;
    layout_spec.uid = spec->uid;
    layout_spec.euid = spec->euid;
    layout_spec.gid = spec->gid;
    layout_spec.egid = spec->egid;
    layout_spec.secure = spec->secure;

    if (kbox_loader_build_layout(&layout_spec, &launch->layout) < 0) {
        fprintf(stderr, "kbox: loader: build_layout failed\n");
        goto fail;
    }

    memset(&image_spec, 0, sizeof(image_spec));
    image_spec.layout = &launch->layout;
    image_spec.main_elf = launch->main_elf;
    image_spec.main_elf_len = launch->main_elf_len;
    image_spec.interp_elf = launch->interp_elf;
    image_spec.interp_elf_len = launch->interp_elf_len;

    if (kbox_loader_materialize_image(&image_spec, &launch->image) < 0) {
        fprintf(stderr, "kbox: loader: materialize_image failed\n");
        goto fail;
    }
    if (kbox_loader_build_handoff(&launch->layout, &launch->image,
                                  &launch->handoff) < 0) {
        fprintf(stderr, "kbox: loader: build_handoff failed\n");
        goto fail;
    }
    if (kbox_loader_prepare_transfer(&launch->handoff, &launch->transfer) < 0) {
        fprintf(stderr, "kbox: loader: prepare_transfer failed\n");
        goto fail;
    }
    return 0;

fail:
    kbox_loader_launch_reset(launch);
    return -1;
}

int kbox_loader_collect_exec_ranges(const struct kbox_loader_launch *launch,
                                    struct kbox_loader_exec_range *ranges,
                                    size_t range_cap,
                                    size_t *range_count)
{
    size_t count = 0;

    if (!launch || !ranges || !range_count)
        return -1;

    for (size_t i = 0; i < launch->layout.mapping_count; i++) {
        const struct kbox_loader_mapping *mapping = &launch->layout.mappings[i];
        uint64_t end;

        if (mapping->size == 0 || (mapping->prot & PROT_EXEC) == 0)
            continue;
        if (mapping->source != KBOX_LOADER_MAPPING_MAIN &&
            mapping->source != KBOX_LOADER_MAPPING_INTERP) {
            continue;
        }
        if (__builtin_add_overflow(mapping->addr, mapping->size, &end))
            return -1;
        if (count >= range_cap)
            return -1;

        ranges[count].start = mapping->addr;
        ranges[count].end = end;
        count++;
    }

    *range_count = count;
    return count > 0 ? 0 : -1;
}
