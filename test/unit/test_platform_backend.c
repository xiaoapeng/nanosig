/**
 * @file test_platform_backend.c
 * @brief P1b loop-only platform backend runtime smoke test.
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include "platform/port.h"

#include <stdint.h>
#include <string.h>

static int expect_ok(int rc)
{
    return rc == NS_OK ? 0 : 1;
}

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static int test_alloc(void)
{
    void *ptr = ns_platform_alloc(16u);

    if(ptr == (void *)0) return 1;

    memset(ptr, 0, 16u);
    ns_platform_free(ptr);
    return 0;
}

static int test_tls(void)
{
    ns_platform_tls_key_t *key = (ns_platform_tls_key_t *)0;
    void *value = (void *)0;
    int marker = 42;

    if(expect_ok(ns_platform_tls_key_create(&key)) != 0) return 1;
    if(expect_ok(ns_platform_tls_get(key, &value)) != 0) return 1;
    if(expect_true(value == (void *)0) != 0) return 1;
    if(expect_ok(ns_platform_tls_set(key, &marker)) != 0) return 1;
    if(expect_ok(ns_platform_tls_get(key, &value)) != 0) return 1;
    if(expect_true(value == &marker) != 0) return 1;
    if(expect_ok(ns_platform_tls_set(key, (void *)0)) != 0) return 1;
    if(expect_ok(ns_platform_tls_key_destroy(key)) != 0) return 1;

    return 0;
}

static int test_mutex(void)
{
    ns_platform_mutex_t *mutex = (ns_platform_mutex_t *)0;

    if(expect_ok(ns_platform_mutex_create(&mutex, "test-mutex")) != 0) return 1;
    if(expect_ok(ns_platform_mutex_lock(mutex)) != 0) return 1;
    if(expect_ok(ns_platform_mutex_unlock(mutex)) != 0) return 1;
    if(expect_ok(ns_platform_mutex_destroy(mutex)) != 0) return 1;

    return 0;
}

static int test_wakeup(void)
{
    ns_platform_wakeup_t *wakeup = (ns_platform_wakeup_t *)0;
    ns_platform_wait_result_t wait_result = NS_PLATFORM_WAIT_SIGNALED;

    if(expect_ok(ns_platform_wakeup_create(&wakeup, "test-wakeup")) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_wait(wakeup, 0u, &wait_result)) != 0) return 1;
    if(expect_true(wait_result == NS_PLATFORM_WAIT_TIMEOUT) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_signal(wakeup)) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_wait(wakeup, NS_PLATFORM_WAIT_INFINITE_US, &wait_result)) != 0) return 1;
    if(expect_true(wait_result == NS_PLATFORM_WAIT_SIGNALED) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_signal(wakeup)) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_reset(wakeup)) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_wait(wakeup, 0u, &wait_result)) != 0) return 1;
    if(expect_true(wait_result == NS_PLATFORM_WAIT_TIMEOUT) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_destroy(wakeup)) != 0) return 1;

    return 0;
}

static int test_clock(void)
{
    ns_platform_time_us_t first = 0u;
    ns_platform_time_us_t second = 0u;

    if(expect_ok(ns_platform_clock_monotonic_us(&first)) != 0) return 1;
    if(expect_ok(ns_platform_clock_monotonic_us(&second)) != 0) return 1;
    if(expect_true(second >= first) != 0) return 1;

    return 0;
}

int main(void)
{
    if(expect_ok(ns_platform_init()) != 0) return 1;
    if(test_alloc() != 0) return 1;
    if(test_tls() != 0) return 1;
    if(test_mutex() != 0) return 1;
    if(test_wakeup() != 0) return 1;
    if(test_clock() != 0) return 1;
    if(expect_ok(ns_platform_shutdown()) != 0) return 1;

    return 0;
}
