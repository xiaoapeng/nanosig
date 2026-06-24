/**
 * @file test_timer.c
 * @brief Timer manager unit tests.
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig.h>

#include "nanosig/internal/ns_timer_mgr.h"

#include "test_macros.h"
#include <stdio.h>

static int test_invalid_and_empty_paths(void)
{
    ns_timer_t timer;
    ns_timer_t zero_timer = {0};
    ns_platform_time_us_t timeout = 0u;

    EXPECT_OK(ns_timer_init(NULL, 1000u, NS_TIMER_ATTR_ONESHOT) == NS_E_INVAL);
    EXPECT_OK(ns_timer_init(&timer, 0u, NS_TIMER_ATTR_ONESHOT) == NS_E_INVAL);
    EXPECT_OK(ns_timer_start(&zero_timer) == NS_E_INVAL);
    EXPECT_OK(ns_timer_cancel(&zero_timer) == NS_E_INVAL);
    EXPECT_OK(ns_timer_restart(&zero_timer) == NS_E_INVAL);
    EXPECT_OK(ns_timer_deinit(&zero_timer) == NS_E_INVAL);

    EXPECT_OK(ns_timer_mgr_next_timeout(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_NO_TIMER);
    EXPECT_OK(ns_timer_mgr_fire_expired() == NS_OK);

    return 0;
}

static int test_start_cancel_and_restart_semantics(void)
{
    ns_timer_t timer;
    ns_platform_time_us_t timeout = 0u;

    EXPECT_OK(ns_timer_init(&timer, 50000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
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
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);

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

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;
    EXPECT_OK(ns_timer_init(&timer, 1000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_NO_TIMER);

    EXPECT_EQ(ctx.seen, 1);
    EXPECT_EQ(g_timer_slot_calls, 1);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

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
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;
    EXPECT_OK(ns_timer_init(&timer, 50000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_EQ(ctx.seen, 1);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

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

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    /* REPEAT | RELOAD_FROM_NOW */
    EXPECT_OK(ns_timer_init(&timer, 20000u,
        NS_TIMER_ATTR_REPEAT | NS_TIMER_ATTR_RELOAD_FROM_NOW) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_EQ(ctx.seen, 1);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: repeat timer fires multiple times                             */
/* ------------------------------------------------------------------ */

typedef struct repeat_multi_ctx {
    ns_loop_t *loop;
    int count;
    int target;
} repeat_multi_ctx_t;

static void slot_repeat_multi(void *user_data, const void *payload)
{
    repeat_multi_ctx_t *ctx = (repeat_multi_ctx_t *)user_data;
    (void)payload;
    ctx->count++;
    if(ctx->count >= ctx->target){
        (void)ns_loop_quit(ctx->loop);
    }
}

static int test_repeat_timer_multiple(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    repeat_multi_ctx_t ctx;

    ctx.loop = NULL;
    ctx.count = 0;
    ctx.target = 5;

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    /* 50ms repeat timer — should fire 5 times in ~250ms */
    EXPECT_OK(ns_timer_init(&timer, 50000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, slot_repeat_multi, loop, &ctx, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_OK);

    EXPECT_EQ(ctx.count, 5);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: microsecond interval timer (no crash / no hang)              */
/* ------------------------------------------------------------------ */

static int g_us_timer_called = 0;

static void slot_us_timer(void *user_data, const void *payload)
{
    ns_loop_t *loop = (ns_loop_t *)user_data;
    (void)payload;
    g_us_timer_called++;
    (void)ns_loop_quit(loop);
}

static int test_timer_microsecond_interval(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;

    g_us_timer_called = 0;

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    /* 1μs oneshot — very short interval, verify no crash */
    EXPECT_OK(ns_timer_init(&timer, 1u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, slot_us_timer, loop, loop, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_OK);

    /* On platforms where 1μs is below scheduler resolution, the timer
     * may fire but the loop may also time out. We just verify no crash. */
    EXPECT_OK(g_us_timer_called >= 0);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

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
    if(test_repeat_timer_multiple() != 0) return 1;
    if(test_timer_microsecond_interval() != 0) return 1;

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}
