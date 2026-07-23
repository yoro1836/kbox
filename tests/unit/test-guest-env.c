/* SPDX-License-Identifier: MIT */

#include "guest-env.h"
#include "test-runner.h"

static void test_guest_env_parses_rootfs_environment(void)
{
    static const char contents[] =
        "# guest defaults\n"
        "PATH=/usr/local/bin:/usr/bin\n"
        "HOME = ignored\n"
        "LANG=ko_KR.UTF-8\n"
        "PAGER=\"less -FRX\"\n";
    struct kbox_guest_env env;

    ASSERT_EQ(kbox_guest_env_parse(&env, contents, sizeof(contents) - 1), 0);
    ASSERT_EQ(env.count, 3);
    ASSERT_STREQ(env.entries[0], "PATH=/usr/local/bin:/usr/bin");
    ASSERT_STREQ(env.entries[1], "LANG=ko_KR.UTF-8");
    ASSERT_STREQ(env.entries[2], "PAGER=less -FRX");
    ASSERT_TRUE(env.entries[3] == NULL);
}

static void test_guest_env_uses_last_duplicate_value(void)
{
    static const char contents[] = "PATH=/bin\nPATH=/usr/bin\n";
    struct kbox_guest_env env;

    ASSERT_EQ(kbox_guest_env_parse(&env, contents, sizeof(contents) - 1), 0);
    ASSERT_EQ(env.count, 1);
    ASSERT_STREQ(env.entries[0], "PATH=/usr/bin");
}

static void test_guest_env_is_empty_without_rootfs_file(void)
{
    struct kbox_guest_env env;

    ASSERT_EQ(kbox_guest_env_parse(&env, NULL, 0), 0);
    ASSERT_EQ(env.count, 0);
    ASSERT_TRUE(env.entries[0] == NULL);
}

static void test_guest_env_rejects_oversized_value(void)
{
    char contents[KBOX_GUEST_ENV_VALUE_MAX + 16];
    struct kbox_guest_env env;

    memset(contents, 'a', sizeof(contents));
    memcpy(contents, "LONG=", 5);
    ASSERT_EQ(kbox_guest_env_parse(&env, contents, sizeof(contents)), -1);
}

void test_guest_env_init(void)
{
    TEST_REGISTER(test_guest_env_parses_rootfs_environment);
    TEST_REGISTER(test_guest_env_uses_last_duplicate_value);
    TEST_REGISTER(test_guest_env_is_empty_without_rootfs_file);
    TEST_REGISTER(test_guest_env_rejects_oversized_value);
}
