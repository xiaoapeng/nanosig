/**
 * @file demo_watcher_loop.c
 * @brief watcher 回调范式二「读净 + emit 自建 signal 回 loop」验收 demo。
 *
 * 范式二：handler 只是入口。broker 线程在 handler 内读净 waitable，再 emit
 * 用户自建的 signal，把事件跨线程投递回用户 loop 的 slot；真正的业务处理在
 * loop 线程执行。
 *
 * 完整链路：producer 线程 signal event → broker 线程 handler drain + emit
 * 自建 signal → loop 线程 slot 处理 → 达阈值 quit。
 *
 * waitable 由公开的 `ns_platform_event_t` 提供，因此本 demo 不含任何操作系统
 * 预处理分支；跨线程触发走 `ns_platform_event_signal`。
 */

#include <assert.h>
#include <stdint.h>

#include <nanosig/nanosig.h>

#define LOOP_TARGET        20
#define PRODUCER_PERIOD_US 1000u

typedef struct loop_ctx {
    ns_platform_event_t ev;
    ns_watcher_t watcher;
    ns_signal_t wakeup;
    ns_loop_t *loop;
    ns_platform_wakeup_t *sleep_wakeup;
    atomic_int delivered;
    atomic_int stop;
} loop_ctx_t;

static loop_ctx_t g_loop_ctx;
static ns_platform_thread_t *g_producer;

/* 借用 wakeup 的超时能力实现可移植睡眠，避免在 demos/ 内写 OS 分支。 */
static void producer_sleep(ns_platform_time_us_t us)
{
    ns_platform_wait_result_t result;

    (void)ns_platform_wakeup_wait(g_loop_ctx.sleep_wakeup, us, &result);
}

static void wakeup_handler(ns_watcher_t *watcher, uint32_t triggered_events)
{
    loop_ctx_t *ctx = NS_CONTAINER_OF(watcher, loop_ctx_t, watcher);

    (void)triggered_events;
    /* 先读净事件源，再 emit；ns_signal_emit 非阻塞，允许在 broker 线程调用。 */
    (void)ns_platform_event_drain(&ctx->ev);
    (void)ns_signal_emit(ctx->wakeup, NS_NO_PAYLOAD);
}

static void wakeup_slot(void *user_data, const ns_no_payload_t *payload)
{
    loop_ctx_t *ctx = (loop_ctx_t *)user_data;
    int seen;

    (void)payload;
    seen = ns_atomic_fetch_add_explicit(&ctx->delivered, 1, ns_memory_order_acq_rel) + 1;
    if(seen >= LOOP_TARGET){
        ns_atomic_store_explicit(&ctx->stop, 1, ns_memory_order_release);
        (void)ns_loop_quit(ctx->loop);
    }
}

static void producer_entry(void *arg)
{
    loop_ctx_t *ctx = (loop_ctx_t *)arg;

    while(ns_atomic_load_explicit(&ctx->stop, ns_memory_order_acquire) == 0){
        (void)ns_platform_event_signal(&ctx->ev);
        producer_sleep(PRODUCER_PERIOD_US);
    }
}

int main(void)
{
    ns_connection_t wakeup_conn;
    ns_loop_config_t cfg;
    int rc;

    rc = ns_init();
    if(rc != NS_OK) return 1;

    ns_atomic_init(&g_loop_ctx.delivered, 0);
    ns_atomic_init(&g_loop_ctx.stop, 0);

    rc = ns_platform_wakeup_create(&g_loop_ctx.sleep_wakeup, "loop-sleep");
    if(rc != NS_OK) goto out_shutdown;

    rc = ns_signal_init(&g_loop_ctx.wakeup, ns_no_payload_t);
    if(rc != NS_OK) goto out_sleep_wakeup;

    cfg = NS_LOOP_CONFIG_DEFAULT();
    cfg.debug_name = "watcher-demo-loop";
    rc = ns_loop_init(&g_loop_ctx.loop, &cfg);
    if(rc != NS_OK) goto out_wakeup_signal;

    rc = ns_signal_connect_typed(g_loop_ctx.wakeup, wakeup_slot, ns_no_payload_t,
                                 g_loop_ctx.loop, &g_loop_ctx, &wakeup_conn);
    if(rc != NS_OK) goto out_loop;

    rc = ns_platform_event_init(&g_loop_ctx.ev, "loop-ev");
    if(rc != NS_OK) goto out_wakeup_conn;

    rc = ns_watcher_init(&g_loop_ctx.watcher, NS_WAITABLE_GET(&g_loop_ctx.ev.waitable),
                         NS_WAITABLE_EVENT_IN, 1, wakeup_handler);
    if(rc != NS_OK) goto out_event;

    rc = ns_broker_add(&g_loop_ctx.watcher);
    if(rc != NS_OK) goto out_watcher;

    rc = ns_platform_thread_create(&g_producer, producer_entry, &g_loop_ctx, "producer");
    if(rc != NS_OK) goto out_broker;

    /* loop 线程分发 handler emit 的自建 signal，直到 slot 达阈值 quit。 */
    (void)ns_loop_run(g_loop_ctx.loop);
    ns_atomic_store_explicit(&g_loop_ctx.stop, 1, ns_memory_order_release);

    if(ns_atomic_load_explicit(&g_loop_ctx.delivered, ns_memory_order_acquire) < LOOP_TARGET){
        rc = NS_E_TIMEOUT;
    }

    (void)ns_platform_thread_join(g_producer);
out_broker:
    (void)ns_broker_remove(&g_loop_ctx.watcher);
out_watcher:
    (void)ns_watcher_deinit(&g_loop_ctx.watcher);
out_event:
    (void)ns_platform_event_deinit(&g_loop_ctx.ev);
out_wakeup_conn:
    (void)ns_signal_disconnect(&wakeup_conn);
out_loop:
    (void)ns_loop_deinit(g_loop_ctx.loop);
out_wakeup_signal:
    (void)ns_signal_deinit(&g_loop_ctx.wakeup);
out_sleep_wakeup:
    (void)ns_platform_wakeup_destroy(g_loop_ctx.sleep_wakeup);
out_shutdown:
    (void)ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
