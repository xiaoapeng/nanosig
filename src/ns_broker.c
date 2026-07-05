/**
 * @file ns_broker.c
 * @brief nanosig event broker and watcher runtime.
 *
 * @section proxy 模型
 * `ns_broker_add` / `ns_broker_remove` 通过 proxy 模式委托给 broker loop
 * 线程执行：
 *  1. user 线程入队 op_request + 唤醒 broker wakeup
 *  2. user 线程阻塞等待 op_request.wakeup
 *  3. loop 线程处理 op_queue，逐个 do_op → 设 rc → signal wakeup
 *  4. user 线程唤醒，读取 rc 后销毁 wakeup，返回
 *
 * 内存序由 ns_platform_wakeup_signal / wakeup_wait 的 OS 原语保证
 * （Linux eventfd write(2) / read(2)、Windows SetEvent / WaitForSingleObject），
 * 库内不另加 atomic barrier。
 *
 * @section 生命周期
 * - consume_fn 由 broker loop 线程在 dispatch 阶段无锁调用，**只应做消费动作，
 *   禁止调用任何 nanosig API**（见 `nanosig_broker.h` 文档）。
 * - ns_broker_add / remove 与 ns_shutdown 不能并发调用
 * - watcher 内存在 pending op 时被 deinit 会 UAF（文档强约束）
 * - consume_fn 调用期间 consume_handle 必须保持有效
 *
 * @date 2026-07-04
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_port.h>
#include <nanosig/nanosig.h>
#include "nanosig/internal/ns_broker.h"
#include "nanosig/internal/ns_timer_mgr.h"
#ifdef NANOSIG_TEST
/* Test hook: non-NS_OK value injects waitset_wait failure.
 * Set before ns_init() to verify broker thread survives waitset errors. */
volatile int g_ns_test_waitset_wait_result = NS_OK;
#endif

#define NS_BROKER_COMPLETION_CAPACITY 16u

enum ns_broker_op {
    NS_BROKER_OP_ADD,
    NS_BROKER_OP_REMOVE
};

/**
 * @brief 一次 add/remove 操作的代理请求。
 *
 * user 线程在栈上构造此结构，enqueue 后阻塞等待 wakeup。loop 线程处理后
 * 设置 rc 并 signal wakeup，user 线程读取 rc 后销毁 wakeup。
 *
 * @note 不能跨线程共享，只能由 user 线程和 loop 线程通过 op_queue 交接。
 */
typedef struct ns_broker_op_request {
    ns_list_node_t link;
    enum ns_broker_op op;
    ns_watcher_t *watcher;
    ns_platform_wakeup_t *wakeup;
    int rc;
} ns_broker_op_request_t;

struct ns_event_broker {
    ns_platform_thread_t *thread;
    ns_platform_waitset_t *waitset;
    ns_platform_wakeup_t *wakeup;
    ns_platform_waitable_t wakeup_waitable;
    ns_list_node_t watcher_head;
    ns_platform_mutex_t *watcher_mutex;
    atomic_int quit_requested;
    /* proxy 模型新增字段 */
    ns_platform_mutex_t *op_lock;       /* 串行化 add/remove 入队 */
    ns_list_node_t op_queue_head;        /* 待处理 op 队列 */
    int shutdown_started;                /* ns_broker_global_shutdown 中置位 */
};

static ns_event_broker_t *g_broker = NULL;

static int ns_broker_runtime_ready(void)
{
    return ns_is_initialized() ? NS_OK : NS_E_SHUTDOWN;
}

static int ns_watcher_events_are_valid(uint32_t events)
{
    const uint32_t valid_events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR;

    return (events & ~valid_events) == 0u;
}

static int ns_broker_node_is_initialized(const ns_list_node_t *node)
{
    return (node != NULL) && (node->next != NULL) && (node->prev != NULL);
}

static int ns_broker_node_is_linked(const ns_list_node_t *node)
{
    if(!ns_broker_node_is_initialized(node)) return 0;
    /* ns_list_init 设置 next = prev = node（自环）；非自环说明已挂入链表 */
    return !ns_list_empty((ns_list_node_t *)node);
}

