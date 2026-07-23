/* SPDX-License-Identifier: MIT */

#include <string.h>

#include "loader-layout.h"

static int align_up_u64(uint64_t value, uint64_t align, uint64_t *out)
{
    uint64_t addend;

    if (align == 0 || (align & (align - 1)) != 0)
        return -1;
    addend = align - 1;
    if (__builtin_add_overflow(value, addend, out))
        return -1;
    *out &= ~addend;
    return 0;
}

static int segment_prot(uint32_t flags)
{
    int prot = 0;

    if ((flags & 0x4u) != 0)
        prot |= PROT_READ;
    if ((flags & 0x2u) != 0)
        prot |= PROT_WRITE;
    if ((flags & 0x1u) != 0)
        prot |= PROT_EXEC;
    return prot;
}

static int loader_stack_prot(const struct kbox_loader_layout *layout)
{
    uint32_t stack_flags = layout->main_plan.stack_flags;

    if (layout->has_interp)
        stack_flags |= layout->interp_plan.stack_flags;
    return PROT_READ | PROT_WRITE |
           (((stack_flags & 0x1u) != 0) ? PROT_EXEC : 0);
}

static uint64_t effective_load_bias(const struct kbox_elf_load_plan *plan,
                                    uint64_t requested_bias)
{
    if (!plan)
        return 0;
    return plan->pie ? requested_bias : 0;
}

static int append_plan_mappings(struct kbox_loader_layout *layout,
                                const struct kbox_elf_load_plan *plan,
                                uint64_t page_size,
                                uint64_t load_bias,
                                enum kbox_loader_mapping_source source)
{
    for (size_t i = 0; i < plan->segment_count; i++) {
        const struct kbox_elf_load_segment *seg = &plan->segments[i];
        uint64_t seg_end;
        uint64_t file_end;
        uint64_t file_map_end;
        uint64_t bss_map_end;
        uint64_t zero_fill_size = 0;
        struct kbox_loader_mapping *mapping;

        if (__builtin_add_overflow(seg->vaddr, seg->mem_size, &seg_end))
            return -1;
        if (align_up_u64(seg_end, seg->map_align, &bss_map_end) < 0)
            return -1;

        if (seg->file_size == 0) {
            uint64_t biased_addr;

            if (__builtin_add_overflow(load_bias, seg->map_start, &biased_addr))
                return -1;
            if (layout->mapping_count >= KBOX_LOADER_MAX_MAPPINGS)
                return -1;
            mapping = &layout->mappings[layout->mapping_count++];
            *mapping = (struct kbox_loader_mapping) {
                .addr = biased_addr,
                .size = seg->map_size,
                .file_offset = 0,
                .file_size = 0,
                .zero_fill_start = 0,
                .zero_fill_size = 0,
                .prot = segment_prot(seg->flags),
                .flags = MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                .source = source,
            };
            continue;
        }

        if (__builtin_add_overflow(seg->vaddr, seg->file_size, &file_end))
            return -1;
        if (align_up_u64(file_end, seg->map_align, &file_map_end) < 0)
            return -1;
        if (file_map_end < seg->map_start || bss_map_end < file_map_end)
            return -1;
        if (seg->mem_size > seg->file_size && file_map_end > file_end)
            zero_fill_size = file_map_end - file_end;

        {
            uint64_t biased_addr;
            uint64_t biased_zf;
            uint64_t biased_bss;

            if (__builtin_add_overflow(load_bias, seg->map_start, &biased_addr))
                return -1;
            if (zero_fill_size &&
                __builtin_add_overflow(load_bias, file_end, &biased_zf))
                return -1;
            if (layout->mapping_count >= KBOX_LOADER_MAX_MAPPINGS)
                return -1;
            mapping = &layout->mappings[layout->mapping_count++];
            *mapping = (struct kbox_loader_mapping) {
                .addr = biased_addr,
                .size = file_map_end - seg->map_start,
                .file_offset = seg->map_offset,
                .file_size =
                    seg->file_size + (seg->file_offset - seg->map_offset),
                .zero_fill_start = zero_fill_size ? biased_zf : 0,
                .zero_fill_size = zero_fill_size,
                .prot = segment_prot(seg->flags),
                .flags = MAP_PRIVATE | MAP_FIXED,
                .source = source,
            };

            if (bss_map_end > file_map_end) {
                if (__builtin_add_overflow(load_bias, file_map_end,
                                           &biased_bss))
                    return -1;
                if (layout->mapping_count >= KBOX_LOADER_MAX_MAPPINGS)
                    return -1;
                mapping = &layout->mappings[layout->mapping_count++];
                *mapping = (struct kbox_loader_mapping) {
                    .addr = biased_bss,
                    .size = bss_map_end - file_map_end,
                    .file_offset = 0,
                    .file_size = 0,
                    .zero_fill_start = 0,
                    .zero_fill_size = 0,
                    .prot = segment_prot(seg->flags),
                    .flags = MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                    .source = source,
                };
            }
        }
    }

