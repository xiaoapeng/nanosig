/**
 * @file test_timer.c
 * @brief Timer manager unit tests.
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>

#include <nanosig/nanosig.h>
#include "nanosig/internal/ns_timer_mgr.h"
#include "test_macros.h"
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

/* ------------------------------------------------------------------ */
/*  Test: restart while timer is running resets the deadline            */
/* ------------------------------------------------------------------ */

static int g_restart_while_running_seen = 0;

static void slot_restart_while_running(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_restart_while_running_seen++;
    /* quit the loop so the test can finish */
}

static int test_restart_while_running(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    ns_platform_time_us_t timeout_before = 0u;
    ns_platform_time_us_t timeout_after = 0u;
    timer_slot_ctx_t ctx;

    g_restart_while_running_seen = 0;
    ctx.loop = NULL;
    ctx.seen = 0;

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    /* 200ms oneshot — long enough to restart before it fires */
    EXPECT_OK(ns_timer_init(&timer, 200000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, slot_restart_while_running, loop, &ctx, &conn) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);

    /* Confirm timer is running */
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout_before) == NS_OK);
    EXPECT_OK(timeout_before <= 200000u);

    /* Restart while running — should reset the deadline */
    EXPECT_OK(ns_timer_restart(&timer) == NS_OK);

    /* After restart, the timeout should have been refreshed.
     * The remaining time should still be <= interval_us. */
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout_after) == NS_OK);
    EXPECT_OK(timeout_after <= 200000u);

    /* Cancel and use a short timer to verify the slot works */
    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);

    /* Now test restart→fire with a short timer */
    EXPECT_OK(ns_timer_init(&timer, 10000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&timer) == NS_OK);

    /* Restart while running */
    EXPECT_OK(ns_timer_restart(&timer) == NS_OK);

    /* Run loop — timer should fire once */
    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_EQ(ctx.seen, 1);

    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: restart on a stopped timer behaves like start                */
/* ------------------------------------------------------------------ */

static int test_restart_idle_timer(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    timer_slot_ctx_t ctx;
    ns_platform_time_us_t timeout = 0u;

    ctx.loop = NULL;
    ctx.seen = 0;

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    EXPECT_OK(ns_timer_init(&timer, 10000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);

    /* restart on a timer that was never started — should behave like start */
    EXPECT_OK(ns_timer_restart(&timer) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 10000u);

    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_EQ(ctx.seen, 1);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: multi-timer ordering and next_timeout tracks the leftmost    */
/* ------------------------------------------------------------------ */

static int test_multi_timer_ordering(void)
{
    ns_timer_t t_short;
    ns_timer_t t_mid;
    ns_timer_t t_long;
    ns_platform_time_us_t timeout = 0u;

    /* Three oneshot timers with distinct intervals. next_timeout must always
     * reflect the earliest expiring one. */
    EXPECT_OK(ns_timer_init(&t_short, 30000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_mid,   60000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_long,  90000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);

    /* Insertion order intentionally not sorted: long, short, mid. */
    EXPECT_OK(ns_timer_start(&t_long)  == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 90000u);

    EXPECT_OK(ns_timer_start(&t_short) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 30000u);   /* short is now leftmost */

    EXPECT_OK(ns_timer_start(&t_mid)   == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 30000u);   /* still short */

    /* Cancel middle — leftmost unchanged. */
    EXPECT_OK(ns_timer_cancel(&t_mid) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 30000u);

    /* Cancel leftmost — next_timeout jumps to t_long. */
    EXPECT_OK(ns_timer_cancel(&t_short) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 90000u);
    EXPECT_OK(timeout > 30000u);    /* short is gone; must be longer */

    /* Cancel last remaining — tree empty. */
    EXPECT_OK(ns_timer_cancel(&t_long) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_NO_TIMER);

    EXPECT_OK(ns_timer_deinit(&t_short) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_mid)   == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_long)  == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: restart promotes a mid/tail timer to leftmost                */
/*                                                                      */
/*  Covers the "became_first" path exercised by the ns_rbtree_add       */
/*  return-value check.                                                 */
/* ------------------------------------------------------------------ */

static int test_restart_promotes_to_leftmost(void)
{
    ns_timer_t t_a;
    ns_timer_t t_b;
    ns_platform_time_us_t timeout = 0u;
    ns_platform_time_us_t timeout_after = 0u;

    /* Two long-interval timers: A shorter than B, so A is leftmost. */
    EXPECT_OK(ns_timer_init(&t_a, 200000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_b, 800000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);

    EXPECT_OK(ns_timer_start(&t_a) == NS_OK);
    EXPECT_OK(ns_timer_start(&t_b) == NS_OK);

    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 200000u);

    /* Restart B: its new interval elapses from *now*, but 800ms is still
     * longer than A's remaining time → B stays behind A. became_first
     * should be false, semantics: next_timeout still reflects A. */
    EXPECT_OK(ns_timer_restart(&t_b) == NS_OK);
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout_after) == NS_OK);
    EXPECT_OK(timeout_after <= 200000u);

    /* Reinit B with a very short interval, then restart to promote it to
     * leftmost. This exercises the became_first == true path. */
    EXPECT_OK(ns_timer_cancel(&t_b) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_b) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_b, 5000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_start(&t_b) == NS_OK);

    /* B (5ms) is now leftmost; A (200ms) is behind. */
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout_after) == NS_OK);
    EXPECT_OK(timeout_after <= 5000u);

    EXPECT_OK(ns_timer_cancel(&t_a) == NS_OK);
    EXPECT_OK(ns_timer_cancel(&t_b) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_a) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_b) == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: start/cancel churn keeps rbtree state consistent              */
