/**
 * @file test_timer.c
 * @brief P6 phase-1 timer manager unit tests.
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig.h>

#include "src/ns_timer_mgr.h"

#include <stdio.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__unix__) || defined(__linux__)
#include <sched.h>
#else
#error "test_timer requires a supported yield primitive"
#endif

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

#define EXPECT_OK(expr) \
    do { \
        if(expect_true((expr)) != 0){ \
            fprintf(stderr, "EXPECT failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while(0)

#define EXPECT_EQ(a, b) \
    do { \
        if(expect_true((a) == (b)) != 0){ \
            fprintf(stderr, "EXPECT failed at %s:%d: %s == %s (%lld != %lld)\n", \
                __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
            return 1; \
        } \
    } while(0)

static void test_yield(void)
{
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

static int wait_until_due(void)
{
    ns_platform_time_us_t timeout = 0u;
    int i;

    for(i = 0; i < 1000000; ++i){
        int rc = ns_timer_mgr_next_timeout(&timeout);
        if(rc != NS_OK) return rc;
        if(timeout == 0u) return NS_OK;
        test_yield();
    }

    return NS_E_INVAL;
}

static int test_invalid_and_empty_paths(void)
{
    ns_timer_t timer;
    ns_timer_t zero_timer = {0};
    ns_platform_time_us_t timeout = 0u;

    EXPECT_OK(ns_timer_create(NULL, 1000u, NS_TIMER_ATTR_ONESHOT) == NS_E_INVAL);
    EXPECT_OK(ns_timer_create(&timer, 0u, NS_TIMER_ATTR_ONESHOT) == NS_E_INVAL);
    EXPECT_OK(ns_timer_start(&zero_timer) == NS_E_INVAL);
    EXPECT_OK(ns_timer_cancel(&zero_timer) == NS_E_INVAL);
    EXPECT_OK(ns_timer_restart(&zero_timer) == NS_E_INVAL);
    EXPECT_OK(ns_timer_destroy(&zero_timer) == NS_E_INVAL);

    EXPECT_OK(ns_timer_mgr_next_timeout(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_NO_TIMER);
    EXPECT_OK(ns_timer_mgr_fire_expired() == NS_OK);

    return 0;
}

static int test_start_cancel_and_restart_semantics(void)
{
    ns_timer_t timer;
    ns_platform_time_us_t timeout = 0u;

    EXPECT_OK(ns_timer_create(&timer, 50000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_timer_start(&timer) == NS_E_EXISTS);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 50000u);

    EXPECT_OK(ns_timer_restart(&timer) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 50000u);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_NO_TIMER);
    EXPECT_OK(ns_timer_destroy(&timer) == NS_OK);

    return 0;
}

static int g_timer_slot_calls = 0;

static void timer_slot(void *user_data, const void *payload)
{
    int *seen = (int *)user_data;

    (void)payload;
    ++g_timer_slot_calls;
    if(seen != NULL) ++*seen;
}

static int test_oneshot_fire_enqueues_signal(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    ns_platform_time_us_t timeout = 0u;
    int seen = 0;

    g_timer_slot_calls = 0;

    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_timer_create(&timer, 1000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, NULL, &seen, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(wait_until_due() == NS_OK);
    EXPECT_OK(ns_timer_mgr_fire_expired() == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_NO_TIMER);

    EXPECT_OK(ns_loop_quit(loop) == NS_OK);
    EXPECT_OK(ns_loop_run() == NS_OK);
    EXPECT_EQ(seen, 1);
    EXPECT_EQ(g_timer_slot_calls, 1);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_destroy(&timer) == NS_OK);
    EXPECT_OK(ns_loop_destroy() == NS_OK);

    return 0;
}

static int test_repeat_timer_rearms_after_fire(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    ns_platform_time_us_t timeout = 0u;
    int seen = 0;

    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_timer_create(&timer, 1000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, NULL, &seen, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(wait_until_due() == NS_OK);
    EXPECT_OK(ns_timer_mgr_fire_expired() == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 1000u);

    EXPECT_OK(ns_loop_quit(loop) == NS_OK);
    EXPECT_OK(ns_loop_run() == NS_OK);
    EXPECT_EQ(seen, 1);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_destroy(&timer) == NS_OK);
    EXPECT_OK(ns_loop_destroy() == NS_OK);

    return 0;
}

int main(void)
{
    ns_platform_time_us_t timeout = 0u;

    EXPECT_OK(ns_timer_start(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_SHUTDOWN);
    EXPECT_OK(ns_init() == NS_OK);

    if(test_invalid_and_empty_paths() != 0) return 1;
    if(test_start_cancel_and_restart_semantics() != 0) return 1;
    if(test_oneshot_fire_enqueues_signal() != 0) return 1;
    if(test_repeat_timer_rearms_after_fire() != 0) return 1;

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}