static void ns_watcher_reset_empty(ns_watcher_t *watcher)
{
    watcher->signal.mutex = NULL;
    watcher->signal.payload_size = 0u;
    watcher->signal.slot_capacity = 0u;
    watcher->signal.debug_name = NULL;
    ns_list_init(&watcher->signal.slot_list);
    ns_waitable_init(&watcher->waitable);
    ns_list_init(&watcher->broker_node);
    watcher->consume_fn = NULL;
    watcher->pending_consume_handle = NULL;
}

static int ns_watcher_is_initialized(const ns_watcher_t *watcher)
{
    if(watcher == NULL) return 0;
    if(watcher->signal.mutex == NULL) return 0;
    if(watcher->signal.payload_size != sizeof(ns_watcher_event_t)) return 0;
    if(!ns_broker_node_is_initialized(&watcher->broker_node)) return 0;
    return ns_watcher_events_are_valid(watcher->waitable.events);
}

static int ns_watcher_init_common(ns_watcher_t *watcher, uint32_t events, int edge_triggered)
{
    int rc;

    if(watcher == NULL) return NS_E_INVAL;
    if(!ns_watcher_events_are_valid(events)) return NS_E_INVAL;

    rc = ns_broker_runtime_ready();
    if(rc != NS_OK) return rc;

    /* 阻止二次 init：signal 资源应一次 init → 一次 deinit */
    if(watcher->signal.mutex != NULL) return NS_E_EXISTS;

    rc = ns_signal_init_raw(&watcher->signal, sizeof(ns_watcher_event_t), 0u, "ns-watcher");
    if(rc != NS_OK) return rc;

    watcher->waitable.events = events;
    watcher->waitable.edge_triggered = edge_triggered ? 1 : 0;
    ns_list_init(&watcher->broker_node);
    return NS_OK;
}

int ns_watcher_init(ns_watcher_t *watcher, ns_waitable_handle_t handle,
                    uint32_t events, int edge_triggered,
                    ns_watcher_consume_fn consume_fn)
{
    int rc;

    if(watcher == NULL) return NS_E_INVAL;
    ns_watcher_reset_empty(watcher);

    if(!ns_waitable_handle_is_valid(handle)) return NS_E_INVAL;

    rc = ns_watcher_init_common(watcher, events, edge_triggered);
    if(rc != NS_OK) return rc;

    NS_WAITABLE_SET(&watcher->waitable, handle);
    watcher->consume_fn = consume_fn;
    return NS_OK;
}

int ns_watcher_deinit(ns_watcher_t *watcher)
{
    int rc;

    if(watcher == NULL) return NS_E_INVAL;
    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;
    if(ns_broker_node_is_linked(&watcher->broker_node)) return NS_E_EXISTS;

    rc = ns_signal_deinit_raw(&watcher->signal);
    ns_waitable_init(&watcher->waitable);
    ns_list_init(&watcher->broker_node);
    watcher->pending_consume_handle = NULL;
    return rc;
}

ns_waitable_handle_t ns_watcher_handle(const ns_watcher_t *watcher)
{
    if(watcher == NULL){
        ns_waitable_handle_t invalid;
        (void)memset(&invalid, 0xFF, sizeof(invalid));
        return invalid;
    }

    return NS_WAITABLE_GET(&watcher->waitable);
}

void ns_watcher_set_consume_handle(ns_watcher_t *watcher, void *handle)
{
    if(watcher != NULL) watcher->pending_consume_handle = handle;
}

ns_event_broker_t *ns_broker(void)
{
    return g_broker;
}

static void ns_broker_notify(void *ctx)
{
    ns_event_broker_t *broker = (ns_event_broker_t *)ctx;

    if((broker != NULL) && (broker->wakeup != NULL)){
        (void)ns_platform_wakeup_signal(broker->wakeup);
    }
}

/* ------------------------------------------------------------------ */
/*  proxy 模型内部辅助                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief 把 op_request 放入 op_queue 并唤醒 broker loop。
 *
 * @note caller 必须已经构造好 req（除 link 外），且 req->wakeup 已创建。
 *       调用前 req 不应在 op_queue 中。
 */
static void ns_broker_queue_op(ns_event_broker_t *broker, ns_broker_op_request_t *req)
{
    int rc = ns_platform_mutex_lock(broker->op_lock);
    /* op_lock 创建失败是 init 错误，调用栈必定已崩溃；此处省略错误检查 */
    (void)rc;
    ns_list_push_back(&broker->op_queue_head, &req->link);
    (void)ns_platform_mutex_unlock(broker->op_lock);

    /* 唤醒 loop 线程处理 op_queue */
    (void)ns_platform_wakeup_signal(broker->wakeup);
}