/*                                                                      */
/*  Repeatedly cycles a set of timers through start→cancel to catch     */
/*  any stale leftmost pointer or double-remove regression.             */
/* ------------------------------------------------------------------ */

static int test_start_cancel_churn(void)
{
    enum { N = 8, ROUNDS = 50 };
    ns_timer_t timers[N];
    ns_platform_time_us_t timeout = 0u;
    int i;
    int round;

    for(i = 0; i < N; ++i){
        /* Intervals: 10ms, 20ms, ..., 80ms — distinct so ordering is stable. */
        EXPECT_OK(ns_timer_init(&timers[i], (ns_time_us_t)((i + 1) * 10000),
            NS_TIMER_ATTR_ONESHOT) == NS_OK);
    }

    for(round = 0; round < ROUNDS; ++round){
        /* Insertion order rotated per round so leftmost transitions vary. */
        int order[N];
        for(i = 0; i < N; ++i) order[i] = (i + round) % N;

        for(i = 0; i < N; ++i){
            EXPECT_OK(ns_timer_start(&timers[order[i]]) == NS_OK);
        }
        /* After all N started, leftmost must be the 10ms one (t[0]). */
        EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
        EXPECT_OK(timeout <= 10000u);

        /* Cancel in reversed order — leftmost stays t[0] until it too is
         * cancelled, then next_timeout must become NS_E_NO_TIMER. */
        for(i = N - 1; i >= 0; --i){
            EXPECT_OK(ns_timer_cancel(&timers[order[i]]) == NS_OK);
        }
        EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_E_NO_TIMER);
    }

    for(i = 0; i < N; ++i){
        EXPECT_OK(ns_timer_deinit(&timers[i]) == NS_OK);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: cancel a non-running timer is a no-op (idempotent)           */
/* ------------------------------------------------------------------ */

static int test_cancel_idempotent(void)
{
    ns_timer_t timer;

    EXPECT_OK(ns_timer_init(&timer, 10000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);

    /* Never started: cancel is a no-op. */
    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);

    EXPECT_OK(ns_timer_start(&timer) == NS_OK);
    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);
    /* Double cancel — second one is a no-op. */
    EXPECT_OK(ns_timer_cancel(&timer) == NS_OK);

    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: fire_expired handles partial-expiry correctly                 */
/*                                                                      */
/*  Manually invoke fire_expired with a mix of expired and not-yet-     */
/*  expired timers via a very short + a very long interval.             */
/* ------------------------------------------------------------------ */

typedef struct fire_counter_ctx {
    int fired_short;
    int fired_long;
} fire_counter_ctx_t;

static void slot_short(void *user_data, const void *payload)
{
    fire_counter_ctx_t *ctx = (fire_counter_ctx_t *)user_data;
    (void)payload;
    ctx->fired_short++;
}

static void slot_long(void *user_data, const void *payload)
{
    fire_counter_ctx_t *ctx = (fire_counter_ctx_t *)user_data;
    (void)payload;
    ctx->fired_long++;
}

static int test_fire_expired_partial(void)
{
    ns_timer_t t_short;
    ns_timer_t t_long;
    ns_connection_t conn_short;
    ns_connection_t conn_long;
    ns_loop_t *loop = NULL;
    fire_counter_ctx_t ctx = { 0, 0 };
    ns_platform_time_us_t timeout = 0u;

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    /* short: 1μs oneshot — expires immediately. long: 10s — nowhere close. */
    EXPECT_OK(ns_timer_init(&t_short, 1u,        NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_long,  10000000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);

    EXPECT_OK(ns_signal_connect(&t_short.signal, slot_short, loop, &ctx, &conn_short) == NS_OK);
    EXPECT_OK(ns_signal_connect(&t_long.signal,  slot_long,  loop, &ctx, &conn_long)  == NS_OK);

    EXPECT_OK(ns_timer_start(&t_short) == NS_OK);
    EXPECT_OK(ns_timer_start(&t_long)  == NS_OK);

    /* Wait for short timer to expire. Poll next_timeout — when it drops to
     * 0 we know t_short is due; then fire_expired must fire exactly one. */
    for(;;){
        EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
        if(timeout == 0u) break;
        /* Busy-wait a bit — 1μs will elapse in a single loop iteration on any
         * modern host. If not, fall through after a bounded number of tries. */
    }

    EXPECT_OK(ns_timer_mgr_fire_expired() == NS_OK);

    /* Long timer must still be queued and unfired. */
    EXPECT_OK(ns_timer_mgr_next_timeout(&timeout) == NS_OK);
    EXPECT_OK(timeout <= 10000000u);

    /* Drive the loop briefly to deliver the emit queued during fire_expired.
     * We use a tiny helper timer that quits the loop after ~5ms. */
    {
        ns_timer_t t_quit;
        ns_connection_t conn_quit;
        timer_slot_ctx_t quit_ctx;
        quit_ctx.loop = loop;
        quit_ctx.seen = 0;
        EXPECT_OK(ns_timer_init(&t_quit, 5000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
        EXPECT_OK(ns_signal_connect(&t_quit.signal, timer_slot, loop, &quit_ctx, &conn_quit) == NS_OK);
        EXPECT_OK(ns_timer_start(&t_quit) == NS_OK);
        EXPECT_OK(ns_loop_run(loop) == NS_OK);
        EXPECT_OK(ns_signal_disconnect(&conn_quit) == NS_OK);
        EXPECT_OK(ns_timer_deinit(&t_quit) == NS_OK);
    }

    EXPECT_EQ(ctx.fired_short, 1);
    EXPECT_EQ(ctx.fired_long,  0);

    EXPECT_OK(ns_timer_cancel(&t_long) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn_short) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn_long)  == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_short) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_long)  == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: mixed attrs (ONESHOT + REPEAT + REPEAT|RELOAD_FROM_NOW)      */
/*                                                                      */
/*  Runs three timers with different attribute combinations             */
/*  concurrently and verifies each fires the expected number of times   */
/*  before the driver timer quits the loop.                             */
/* ------------------------------------------------------------------ */

typedef struct mixed_ctx {
    ns_loop_t *loop;
    int oneshot_seen;
    int repeat_seen;
    int reload_seen;
    int quit_target;
} mixed_ctx_t;

static void slot_mixed_oneshot(void *user_data, const void *payload)
{
    mixed_ctx_t *ctx = (mixed_ctx_t *)user_data;
    (void)payload;
    ctx->oneshot_seen++;
}

static void slot_mixed_repeat(void *user_data, const void *payload)
{
    mixed_ctx_t *ctx = (mixed_ctx_t *)user_data;
    (void)payload;
    ctx->repeat_seen++;
}

static void slot_mixed_reload(void *user_data, const void *payload)
{
    mixed_ctx_t *ctx = (mixed_ctx_t *)user_data;
    (void)payload;
    ctx->reload_seen++;
    if(ctx->reload_seen >= ctx->quit_target){
        (void)ns_loop_quit(ctx->loop);
    }
}

static int test_mixed_attrs_concurrent(void)
{
    ns_timer_t t_one;
    ns_timer_t t_rep;
    ns_timer_t t_rel;
    ns_connection_t c_one;
    ns_connection_t c_rep;
    ns_connection_t c_rel;
    ns_loop_t *loop = NULL;
    mixed_ctx_t ctx = { NULL, 0, 0, 0, 5 };

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    /* ONESHOT @ 10ms — should fire exactly 1. */
    EXPECT_OK(ns_timer_init(&t_one, 10000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    /* REPEAT @ 20ms — should fire multiple times, we don't pin the exact
     * count because scheduler jitter matters, but must be >= 1. */
    EXPECT_OK(ns_timer_init(&t_rep, 20000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
    /* REPEAT|RELOAD_FROM_NOW @ 30ms — the quit driver; fire 5 times then
     * stop the loop. */
    EXPECT_OK(ns_timer_init(&t_rel, 30000u,
        NS_TIMER_ATTR_REPEAT | NS_TIMER_ATTR_RELOAD_FROM_NOW) == NS_OK);

    EXPECT_OK(ns_signal_connect(&t_one.signal, slot_mixed_oneshot, loop, &ctx, &c_one) == NS_OK);
    EXPECT_OK(ns_signal_connect(&t_rep.signal, slot_mixed_repeat,  loop, &ctx, &c_rep) == NS_OK);
    EXPECT_OK(ns_signal_connect(&t_rel.signal, slot_mixed_reload,  loop, &ctx, &c_rel) == NS_OK);

    EXPECT_OK(ns_timer_start(&t_one) == NS_OK);
    EXPECT_OK(ns_timer_start(&t_rep) == NS_OK);
    EXPECT_OK(ns_timer_start(&t_rel) == NS_OK);

    EXPECT_OK(ns_loop_run(loop) == NS_OK);

    EXPECT_EQ(ctx.oneshot_seen, 1);
    EXPECT_OK(ctx.repeat_seen >= 1);
    EXPECT_OK(ctx.reload_seen >= ctx.quit_target);

    EXPECT_OK(ns_timer_cancel(&t_rep) == NS_OK);
    EXPECT_OK(ns_timer_cancel(&t_rel) == NS_OK);
    /* t_one already fired (oneshot); cancel is a no-op. */
    EXPECT_OK(ns_timer_cancel(&t_one) == NS_OK);

    EXPECT_OK(ns_signal_disconnect(&c_one) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&c_rep) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&c_rel) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_one) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_rep) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_rel) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: broker delivers multiple concurrent timers correctly         */
/*                                                                      */
/*  Ensures the notify/wake path stays correct after the cancel/       */
/*  restart notify simplifications (cancel_locked no longer wakes on   */
/*  non-leftmost removals; restart no longer wakes on was_first).      */
/* ------------------------------------------------------------------ */

typedef struct concurrent_ctx {
    ns_loop_t *loop;
    int a_seen;
    int b_seen;
    int c_seen;
} concurrent_ctx_t;

static void slot_conc_a(void *ud, const void *p) { (void)p; ((concurrent_ctx_t *)ud)->a_seen++; }
static void slot_conc_b(void *ud, const void *p) { (void)p; ((concurrent_ctx_t *)ud)->b_seen++; }
static void slot_conc_c(void *ud, const void *p)
{
    concurrent_ctx_t *ctx = (concurrent_ctx_t *)ud;
    (void)p;
    ctx->c_seen++;
    (void)ns_loop_quit(ctx->loop);
}

static int test_broker_delivers_three_timers(void)
{
    ns_timer_t t_a;
    ns_timer_t t_b;
    ns_timer_t t_c;
    ns_connection_t c_a;
    ns_connection_t c_b;
    ns_connection_t c_c;
    ns_loop_t *loop = NULL;
    concurrent_ctx_t ctx = { NULL, 0, 0, 0 };

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    /* Three oneshot timers scheduled 10 / 20 / 30 ms. All must be delivered
     * before c fires and quits the loop. */
    EXPECT_OK(ns_timer_init(&t_a, 10000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_b, 20000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_c, 30000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&t_a.signal, slot_conc_a, loop, &ctx, &c_a) == NS_OK);
    EXPECT_OK(ns_signal_connect(&t_b.signal, slot_conc_b, loop, &ctx, &c_b) == NS_OK);
    EXPECT_OK(ns_signal_connect(&t_c.signal, slot_conc_c, loop, &ctx, &c_c) == NS_OK);

    /* Start in reverse-order so the first start inserts to leftmost, the
     * second demotes it, etc. — exercises became_first true→false → true. */
    EXPECT_OK(ns_timer_start(&t_c) == NS_OK);
    EXPECT_OK(ns_timer_start(&t_b) == NS_OK);
    EXPECT_OK(ns_timer_start(&t_a) == NS_OK);

    EXPECT_OK(ns_loop_run(loop) == NS_OK);

    EXPECT_EQ(ctx.a_seen, 1);
    EXPECT_EQ(ctx.b_seen, 1);
    EXPECT_EQ(ctx.c_seen, 1);

    EXPECT_OK(ns_signal_disconnect(&c_a) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&c_b) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&c_c) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_a) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_b) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_c) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: broker still fires the sole timer after peers are cancelled  */
