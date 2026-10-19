/**
 * @file test_broker_delivery.c
 * @brief watcher handler → 自建 signal → 用户 loop 的跨线程投递契约测试（AC-3）。
 * @date 2026-10-17
 *
 * 覆盖 watcher 回调的情况二范式：broker 线程在 handler 内读净 waitable，再 emit
 * 用户自建的 signal，把就绪事件跨线程投递到用户 loop 的 slot；真正的处理在
 * loop 线程执行。串行化的 watchdog timer 保证投递链路断裂时 loop 仍会退出。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdint.h>

#include <nanosig/nanosig.h>
#include "test_macros.h"

#define DELIVERY_WATCHDOG_US 5000000u

typedef struct delivery_ctx {
    ns_platform_event_t ev;
    ns_watcher_t watcher;
    ns_signal_t wakeup;
    ns_loop_t *loop;
    atomic_int delivered;
} delivery_ctx_t;

static delivery_ctx_t g_ctx;

/* handler 在 broker 线程运行：读净事件源，再 emit 自建 signal 回用户 loop。 */
static void delivery_handler(ns_watcher_t *watcher, uint32_t triggered_events)
{
    delivery_ctx_t *ctx = NS_CONTAINER_OF(watcher, delivery_ctx_t, watcher);

    (void)triggered_events;
    (void)ns_platform_event_drain(&ctx->ev);
    (void)ns_signal_emit(ctx->wakeup, NS_NO_PAYLOAD);
}

/* slot 在用户 loop 线程运行：本轮投递到达后退出 loop。 */
static void delivery_slot(void *user_data, const ns_no_payload_t *payload)
{
    delivery_ctx_t *ctx = (delivery_ctx_t *)user_data;

    (void)payload;
    ns_atomic_fetch_add_explicit(&ctx->delivered, 1, ns_memory_order_acq_rel);
    (void)ns_loop_quit(ctx->loop);
}

/* watchdog：投递链路断裂时也能让 loop 退出，避免挂死。 */
static void delivery_watchdog_slot(void *user_data, const void *payload)
{
    delivery_ctx_t *ctx = (delivery_ctx_t *)user_data;

    (void)payload;
    (void)ns_loop_quit(ctx->loop);
}

static int test_handler_emit_reaches_loop(void)
{
    ns_timer_t watchdog;
    ns_connection_t slot_conn;
    ns_connection_t watchdog_conn;
    int result;

    ns_atomic_init(&g_ctx.delivered, 0);
    g_ctx.loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);

    EXPECT_OK(ns_platform_event_init(&g_ctx.ev, "delivery-ev") == NS_OK);
    EXPECT_OK(ns_signal_init(&g_ctx.wakeup, ns_no_payload_t) == NS_OK);
    EXPECT_OK(ns_loop_create(&g_ctx.loop, NULL) == NS_OK);

    EXPECT_OK(ns_signal_connect_typed(g_ctx.wakeup, delivery_slot, ns_no_payload_t,
                                      g_ctx.loop, &g_ctx, &slot_conn) == NS_OK);

    EXPECT_OK(ns_watcher_init(&g_ctx.watcher, (g_ctx.ev.waitable.primitive),
                              NS_WAITABLE_EVENT_IN, 1, delivery_handler) == NS_OK);
    EXPECT_OK(ns_broker_add(&g_ctx.watcher) == NS_OK);

    EXPECT_OK(ns_timer_init(&watchdog, DELIVERY_WATCHDOG_US, 0) == NS_OK);
    EXPECT_OK(ns_signal_connect(&watchdog.signal, delivery_watchdog_slot,
                                g_ctx.loop, &g_ctx, &watchdog_conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&watchdog) == NS_OK);

    /* 触发一次就绪；handler 在 broker 线程读净后 emit 自建 signal。 */
    EXPECT_OK(ns_platform_event_signal(&g_ctx.ev) == NS_OK);

    /* loop 线程分发该 signal；slot 收到后退出 loop。 */
    (void)ns_loop_run(g_ctx.loop);

    result = (ns_atomic_load_explicit(&g_ctx.delivered, ns_memory_order_acquire) >= 1) ? 0 : 1;

    (void)ns_timer_cancel(&watchdog);
    (void)ns_signal_disconnect(&watchdog_conn);
    (void)ns_timer_deinit(&watchdog);
    (void)ns_broker_remove(&g_ctx.watcher);
    (void)ns_watcher_deinit(&g_ctx.watcher);
    (void)ns_signal_disconnect(&slot_conn);
    (void)ns_signal_deinit(&g_ctx.wakeup);
    (void)ns_loop_destroy(g_ctx.loop);
    g_ctx.loop = NULL;
    (void)ns_platform_event_deinit(&g_ctx.ev);
    (void)ns_shutdown();

    return result;
}

int main(void)
{
    if(test_handler_emit_reaches_loop() != 0) return 1;
    return 0;
}