/**
 * @brief 等待 op_request.wakeup，循环处理 spurious wakeup。
 *
 * 内存序由 OS 原语保证，不加额外 atomic barrier。
 */
/**
 * @brief 等待 op 完成，500ms 超时保护。
 *
 * @note 超时后不设 req->rc；调用方应尝试从 op_queue 移除 req，
 *       若移除失败（已出队）再等一次（窗口极窄，broker 即将完成）。
 *
 * @param req op 请求；完成后 req->rc 为 broker 设置的返回值。
 * @return NS_OK        op 已完成，可读 req->rc。
 * @return NS_E_TIMEOUT broker 线程未能在 500ms 内处理此 op。
 */
static int ns_broker_wait_op_completion(ns_broker_op_request_t *req)
{
    ns_platform_wait_result_t result;

    if(ns_platform_wakeup_wait(req->wakeup, 500000, &result) != NS_OK){
        return NS_E_TIMEOUT;
    }
    /* wakeup 信号已消费；从 req 读 rc 安全（happens-before 由 wakeup 提供） */
    return NS_OK;
}

/**
 * @brief 从 op_queue 中移除指定的 req（若仍在队列中）。
 *
 * 用于超时恢复路径：检查 req->link 是否自环（ns_list_remove_init 后的
 * 状态）。自环说明 broker 已出队，user 线程必须等其完成；非自环说明
 * req 仍在 op_queue 中，直接摘除后 user 线程可安全返回。
 *
 * @return NS_OK      成功移除，broker 不会再碰此 req。
 * @return NS_E_EMPTY req 不在 op_queue 中（已被 broker 出队处理）。
 */
static int ns_broker_try_dequeue_op(ns_event_broker_t *broker, ns_broker_op_request_t *req)
{
    int rc;

    rc = ns_platform_mutex_lock(broker->op_lock);
    if(rc != NS_OK) return rc;

    if(ns_list_empty(&req->link)){
        /* link 自环：broker 已出队，user 线程必须等 broker 完成 */
        (void)ns_platform_mutex_unlock(broker->op_lock);
        return NS_E_EMPTY;
    }

    /* req 仍在 op_queue 中，摘除后 user 线程可安全返回 */
    ns_list_remove_init(&req->link);
    (void)ns_platform_mutex_unlock(broker->op_lock);
    return NS_OK;
}

/**
 * @brief 真正执行 add 操作（仅 broker loop 线程或单线程模式调用）。
 */
static int ns_broker_do_add(ns_event_broker_t *broker, ns_watcher_t *watcher)
{
    int rc;
    int unlock_rc;

    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;

    rc = ns_platform_mutex_lock(broker->watcher_mutex);
    if(rc != NS_OK) return rc;

    if(ns_broker_node_is_linked(&watcher->broker_node)){
        unlock_rc = ns_platform_mutex_unlock(broker->watcher_mutex);
        return (unlock_rc == NS_OK) ? NS_E_EXISTS : unlock_rc;
    }

    watcher->waitable.user_data = watcher;
    rc = ns_platform_waitset_add(broker->waitset, &watcher->waitable);
    if(rc == NS_OK){
        ns_list_push_back(&broker->watcher_head, &watcher->broker_node);
    } else {
        watcher->waitable.user_data = NULL;
    }

    unlock_rc = ns_platform_mutex_unlock(broker->watcher_mutex);
    if((rc == NS_OK) && (unlock_rc != NS_OK)) rc = unlock_rc;
    return rc;
}

/**
 * @brief 真正执行 remove 操作（仅 broker loop 线程或单线程模式调用）。
 *
 * NULL 化对应 completion.waitable，dispatch 阶段会跳过此 watcher。
 */
