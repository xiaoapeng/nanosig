/**
 * @file test_timer.c
 * @brief Timer manager unit tests.
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig.h>

#include "src/ns_timer_mgr.h"

#include <stdio.h>

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

typedef struct timer_slot_ctx {
    ns_loop_t *loop;
    int seen;
} timer_slot_ctx_t;

static void timer_slot(void *user_data, const void *payload)
{
    timer_slot_ctx_t *ctx = (timer_slot_ctx_t *)user_data;

    (void)payload;
    ++g_timer_slot_calls;
    if(ctx != NULL){
        ++ctx->seen;
        (void)ns_loop_quit(ctx->loop);
    }
}

static int test_oneshot_broker_fires_signal(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    ns_platform_time_us_t timeout = 0u;
    timer_slot_ctx_t ctx;

    g_timer_slot_calls = 0;
    ctx.loop = NULL;
    ctx.seen = 0;

    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);
    ctx.loop = loop;
    EXPECT_OK(ns_timer_create(&timer, 1000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_NO_TIMER);

    EXPECT_EQ(ctx.seen, 1);
    EXPECT_EQ(g_timer_slot_calls, 1);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_destroy(&timer) == NS_OK);
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);

    return 0;
}

static int test_repeat_timer_rearms_after_fire(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    timer_slot_ctx_t ctx;

    ctx.loop = NULL;
    ctx.seen = 0;
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);
    ctx.loop = loop;
    EXPECT_OK(ns_timer_create(&timer, 50000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_EQ(ctx.seen, 1);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_destroy(&timer) == NS_OK);
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: repeat timer with RELOAD_FROM_NOW attr bit                   */
/* ------------------------------------------------------------------ */

static int test_repeat_timer_reload_from_now(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    timer_slot_ctx_t ctx;

    ctx.loop = NULL;
    ctx.seen = 0;

    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    /* REPEAT | RELOAD_FROM_NOW */
    EXPECT_OK(ns_timer_create(&timer, 20000u,
        NS_TIMER_ATTR_REPEAT | NS_TIMER_ATTR_RELOAD_FROM_NOW) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_EQ(ctx.seen, 1);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_destroy(&timer) == NS_OK);
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);

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
    if(test_oneshot_broker_fires_signal() != 0) return 1;
    if(test_repeat_timer_rearms_after_fire() != 0) return 1;
    if(test_repeat_timer_reload_from_now() != 0) return 1;

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}