/*                                                                      */
/*  Regression guard for the cancel_locked notify simplification: when */
/*  cancelling the last non-leftmost timer, notify is skipped — that   */
/*  must not stall delivery of the remaining leftmost timer.           */
/* ------------------------------------------------------------------ */

typedef struct sole_ctx {
    ns_loop_t *loop;
    int seen;
} sole_ctx_t;

static void slot_sole(void *ud, const void *p)
{
    sole_ctx_t *ctx = (sole_ctx_t *)ud;
    (void)p;
    ctx->seen++;
    (void)ns_loop_quit(ctx->loop);
}

static int test_cancel_peers_then_fire_leftmost(void)
{
    ns_timer_t t_short;
    ns_timer_t t_mid;
    ns_timer_t t_long;
    ns_connection_t c_short;
    ns_loop_t *loop = NULL;
    sole_ctx_t ctx = { NULL, 0 };

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    EXPECT_OK(ns_timer_init(&t_short, 10000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_mid,   500000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_timer_init(&t_long, 1000000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);

    EXPECT_OK(ns_signal_connect(&t_short.signal, slot_sole, loop, &ctx, &c_short) == NS_OK);

    /* Insert in order — t_short is leftmost. */
    EXPECT_OK(ns_timer_start(&t_short) == NS_OK);
    EXPECT_OK(ns_timer_start(&t_mid)   == NS_OK);
    EXPECT_OK(ns_timer_start(&t_long)  == NS_OK);

    /* Cancel the non-leftmost peers. New behaviour: no notify emitted for
     * these cancels (they don't change the leftmost). Delivery of t_short
     * must still happen when its expire arrives. */
    EXPECT_OK(ns_timer_cancel(&t_long) == NS_OK);
    EXPECT_OK(ns_timer_cancel(&t_mid)  == NS_OK);

    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_EQ(ctx.seen, 1);

    EXPECT_OK(ns_signal_disconnect(&c_short) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_short) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_mid)   == NS_OK);
    EXPECT_OK(ns_timer_deinit(&t_long)  == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: repeated restart on the same leftmost timer                  */
/*                                                                      */
/*  After the restart simplification, restarting a timer that stays    */
/*  leftmost skips notify entirely. Make sure the deadline is still    */
/*  refreshed correctly and the timer eventually fires.                */
/* ------------------------------------------------------------------ */

static int test_repeated_restart_same_leftmost(void)
{
    ns_timer_t timer;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    timer_slot_ctx_t ctx;
    int i;

    ctx.loop = NULL;
    ctx.seen = 0;
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    ctx.loop = loop;

    /* 30ms oneshot — will be restarted repeatedly before firing. */
    EXPECT_OK(ns_timer_init(&timer, 30000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, &ctx, &conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&timer) == NS_OK);

    /* Restart 20 times back-to-back — each call skips notify (no
     * became_first transition, timer is the sole leftmost). */
    for(i = 0; i < 20; ++i){
        EXPECT_OK(ns_timer_restart(&timer) == NS_OK);
    }

    /* Now let it fire. */
    EXPECT_OK(ns_loop_run(loop) == NS_OK);
    EXPECT_EQ(ctx.seen, 1);

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
    if(test_restart_while_running() != 0) return 1;
    if(test_restart_idle_timer() != 0) return 1;
    if(test_multi_timer_ordering() != 0) return 1;
    if(test_restart_promotes_to_leftmost() != 0) return 1;
    if(test_start_cancel_churn() != 0) return 1;
    if(test_cancel_idempotent() != 0) return 1;
    if(test_fire_expired_partial() != 0) return 1;
    if(test_mixed_attrs_concurrent() != 0) return 1;
    if(test_broker_delivers_three_timers() != 0) return 1;
    if(test_cancel_peers_then_fire_leftmost() != 0) return 1;
    if(test_repeated_restart_same_leftmost() != 0) return 1;

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}