static int ns_broker_do_remove(ns_event_broker_t *broker, ns_watcher_t *watcher,
                               ns_platform_waitset_completion_t *completions, size_t count)
{
    int rc;
    int unlock_rc;
    size_t i;

    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;

    rc = ns_platform_mutex_lock(broker->watcher_mutex);
    if(rc != NS_OK) return rc;

    if(!ns_broker_node_is_linked(&watcher->broker_node)){
        unlock_rc = ns_platform_mutex_unlock(broker->watcher_mutex);
        return (unlock_rc == NS_OK) ? NS_E_INVAL : unlock_rc;
    }

    rc = ns_platform_waitset_remove(broker->waitset, &watcher->waitable);
    if(rc == NS_OK){
        /* NULL 化对应 completion：dispatch 阶段会跳过此 watcher。
           user 线程拿到 NS_OK 后可安全 deinit/free watcher。 */
        for(i = 0u; i < count; ++i){
            if(completions[i].waitable == &watcher->waitable){
                completions[i].waitable = NULL;
            }
        }
        watcher->waitable.user_data = NULL;
        ns_list_remove_init(&watcher->broker_node);
    }

    unlock_rc = ns_platform_mutex_unlock(broker->watcher_mutex);
    if((rc == NS_OK) && (unlock_rc != NS_OK)) rc = unlock_rc;
    return rc;
}

/* ------------------------------------------------------------------ */
/*  event dispatch                                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief 处理一个 watcher 的事件：调 consume_fn（可选）+ emit signal。
 *
 * consume_fn 内的 IO 会重新触发 OS eventfd，由下一轮 waitset_wait 重读；
 * 不需要 pending 状态位。do_remove 已把对应 completion.waitable 置 NULL，
 * 故 watcher 不会在被 remove 后被 dispatch 到。
 */
static void ns_broker_dispatch_watcher(ns_watcher_t *watcher, uint32_t triggered_events)
{
    ns_watcher_event_t event;

    event.triggered_events = triggered_events;
    event.consume_handle = NULL;

    if(watcher->consume_fn != NULL){
        int rc = watcher->consume_fn(watcher);
        if(rc <= 0) return;
        event.consume_handle = watcher->pending_consume_handle;
    }

    (void)ns_signal_emit_raw(&watcher->signal, &event, sizeof(event));
}

/**
 * @brief 处理 waitset 本轮触发的 completion 数组。
 *
 * do_remove 期间会把对应 completion.waitable 置 NULL，所以这里跳过 NULL。
 * 不取 watcher_mutex：只在完成事件触达的 watcher 上 dispatch，且
 * do_remove 已把 waiter 从 waitset 摘掉、user_data 已清空——race 不可达。
 */
static void ns_broker_dispatch_pending_events(ns_event_broker_t *broker,
    ns_platform_waitset_completion_t *completions, size_t count)
{
    size_t i;

    (void)broker;

    for(i = 0u; i < count; ++i){
        const ns_platform_waitable_t *w = completions[i].waitable;
        ns_watcher_t *watcher;

        if(w == NULL) continue;
        if(w == &broker->wakeup_waitable) continue;
        watcher = (ns_watcher_t *)w->user_data;
        if(watcher == NULL) continue;

        ns_broker_dispatch_watcher(watcher, completions[i].triggered_events);
    }
}

/**
 * @brief loop 线程：drain op_queue。处理所有挂起的 add/remove 请求。
 *
 * 单线程串行处理；每个 op 处理完后 signal req.wakeup 唤醒 user 线程。
 */
static void ns_broker_drain_op_queue(ns_event_broker_t *broker,
    ns_platform_waitset_completion_t *completions, size_t count)
{
    for(;;){
        ns_broker_op_request_t *req;
        int op_rc;

        if(ns_platform_mutex_lock(broker->op_lock) != NS_OK) return;
        if(ns_list_empty(&broker->op_queue_head)){
            (void)ns_platform_mutex_unlock(broker->op_lock);
            return;
        }
        /* 取队首 */
        {
            ns_list_node_t *node = ns_list_front(&broker->op_queue_head);
            req = ns_list_entry(node, ns_broker_op_request_t, link);
        }
        ns_list_remove_init(&req->link);
        (void)ns_platform_mutex_unlock(broker->op_lock);

        /* 执行 op */
        switch(req->op){
            case NS_BROKER_OP_ADD:
                op_rc = ns_broker_do_add(broker, req->watcher);
                break;
            case NS_BROKER_OP_REMOVE:
                op_rc = ns_broker_do_remove(broker, req->watcher, completions, count);
                break;
            default:
                op_rc = NS_E_INVAL;
                break;
        }

        req->rc = op_rc;
        /* signal 前写入 rc；user 线程 wakeup_wait 返回后读 rc，
           has-happens-before 由 OS 原语提供 */
        (void)ns_platform_wakeup_signal(req->wakeup);
    }
}

