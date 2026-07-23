/* SPDX-License-Identifier: MIT */

#include "guest-env.h"
#include "test-runner.h"

static void test_guest_env_is_minimal_and_host_independent(void)
{
    struct kbox_guest_env env;

    ASSERT_EQ(kbox_guest_env_init(&env, KBOX_SYSCALL_MODE_AUTO), 0);
    ASSERT_EQ(env.count, 4);
    ASSERT_STREQ(env.entries[0], "HOME=/root");
    ASSERT_STREQ(
        env.entries[1],
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
    ASSERT_STREQ(env.entries[2], "TERM=xterm-256color");
    ASSERT_STREQ(env.entries[3], "KBOX_SYSCALL_MODE=auto");
    ASSERT_TRUE(env.entries[4] == NULL);
}

static void test_guest_env_sets_each_syscall_mode(void)
{
    struct kbox_guest_env env;

    ASSERT_EQ(kbox_guest_env_init(&env, KBOX_SYSCALL_MODE_SECCOMP), 0);
    ASSERT_STREQ(env.entries[3], "KBOX_SYSCALL_MODE=seccomp");
    ASSERT_EQ(kbox_guest_env_init(&env, KBOX_SYSCALL_MODE_TRAP), 0);
    ASSERT_STREQ(env.entries[3], "KBOX_SYSCALL_MODE=trap");
    ASSERT_EQ(kbox_guest_env_init(&env, KBOX_SYSCALL_MODE_REWRITE), 0);
    ASSERT_STREQ(env.entries[3], "KBOX_SYSCALL_MODE=rewrite");
}

static void test_guest_env_rejects_invalid_mode(void)
{
    struct kbox_guest_env env;

    ASSERT_EQ(kbox_guest_env_init(&env, (enum kbox_syscall_mode) - 1), -1);
    ASSERT_EQ(kbox_guest_env_init(NULL, KBOX_SYSCALL_MODE_AUTO), -1);
}

void test_guest_env_init(void)
{
    TEST_REGISTER(test_guest_env_is_minimal_and_host_independent);
    TEST_REGISTER(test_guest_env_sets_each_syscall_mode);
    TEST_REGISTER(test_guest_env_rejects_invalid_mode);
}