    return 0;
}

void kbox_loader_layout_reset(struct kbox_loader_layout *layout)
{
    if (!layout)
        return;
    kbox_loader_stack_image_reset(&layout->stack);
    memset(layout, 0, sizeof(*layout));
}

int kbox_loader_build_layout(const struct kbox_loader_layout_spec *spec,
                             struct kbox_loader_layout *layout)
{
    struct kbox_loader_stack_spec stack_spec;
    uint64_t main_load_bias;
    uint64_t interp_load_bias = 0;

    if (!spec || !layout || !spec->main_elf || spec->main_elf_len == 0 ||
        !spec->argv || spec->argc == 0)
        return -1;

    kbox_loader_layout_reset(layout);

    if (kbox_build_elf_load_plan(spec->main_elf, spec->main_elf_len,
                                 spec->page_size, &layout->main_plan) < 0)
        return -1;

    if (spec->interp_elf && spec->interp_elf_len > 0) {
        if (kbox_build_elf_load_plan(spec->interp_elf, spec->interp_elf_len,
                                     spec->page_size,
                                     &layout->interp_plan) < 0) {
            kbox_loader_layout_reset(layout);
            return -1;
        }
        layout->has_interp = 1;
    }

    main_load_bias =
        effective_load_bias(&layout->main_plan, spec->main_load_bias);
    if (layout->has_interp) {
        interp_load_bias =
            effective_load_bias(&layout->interp_plan, spec->interp_load_bias);
    }

    memset(&stack_spec, 0, sizeof(stack_spec));
    stack_spec.argv = spec->argv;
    stack_spec.argc = spec->argc;
    stack_spec.envp = spec->envp;
    stack_spec.envc = spec->envc;
    stack_spec.execfn = spec->execfn;
    stack_spec.random_bytes = spec->random_bytes;
    stack_spec.extra_auxv = spec->extra_auxv;
    stack_spec.extra_auxv_count = spec->extra_auxv_count;
    stack_spec.main_plan = &layout->main_plan;
    stack_spec.interp_plan = layout->has_interp ? &layout->interp_plan : NULL;
    stack_spec.main_load_bias = main_load_bias;
    stack_spec.interp_load_bias = interp_load_bias;
    stack_spec.page_size = spec->page_size;
    stack_spec.stack_top = spec->stack_top;
    stack_spec.stack_size =
        spec->stack_size ? spec->stack_size : KBOX_LOADER_DEFAULT_STACK_SIZE;
    stack_spec.uid = spec->uid;
    stack_spec.euid = spec->euid;
    stack_spec.gid = spec->gid;
    stack_spec.egid = spec->egid;
    stack_spec.secure = spec->secure;

    if (kbox_loader_build_initial_stack(&stack_spec, &layout->stack) < 0) {
        kbox_loader_layout_reset(layout);
        return -1;
    }

    layout->main_load_bias = main_load_bias;
    layout->interp_load_bias = interp_load_bias;
    layout->stack_top = stack_spec.stack_top;
    layout->stack_size = stack_spec.stack_size;
    layout->initial_sp = layout->stack.initial_sp;

    if (append_plan_mappings(layout, &layout->main_plan, spec->page_size,
                             main_load_bias, KBOX_LOADER_MAPPING_MAIN) < 0) {
        kbox_loader_layout_reset(layout);
        return -1;
    }
    if (layout->has_interp &&
        append_plan_mappings(layout, &layout->interp_plan, spec->page_size,
                             interp_load_bias,
                             KBOX_LOADER_MAPPING_INTERP) < 0) {
        kbox_loader_layout_reset(layout);
        return -1;
    }
    if (layout->mapping_count >= KBOX_LOADER_MAX_MAPPINGS) {
        kbox_loader_layout_reset(layout);
        return -1;
    }
    if (layout->stack_size > layout->stack_top) {
        kbox_loader_layout_reset(layout);
        return -1;
    }
    layout->mappings[layout->mapping_count++] = (struct kbox_loader_mapping) {
        .addr = layout->stack_top - layout->stack_size,
        .size = layout->stack_size,
        .file_offset = 0,
        .file_size = 0,
        .zero_fill_start = 0,
        .zero_fill_size = 0,
        .prot = loader_stack_prot(layout),
        .flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
        .source = KBOX_LOADER_MAPPING_STACK,
    };

    {
        uint64_t pc;
        uint64_t bias = layout->has_interp ? interp_load_bias : main_load_bias;
        uint64_t entry = layout->has_interp ? layout->interp_plan.entry
                                            : layout->main_plan.entry;

        if (__builtin_add_overflow(bias, entry, &pc)) {
            kbox_loader_layout_reset(layout);
            return -1;
        }
        layout->initial_pc = pc;
    }
    return 0;
}