/**
 * @brief shutdown 阶段：drain 剩余 op 并 signal 失败。原因：loop join 后，
 *       仍有 user 线程可能 enqueue 了 op（在 shutdown_started 置位之前）。
 *
 * 必须在 loop join 后调用。
 */
static void ns_broker_drain_op_queue_shutdown(ns_event_broker_t *broker)
{
    for(;;){
        ns_broker_op_request_t *req;

        if(ns_platform_mutex_lock(broker->op_lock) != NS_OK) return;
        if(ns_list_empty(&broker->op_queue_head)){
            (void)ns_platform_mutex_unlock(broker->op_lock);
            return;
        }
        {
            ns_list_node_t *node = ns_list_front(&broker->op_queue_head);
            req = ns_list_entry(node, ns_broker_op_request_t, link);
        }
        ns_list_remove_init(&req->link);
        (void)ns_platform_mutex_unlock(broker->op_lock);

        req->rc = NS_E_SHUTDOWN;
        (void)ns_platform_wakeup_signal(req->wakeup);
    }
}

/* ------------------------------------------------------------------ */
/*  loop 线程主循环                                                  */
/* ------------------------------------------------------------------ */

static void ns_broker_run(void *arg)
{
    ns_event_broker_t *broker = (ns_event_broker_t *)arg;
    ns_platform_waitset_completion_t completions[NS_BROKER_COMPLETION_CAPACITY];

    while(ns_atomic_load_explicit(&broker->quit_requested, ns_memory_order_acquire) == 0){
        ns_platform_time_us_t timeout_us = NS_PLATFORM_WAIT_INFINITE_US;
        size_t count = 0u;
        size_t i;
        int rc;

        rc = ns_timer_mgr_next_timeout(&timeout_us);
        if(rc == NS_E_NO_TIMER){
            timeout_us = NS_PLATFORM_WAIT_INFINITE_US;
        } else if(rc != NS_OK){
            timeout_us = NS_PLATFORM_WAIT_INFINITE_US;
        }

        rc = ns_platform_waitset_wait(
            broker->waitset,
            timeout_us,
            completions,
            NS_BROKER_COMPLETION_CAPACITY,
            &count);
#ifdef NANOSIG_TEST
        if(g_ns_test_waitset_wait_result != NS_OK){
            rc = g_ns_test_waitset_wait_result;
            /* Inject one failure, then reset so subsequent iterations work normally */
            g_ns_test_waitset_wait_result = NS_OK;
        }
#endif
        if(rc == NS_OK){
            for(i = 0u; i < count; ++i){
                if(completions[i].waitable == &broker->wakeup_waitable){
                    ns_platform_wait_result_t wait_result;

                    (void)ns_platform_wakeup_wait(broker->wakeup, 0u, &wait_result);
                }
                /* watcher 触发事件暂存于 completions 数组，由 dispatch 阶段处理 */
            }
        }

        /* 处理顺序：先 op_queue（remove 会 NULL 化对应 completion.waitable），
           再 dispatch completions。这样新 add 的 watcher 在本轮无事件，
           刚 remove 的 watcher 对应 completion 已被跳过。 */
        ns_broker_drain_op_queue(broker, completions, count);
        ns_broker_dispatch_pending_events(broker, completions, count);
        /* Fire expired timers and immediately recheck — consume_fn
           may have registered new short-expiry timers that need
           prompt delivery without waiting for the next waitset_wait */
        (void)ns_timer_mgr_fire_expired();
        for(;;){
            ns_platform_time_us_t recheck;
            int ret = ns_timer_mgr_next_timeout(&recheck);
            if(ret != NS_OK) break;        /* NS_E_NO_TIMER — no active timers */
            if(recheck > 0) break;         /* next timer is in the future */
            (void)ns_timer_mgr_fire_expired();
        }
    }

    /* 退出主循环：合同保证 shutdown 期间不再 add，且所有 watcher 已被用户
       显式 remove（或通过 ns_broker_global_shutdown 统一清理）。
       此处不重复 drain / dispatch——见上文 drain_op_queue 调用点的注释。 */
}

