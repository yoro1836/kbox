/* SPDX-License-Identifier: MIT */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "loader-image.h"

static int apply_final_prot(void *addr, size_t size, int prot)
{
    if (size == 0)
        return 0;
    return mprotect(addr, size, prot) == 0 ? 0 : -errno;
}

static int map_region_exact(uint64_t addr, uint64_t size, int flags, void **out)
{
    int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *mapped;

    if (!out || size == 0)
        return -EINVAL;
    if ((flags & MAP_STACK) != 0)
        mmap_flags |= MAP_STACK;
#ifdef MAP_FIXED_NOREPLACE
    mmap_flags |= MAP_FIXED_NOREPLACE;
#else
    mmap_flags |= MAP_FIXED;
#endif

    mapped = mmap((void *) (uintptr_t) addr, (size_t) size,
                  PROT_READ | PROT_WRITE, mmap_flags, -1, 0);

#ifdef MAP_FIXED_NOREPLACE
    /* TOCTOU race: address may have been taken between probe and use.
     * Fall back to MAP_FIXED which replaces the existing mapping.
     */
    if (mapped == MAP_FAILED && errno == EEXIST) {
        mmap_flags &= ~MAP_FIXED_NOREPLACE;
        mmap_flags |= MAP_FIXED;
        mapped = mmap((void *) (uintptr_t) addr, (size_t) size,
                      PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
    }
#endif

    if (mapped == MAP_FAILED)
        return -errno;
    if ((uintptr_t) mapped != (uintptr_t) addr) {
        munmap(mapped, (size_t) size);
        return -EEXIST;
    }
    *out = mapped;
    return 0;
}

static void unmap_recorded_regions(struct kbox_loader_image *image)
{
    while (image && image->region_count > 0) {
        struct kbox_loader_image_region *region =
            &image->regions[image->region_count - 1];

        if (region->addr && region->size > 0)
            munmap(region->addr, region->size);
        image->region_count--;
    }
}

static int record_region(struct kbox_loader_image *image,
                         void *addr,
                         size_t size)
{
    if (!image || image->region_count >= KBOX_LOADER_MAX_MAPPINGS)
        return -1;
    image->regions[image->region_count++] = (struct kbox_loader_image_region) {
        .addr = addr,
        .size = size,
    };
    return 0;
}

static int copy_mapping_bytes(void *mapped,
                              size_t mapped_size,
                              const unsigned char *src,
                              size_t src_len,
                              const struct kbox_loader_mapping *mapping)
{
    if (!mapped || !mapping)
        return -EINVAL;
    if (mapping->file_size == 0)
        return 0;
    if (!src)
        return -EINVAL;
    if (mapping->file_offset > src_len || mapping->file_size > src_len ||
        mapping->file_offset + mapping->file_size > src_len)
        return -EINVAL;
    if (mapping->file_size > mapped_size)
        return -EINVAL;

    memcpy(mapped, src + mapping->file_offset, (size_t) mapping->file_size);
    if (mapping->zero_fill_size > 0) {
        uintptr_t start = (uintptr_t) mapping->zero_fill_start;
        uintptr_t base = (uintptr_t) mapping->addr;

        if (start < base || start - base > mapped_size ||
            mapping->zero_fill_size > mapped_size - (start - base)) {
            return -EINVAL;
        }
        memset((unsigned char *) mapped + (start - base), 0,
               (size_t) mapping->zero_fill_size);
    }
    return 0;
}

void kbox_loader_image_reset(struct kbox_loader_image *image)
{
    if (!image)
        return;
    unmap_recorded_regions(image);
    memset(image, 0, sizeof(*image));
}

int kbox_loader_materialize_image(const struct kbox_loader_image_spec *spec,
                                  struct kbox_loader_image *image)
{
    const struct kbox_loader_layout *layout;

    if (!spec || !image || !spec->layout || !spec->main_elf)
        return -EINVAL;

    kbox_loader_image_reset(image);
    layout = spec->layout;
    int rc = -1;

    for (size_t i = 0; i < layout->mapping_count; i++) {
        const struct kbox_loader_mapping *mapping = &layout->mappings[i];
        const unsigned char *src = NULL;
        size_t src_len = 0;
        void *mapped = NULL;

        if (mapping->size == 0)
            continue;

        switch (mapping->source) {
        case KBOX_LOADER_MAPPING_MAIN:
            src = spec->main_elf;
            src_len = spec->main_elf_len;
            break;
        case KBOX_LOADER_MAPPING_INTERP:
            src = spec->interp_elf;
            src_len = spec->interp_elf_len;
            break;
        case KBOX_LOADER_MAPPING_STACK:
            src = layout->stack.data;
            src_len = layout->stack.size;
            break;
        }

        rc = map_region_exact(mapping->addr, mapping->size, mapping->flags,
                              &mapped);
        if (rc < 0)
            goto fail;
        if (record_region(image, mapped, (size_t) mapping->size) < 0) {
            munmap(mapped, mapping->size);
            rc = -ENOMEM;
            goto fail;
        }

        if (mapping->source == KBOX_LOADER_MAPPING_STACK) {
            uintptr_t start = (uintptr_t) layout->initial_sp;
            uintptr_t base = (uintptr_t) mapping->addr;

            if (!src || start < base || layout->stack.size > mapping->size ||
                start - base > mapping->size - layout->stack.size) {
                rc = -EINVAL;
                goto fail;
            }
            memcpy((unsigned char *) mapped + (start - base), src,
                   layout->stack.size);
        } else {
            rc = copy_mapping_bytes(mapped, (size_t) mapping->size, src,
                                    src_len, mapping);
            if (rc < 0)
                goto fail;
        }

        if (mapping->prot & PROT_EXEC)
            __builtin___clear_cache((char *) mapped,
                                    (char *) mapped + mapping->size);
        rc = apply_final_prot(mapped, (size_t) mapping->size, mapping->prot);
        if (rc < 0)
            goto fail;
    }

    return 0;

fail:
    kbox_loader_image_reset(image);
    return rc;
}
