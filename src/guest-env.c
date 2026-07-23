/* SPDX-License-Identifier: MIT */

#include "guest-env.h"

#include <ctype.h>
#include <string.h>

void kbox_guest_env_reset(struct kbox_guest_env *env)
{
    if (!env)
        return;
    memset(env, 0, sizeof(*env));
}

static int valid_name(const char *name, size_t len)
{
    if (len == 0 || !(isalpha((unsigned char) name[0]) || name[0] == '_'))
        return 0;
    for (size_t i = 1; i < len; i++) {
        if (!(isalnum((unsigned char) name[i]) || name[i] == '_'))
            return 0;
    }
    return 1;
}

static int add_entry(struct kbox_guest_env *env,
                     const char *line,
                     size_t line_len)
{
    const char *equals = memchr(line, '=', line_len);
    size_t name_len;
    size_t value_start;
    size_t value_end;
    size_t entry_len;
    size_t slot = env->count;

    if (!equals)
        return 0;
    name_len = (size_t) (equals - line);
    if (!valid_name(line, name_len))
        return 0;

    for (size_t i = 0; i < env->count; i++) {
        if (strncmp(env->entries[i], line, name_len) == 0 &&
            env->entries[i][name_len] == '=') {
            slot = i;
            break;
        }
    }
    if (slot == KBOX_GUEST_ENV_MAX)
        return -1;

    value_start = name_len + 1;
    while (value_start < line_len && isspace((unsigned char) line[value_start]))
        value_start++;
    value_end = line_len;
    while (value_end > value_start &&
           isspace((unsigned char) line[value_end - 1]))
        value_end--;
    if (value_end - value_start >= 2 &&
        ((line[value_start] == '"' && line[value_end - 1] == '"') ||
         (line[value_start] == '\'' && line[value_end - 1] == '\''))) {
        value_start++;
        value_end--;
    }

    entry_len = name_len + 1 + value_end - value_start;
    if (entry_len >= KBOX_GUEST_ENV_VALUE_MAX)
        return -1;
    memcpy(env->values[slot], line, name_len);
    env->values[slot][name_len] = '=';
    memcpy(env->values[slot] + name_len + 1, line + value_start,
           value_end - value_start);
    env->values[slot][entry_len] = '\0';
    env->entries[slot] = env->values[slot];
    if (slot == env->count) {
        env->count++;
        env->entries[env->count] = NULL;
    }
    return 0;
}

int kbox_guest_env_parse(struct kbox_guest_env *env,
                         const char *contents,
                         size_t contents_len)
{
    size_t offset = 0;

    if (!env || (!contents && contents_len != 0))
        return -1;
    kbox_guest_env_reset(env);
    while (offset < contents_len) {
        const char *line = contents + offset;
        size_t line_len = 0;
        int rc;

        while (offset + line_len < contents_len &&
               contents[offset + line_len] != '\n')
            line_len++;
        offset += line_len;
        if (offset < contents_len)
            offset++;
        if (line_len > 0 && line[line_len - 1] == '\r')
            line_len--;
        while (line_len > 0 && isspace((unsigned char) line[0])) {
            line++;
            line_len--;
        }
        if (line_len == 0 || line[0] == '#')
            continue;
        rc = add_entry(env, line, line_len);
        if (rc < 0)
            return rc;
    }
    return 0;
}