static void ns_broker_remove_all_watchers(ns_event_broker_t *broker);

/* ------------------------------------------------------------------ */
/*  公开 API                                                          */
/* ------------------------------------------------------------------ */

int ns_broker_add(ns_watcher_t *watcher)
{
    ns_event_broker_t *broker;
    ns_broker_op_request_t req;
    ns_platform_wakeup_t *wakeup = NULL;
    int queue_rc;

    if(watcher == NULL) return NS_E_INVAL;
    broker = g_broker;
    if(broker == NULL) return NS_E_SHUTDOWN;

    /* 早返回：shutdown 已开始则不入队 */
    if(broker->shutdown_started) return NS_E_SHUTDOWN;

    /* 创建用于 proxy wakeup */
    {
        int wakeup_rc = ns_platform_wakeup_create(&wakeup, "ns-broker-proxy");
        if(wakeup_rc != NS_OK) return wakeup_rc;
    }

    req.op = NS_BROKER_OP_ADD;
    req.watcher = watcher;
    req.wakeup = wakeup;
    req.rc = NS_OK;

    ns_broker_queue_op(broker, &req);

    if(ns_broker_wait_op_completion(&req) != NS_OK){
        /* 超时：先尝试从 op_queue 移除。若成功，broker 不会碰我们的 req/wakeup。 */
        if(ns_broker_try_dequeue_op(broker, &req) == NS_OK){
            queue_rc = NS_E_TIMEOUT;
            goto out;
        }
        /* req 已被 broker 出队处理中，等最终结果（窗口极窄，不会等太久） */
        if(ns_broker_wait_op_completion(&req) != NS_OK){
            queue_rc = NS_E_TIMEOUT;
            goto out;
        }
    }
    queue_rc = req.rc;
out:
    (void)ns_platform_wakeup_destroy(wakeup);
    return queue_rc;
}

int ns_broker_remove(ns_watcher_t *watcher)
{
    ns_event_broker_t *broker;
    ns_broker_op_request_t req;
    ns_platform_wakeup_t *wakeup = NULL;
    int queue_rc;

    if(watcher == NULL) return NS_E_INVAL;
    broker = g_broker;
    if(broker == NULL) return NS_E_SHUTDOWN;

    if(broker->shutdown_started) return NS_E_SHUTDOWN;

    {
        int wakeup_rc = ns_platform_wakeup_create(&wakeup, "ns-broker-proxy");
        if(wakeup_rc != NS_OK) return wakeup_rc;
    }

    req.op = NS_BROKER_OP_REMOVE;
    req.watcher = watcher;
    req.wakeup = wakeup;
    req.rc = NS_OK;

    ns_broker_queue_op(broker, &req);

    if(ns_broker_wait_op_completion(&req) != NS_OK){
        if(ns_broker_try_dequeue_op(broker, &req) == NS_OK){
            queue_rc = NS_E_TIMEOUT;
            goto out;
        }
        if(ns_broker_wait_op_completion(&req) != NS_OK){
            queue_rc = NS_E_TIMEOUT;
            goto out;
        }
    }
    queue_rc = req.rc;
out:
    (void)ns_platform_wakeup_destroy(wakeup);
    return queue_rc;
}

/* ------------------------------------------------------------------ */
/*  lifecycle                                                        */
/* ------------------------------------------------------------------ */

