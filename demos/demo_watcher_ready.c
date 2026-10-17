/**
 * @file demo_watcher_ready.c
 * @brief watcher 回调范式一「就绪即处理」验收 demo。
 *
 * 范式一：handler 就是终点。watcher 的 waitable 就绪后，broker 线程直接在
 * handler 内把事件源读净并完成处理，不再把事件投递回任何 loop。
 *
 * 完整链路：main 线程 signal event → broker 线程 handler drain + 计数。
 *
 * waitable 由公开的 `ns_platform_event_t` 提供，因此本 demo 不含任何操作系统
 * 预处理分支；跨线程触发走 `ns_platform_event_signal`。
 */

#include <stdint.h>

#include <nanosig/nanosig.h>

#define READY_TARGET     8u
#define READY_POLL_US    1000u
#define READY_MAX_POLLS  2000000u

typedef struct ready_ctx {
    ns_platform_event_t ev;
    ns_watcher_t watcher;
    ns_platform_wakeup_t sleep_wakeup;
    atomic_int seen;
} ready_ctx_t;

static ready_ctx_t g_ready;

static void ready_handler(ns_watcher_t *watcher, uint32_t triggered_events)
{
    ready_ctx_t *ctx = NS_CONTAINER_OF(watcher, ready_ctx_t, watcher);

    (void)triggered_events;
    /* 就绪即处理：非阻塞读净事件，避免电平触发下每轮空转。 */
    (void)ns_platform_event_drain(&ctx->ev);
    ns_atomic_fetch_add_explicit(&ctx->seen, 1, ns_memory_order_relaxed);
}

/* 借用 wakeup 的超时能力实现可移植睡眠，避免在 demos/ 内写 OS 分支。 */
static void ready_sleep(ns_platform_time_us_t us)
{
    ns_platform_wait_result_t result;

    (void)ns_platform_wakeup_wait(&g_ready.sleep_wakeup, us, &result);
}

int main(void)
{
    unsigned int i;
    int rc;

    rc = ns_init();
    if(rc != NS_OK) return 1;

    ns_atomic_init(&g_ready.seen, 0);

    rc = ns_platform_wakeup_init(&g_ready.sleep_wakeup, "ready-sleep");
    if(rc != NS_OK) goto out_shutdown;

    rc = ns_platform_event_init(&g_ready.ev, "ready-ev");
    if(rc != NS_OK) goto out_sleep_wakeup;

    rc = ns_watcher_init(&g_ready.watcher, g_ready.ev.waitable.primitive,
                         NS_WAITABLE_EVENT_IN, 1, ready_handler);
    if(rc != NS_OK) goto out_event;

    rc = ns_broker_add(&g_ready.watcher);
    if(rc != NS_OK) goto out_watcher;

    /*
     * 主线程逐个触发并等待 broker 线程消费。事件是布尔语义，逐次等待可避免
     * 多次 signal 被合并成一次回调。
     */
    for(i = 0u; i < READY_TARGET; i++){
        unsigned int polls = 0u;

        rc = ns_platform_event_signal(&g_ready.ev);
        if(rc != NS_OK) goto out_broker;

        while(ns_atomic_load_explicit(&g_ready.seen, ns_memory_order_acquire) <= (int)i){
            if(polls++ >= READY_MAX_POLLS){
                rc = NS_E_TIMEOUT;
                goto out_broker;
            }
            ready_sleep(READY_POLL_US);
        }
    }

    /* 上面的轮询循环已保证每次 signal 都被消费；这里做一次显式收口校验，
       不依赖 assert（Release 下会被 NDEBUG 编译掉）。 */
    if(ns_atomic_load_explicit(&g_ready.seen, ns_memory_order_acquire) < (int)READY_TARGET){
        rc = NS_E_TIMEOUT;
    }

out_broker:
    (void)ns_broker_remove(&g_ready.watcher);
out_watcher:
    (void)ns_watcher_deinit(&g_ready.watcher);
out_event:
    (void)ns_platform_event_deinit(&g_ready.ev);
out_sleep_wakeup:
    (void)ns_platform_wakeup_deinit(&g_ready.sleep_wakeup);
out_shutdown:
    (void)ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