int ns_broker_global_init(void)
{
    ns_event_broker_t *broker;
    int rc;

    if(g_broker != NULL) return NS_E_EXISTS;

    broker = (ns_event_broker_t *)ns_platform_alloc(sizeof(*broker));
    if(broker == NULL) return NS_E_NOMEM;

    broker->thread = NULL;
    broker->waitset = NULL;
    broker->wakeup = NULL;
    ns_waitable_init(&broker->wakeup_waitable);
    broker->watcher_mutex = NULL;
    ns_list_init(&broker->watcher_head);
    ns_atomic_init(&broker->quit_requested, 0);
    broker->op_lock = NULL;
    ns_list_init(&broker->op_queue_head);
    broker->shutdown_started = 0;

    rc = ns_platform_wakeup_create(&broker->wakeup, "nanosig-broker");
    if(rc != NS_OK) goto out_free;

    rc = ns_platform_waitset_create(&broker->waitset);
    if(rc != NS_OK) goto out_wakeup;

    broker->wakeup_waitable = ns_platform_wakeup_get_waitable(broker->wakeup);
    broker->wakeup_waitable.events = NS_WAITABLE_EVENT_IN;
    broker->wakeup_waitable.user_data = NULL;
    rc = ns_platform_waitset_add(broker->waitset, &broker->wakeup_waitable);
    if(rc != NS_OK) goto out_waitset;

    rc = ns_platform_mutex_create(&broker->watcher_mutex, "nanosig-broker-watchers");
    if(rc != NS_OK) goto out_wakeup_waitable;

    rc = ns_platform_mutex_create(&broker->op_lock, "nanosig-broker-op");
    if(rc != NS_OK) goto out_watcher_mutex;

    rc = ns_timer_mgr_global_init(ns_broker_notify, broker);
    if(rc != NS_OK) goto out_op_lock;

    rc = ns_platform_thread_create(&broker->thread, ns_broker_run, broker, "nanosig-broker");
    if(rc != NS_OK) goto out_timer_mgr;

    g_broker = broker;
    return NS_OK;

out_timer_mgr:
    ns_timer_mgr_global_shutdown();
out_op_lock:
    (void)ns_platform_mutex_destroy(broker->op_lock);
    broker->op_lock = NULL;
out_watcher_mutex:
    (void)ns_platform_mutex_destroy(broker->watcher_mutex);
    broker->watcher_mutex = NULL;
out_wakeup_waitable:
    (void)ns_platform_waitset_remove(broker->waitset, &broker->wakeup_waitable);
out_waitset:
    (void)ns_platform_waitset_destroy(broker->waitset);
    broker->waitset = NULL;
out_wakeup:
    (void)ns_platform_wakeup_destroy(broker->wakeup);
    broker->wakeup = NULL;
out_free:
    ns_platform_free(broker);
    return rc;
}

/* shutdown 必须先于 ns_platform_thread_join，避免 join 完成后有 user 线程
   排队却无人处理。shutdown_started 置位确保新 user 调用直接返回
   NS_E_SHUTDOWN 而非永久阻塞。 */
void ns_broker_global_shutdown(void)
{
    ns_event_broker_t *broker = g_broker;

    if(broker == NULL) return;

    /* ① 阻止后续 add/remove 入队 */
    broker->shutdown_started = 1;
    (void)ns_platform_wakeup_signal(broker->wakeup);

    /* ② 唤醒 loop 线程、join */
    ns_atomic_store_explicit(&broker->quit_requested, 1, ns_memory_order_release);
    (void)ns_platform_wakeup_signal(broker->wakeup);
    if(broker->thread != NULL){
        (void)ns_platform_thread_join(broker->thread);
        broker->thread = NULL;
    }

    /* ③ loop 退出后，drain 剩余 op（loop 线程可能在 drain 期间被 quit 唤醒），
       所有挂起的 user 线程拿到 NS_E_SHUTDOWN 后继续 */
    ns_broker_drain_op_queue_shutdown(broker);

    /* ④ 后续清理——销毁 broker 资源 */
    ns_timer_mgr_global_shutdown();

    ns_broker_remove_all_watchers(broker);

    (void)ns_platform_waitset_remove(broker->waitset, &broker->wakeup_waitable);
    (void)ns_platform_waitset_destroy(broker->waitset);
    (void)ns_platform_wakeup_destroy(broker->wakeup);
    (void)ns_platform_mutex_destroy(broker->watcher_mutex);
    (void)ns_platform_mutex_destroy(broker->op_lock);

    g_broker = NULL;
    ns_platform_free(broker);
}

static void ns_broker_remove_all_watchers(ns_event_broker_t *broker)
{
    if(broker == NULL) return;

    if(ns_platform_mutex_lock(broker->watcher_mutex) == NS_OK){
        while(!ns_list_empty(&broker->watcher_head)){
            ns_list_node_t *node = ns_list_front(&broker->watcher_head);
            ns_watcher_t *watcher = ns_list_entry(node, ns_watcher_t, broker_node);

            (void)ns_platform_waitset_remove(broker->waitset, &watcher->waitable);
            watcher->waitable.user_data = NULL;
            ns_list_remove_init(&watcher->broker_node);
        }
        (void)ns_platform_mutex_unlock(broker->watcher_mutex);
    }
}
