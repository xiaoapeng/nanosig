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
 * 内存序由 eventfd/pipe/kqueue 的 signal 与 waitset wait，以及
 * ns_platform_wakeup_signal / wakeup_wait 的 OS 原语保证
 * （Linux eventfd write(2) / read(2)、Windows SetEvent / WaitForSingleObject），
 * 库内不另加 atomic barrier。
 *
 * @section 生命周期
 * - handler 由 broker loop 线程在 dispatch 阶段无锁调用。它必须非阻塞，
 *   且不得调用任何会等待 broker 线程的 nanosig API（ns_broker_add /
 *   ns_broker_remove / ns_shutdown 及 teardown）；ns_signal_emit* 为非阻塞，
 *   允许调用。完整契约见 `nanosig_broker.h`。
 * - ns_broker_add / remove 与 ns_shutdown 不能并发调用
 * - watcher 内存在 pending op 时被 deinit 会 UAF（文档强约束）
 * - handler 调用期间 handler 使用的上下文必须保持有效
 *
 * @date 2026-07-04
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_port.h>
#include <nanosig/nanosig.h>
#include "nanosig/internal/ns_broker.h"
#include "nanosig/internal/ns_timer_mgr.h"

#include <nanosig/ns_debug.h>
/* Default SYS; overridable at build time (e.g. project CMake
 * target_compile_definitions(nanosig PRIVATE NS_DBG_MODULE_LEVEL_BROKER=NS_DBG_DEBUG)). */
#ifndef NS_DBG_MODULE_LEVEL_BROKER
#define NS_DBG_MODULE_LEVEL_BROKER NS_DBG_SYS
#endif

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

/**
 * @note 所有字段仅 broker 线程访问：user 线程通过 op_queue 交接 add/remove，
 *       不直接触碰 waitset 或链路。
 */
struct ns_event_broker {
    ns_platform_thread_t thread;
    ns_platform_waitset_t waitset;
    ns_platform_event_t event;           /* broker loop 自唤醒事件源 */
    atomic_int quit_requested;
    /* proxy 模型字段 */
    ns_platform_mutex_t op_lock;        /* 串行化 add/remove 入队 */
    ns_list_node_t op_queue_head;        /* 待处理 op 队列 */
    atomic_int shutdown_started;         /* ns_broker_global_shutdown 中置位 */
};

static ns_event_broker_t *g_broker = NULL;

static int ns_broker_runtime_ready(void)
{
    return ns_is_initialized() ? NS_OK : NS_E_SHUTDOWN;
}

static int ns_watcher_events_are_valid(uint32_t events)
{
    const uint32_t valid_events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR;

    /* 0 表示"不监听任何事件"：host 三后端会注册成功但永不触发，RTOS 后端则
       报 NS_E_INVAL。统一在此拒绝，避免同一段代码跨平台行为分裂。 */
    if(events == 0u) return 0;

    return (events & ~valid_events) == 0u;
}

/**
 * @brief watcher 是否已注册到 broker 的 waitset。
 *
 * 以平台层维护的 `registered_waitset` 判定，且必须收窄到 broker 自己的 waitset：
 * 该字段语义是"注册在某个 waitset"，调用方也可用 ns_platform_waitset_init 自建。
 * broker 已销毁时返回 0（watcher 无从属于任何 broker）。
 */
static int ns_watcher_is_registered(const ns_watcher_t *watcher)
{
    return (g_broker != NULL) && (watcher->waitable.registered_waitset == &g_broker->waitset);
}

static void ns_watcher_reset_empty(ns_watcher_t *watcher)
{
    ns_waitable_init(&watcher->waitable);
    watcher->handler = NULL;
}

static int ns_watcher_is_initialized(const ns_watcher_t *watcher)
{
    if(watcher == NULL) return 0;
    if(watcher->handler == NULL) return 0;
    return ns_watcher_events_are_valid(watcher->waitable.events);
}

static void ns_watcher_init_common(ns_watcher_t *watcher, uint32_t events,
                                   int edge_triggered, ns_watcher_handler_fn handler)
{
    ns_watcher_reset_empty(watcher);
    watcher->waitable.events = events;
    watcher->waitable.edge_triggered = edge_triggered ? 1 : 0;
    watcher->handler = handler;
}

int ns_watcher_init(ns_watcher_t *watcher, ns_waitable_handle_t handle,
                    uint32_t events, int edge_triggered,
                    ns_watcher_handler_fn handler)
{
    int rc;

    if(watcher == NULL) return NS_E_INVAL;
    if(handler == NULL) return NS_E_INVAL;

    /* 不检测重复初始化：重复 init（未先 deinit）属调用方错误，见
       ns_watcher_init 的 @warning；与 ns_signal_init_raw 保持统一策略。 */
    if(!ns_waitable_handle_is_valid(handle)) return NS_E_INVAL;
    if(!ns_watcher_events_are_valid(events)) return NS_E_INVAL;

    rc = ns_broker_runtime_ready();
    if(rc != NS_OK) return rc;

    ns_watcher_init_common(watcher, events, edge_triggered, handler);
    watcher->waitable.primitive = handle;
    return NS_OK;
}

int ns_watcher_deinit(ns_watcher_t *watcher)
{
    if(watcher == NULL) return NS_E_INVAL;
    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;
    if(ns_watcher_is_registered(watcher)) return NS_E_EXISTS;

    /* 完整重置 waitable / handler：使 ns_watcher_handle 在已 deinit 的
       watcher 上返回无效哨兵而非陈旧句柄（D14）。 */
    ns_watcher_reset_empty(watcher);
    return NS_OK;
}

ns_waitable_handle_t ns_watcher_handle(const ns_watcher_t *watcher)
{
    if(watcher == NULL){
        ns_waitable_handle_t invalid;
        (void)memset(&invalid, 0xFF, sizeof(invalid));
        return invalid;
    }

    return watcher->waitable.primitive;
}

ns_event_broker_t *ns_broker(void)
{
    return g_broker;
}

static void ns_broker_notify(void *ctx)
{
    ns_event_broker_t *broker = (ns_event_broker_t *)ctx;

    if(broker != NULL){
        (void)ns_platform_event_signal(&broker->event);
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
static int ns_broker_queue_op(ns_event_broker_t *broker, ns_broker_op_request_t *req)
{
    int rc;

    rc = ns_platform_mutex_lock(&broker->op_lock);
    if(rc != NS_OK) return rc;
    ns_list_push_back(&broker->op_queue_head, &req->link);

    /* 持锁期间 signal 失败时可原子回滚；否则用户线程将因 broker 不被唤醒而永久等待。
       event 契约下底层缓冲已满（EAGAIN）视为成功，仅真实错误触发回滚。 */
    rc = ns_platform_event_signal(&broker->event);
    if(rc != NS_OK){
        ns_list_remove_init(&req->link);
    }

    (void)ns_platform_mutex_unlock(&broker->op_lock);
    return rc;
}

/**
 * @brief 等待 op 完成；永不超时。
 *
 * 内存序由 OS 原语保证，不加额外 atomic barrier。
 *
 * @note 等待失败时**不得返回**：req 仍挂在 op_queue 中，提前返回会让 broker
 *       随后写已失效的栈帧、并向已关闭的 wakeup signal（use-after-free）。
 *       因此非 SIGNALED 一律重试，只在收到 signal 后返回。
 * @note 退化风险：Linux `ns_linux_wakeup_drain` 失败时已消费 POLLIN 但未消费
 *       eventfd 计数，下一轮 poll 立即返回，此处会热自旋（仅 host 后端）。
 *       该缺陷登记于 `docs/review/platform-ports-code-review.md`，本模块不修。
 *
 * @param req op 请求；返回时 req->rc 为 broker 设置的返回值。
 */
static void ns_broker_wait_op_completion(ns_broker_op_request_t *req)
{
    ns_platform_wait_result_t result = NS_PLATFORM_WAIT_TIMEOUT;
    int warned = 0;

    for(;;){
        int rc = ns_platform_wakeup_wait(req->wakeup, NS_PLATFORM_WAIT_INFINITE_US, &result);

        if((rc == NS_OK) && (result == NS_PLATFORM_WAIT_SIGNALED)) return;

        /* 日志去重：失败极罕见，但热自旋路径下每轮都会走到这里 */
        if(!warned){
            ns_mwarnln(BROKER, "wakeup wait failed (rc=%d); retrying", rc);
            warned = 1;
        }
    }
}

/**
 * @brief 真正执行 add 操作（仅 broker 线程调用）。
 *
 * 重复注册的判定由平台层 `ns_platform_waitset_add` 承担（五个后端均对
 * 已注册 waitable 返回 NS_E_EXISTS）；本函数不再自行维护平行标记。
 */
static int ns_broker_do_add(ns_event_broker_t *broker, ns_watcher_t *watcher)
{
    int rc;

    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;

    watcher->waitable.user_data = watcher;
    rc = ns_platform_waitset_add(&broker->waitset, &watcher->waitable);
    if(rc != NS_OK){
        ns_merrln(BROKER, "waitset_add failed: %d", rc);
        watcher->waitable.user_data = NULL;
    }

    return rc;
}

/**
 * @brief 真正执行 remove 操作（仅 broker 线程调用）。
 *
 * NULL 化对应 completion.waitable，dispatch 阶段会跳过此 watcher。
 * 未注册的判定由平台层 `ns_platform_waitset_remove` 承担（五个后端均对
 * 未注册 waitable 返回 NS_E_INVAL）。
 */
static int ns_broker_do_remove(ns_event_broker_t *broker, ns_watcher_t *watcher,
                               ns_platform_waitset_completion_t *completions, size_t count)
{
    int rc;
    size_t i;

    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;

    rc = ns_platform_waitset_remove(&broker->waitset, &watcher->waitable);
    if(rc != NS_OK){
        ns_merrln(BROKER, "waitset_remove failed: %d", rc);
    }
    if(rc == NS_OK){
        /* NULL 化对应 completion：dispatch 阶段会跳过此 watcher。
           user 线程拿到 NS_OK 后可安全 deinit/free watcher。 */
        for(i = 0u; i < count; ++i){
            if(completions[i].waitable == &watcher->waitable){
                completions[i].waitable = NULL;
            }
        }
        watcher->waitable.user_data = NULL;
    }

    return rc;
}

/* ------------------------------------------------------------------ */
/*  event dispatch                                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief 处理 waitset 本轮触发的 completion 数组。
 *
 * do_remove 期间会把对应 completion.waitable 置 NULL，所以这里跳过 NULL。
 * 本函数只在 broker 线程执行，与 add/remove 天然串行——无需额外同步。
 */
static void ns_broker_dispatch_pending_events(ns_event_broker_t *broker,
    ns_platform_waitset_completion_t *completions, size_t count)
{
    size_t i;

    for(i = 0u; i < count; ++i){
        const ns_platform_waitable_t *w = completions[i].waitable;
        ns_watcher_t *watcher;

        if(w == NULL) continue;
        if(w == &broker->event.waitable) continue;
        watcher = (ns_watcher_t *)w->user_data;
        if(watcher == NULL) continue;

        watcher->handler(watcher, completions[i].triggered_events);
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

        if(ns_platform_mutex_lock(&broker->op_lock) != NS_OK) return;
        if(ns_list_empty(&broker->op_queue_head)){
            (void)ns_platform_mutex_unlock(&broker->op_lock);
            return;
        }
        /* 取队首 */
        {
            ns_list_node_t *node = ns_list_front(&broker->op_queue_head);
            req = ns_list_entry(node, ns_broker_op_request_t, link);
        }
        ns_list_remove_init(&req->link);
        (void)ns_platform_mutex_unlock(&broker->op_lock);

        /* 执行 op */
        switch(req->op){
            case NS_BROKER_OP_ADD:
                op_rc = ns_broker_do_add(broker, req->watcher);
                break;
            case NS_BROKER_OP_REMOVE:
                op_rc = ns_broker_do_remove(broker, req->watcher, completions, count);
                break;
            default:
                ns_merrln(BROKER, "drain_op_queue: unknown op=%d", req->op);
                op_rc = NS_E_INVAL;
                break;
        }

        req->rc = op_rc;
        /* signal 前写入 rc；user 线程 wakeup_wait 返回后读 rc，
           has-happens-before 由 OS 原语提供 */
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

        /* timeout_us 初值即 INFINITE；next_timeout 只在成功时写 out 参数，
           错误路径无需分支 */
        (void)ns_timer_mgr_next_timeout(&timeout_us);

        rc = ns_platform_waitset_wait(
            &broker->waitset,
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
                if(completions[i].waitable == &broker->event.waitable){
                    (void)ns_platform_event_drain(&broker->event);
                }
                /* watcher 触发事件暂存于 completions 数组，由 dispatch 阶段处理 */
            }
        }else{
            ns_merrln(BROKER, "waitset_wait failed: %d", rc);
            count = 0u;
        }

        /* 处理顺序：先 op_queue（remove 会 NULL 化对应 completion.waitable），
           再 dispatch completions。这样新 add 的 watcher 在本轮无事件，
           刚 remove 的 watcher 对应 completion 已被跳过。 */
        ns_broker_drain_op_queue(broker, completions, count);
        ns_broker_dispatch_pending_events(broker, completions, count);
        /* Fire expired timers and immediately recheck — handler
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

    /* 退出主循环：合同保证 shutdown 期间不再 add；所有 watcher 已由调用方
       显式 remove，残留 op 由 ns_broker_global_shutdown 记录警告后丢弃。
       此处不重复 drain / dispatch——见上文 drain_op_queue 调用点的注释。 */
}

/* ------------------------------------------------------------------ */
/*  公开 API                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief add / remove 的公共骨架：入队 op，等待 broker 线程执行并回传结果。
 *
 * 等待无超时（见 ns_broker_wait_op_completion）；契约要求调用方在
 * ns_shutdown() 前完成 teardown，且 handler 内不得回调本函数。
 */
static int ns_broker_submit_op(ns_watcher_t *watcher, enum ns_broker_op op)
{
    ns_event_broker_t *broker;
    ns_broker_op_request_t req;
    ns_platform_wakeup_t wakeup;
    int wakeup_rc;
    int queue_rc;

    if(watcher == NULL) return NS_E_INVAL;
    broker = g_broker;
    if(broker == NULL) return NS_E_SHUTDOWN;

    /* 早返回：shutdown 已开始则不入队 */
    if(ns_atomic_load_explicit(&broker->shutdown_started, ns_memory_order_acquire)) return NS_E_SHUTDOWN;

    wakeup_rc = ns_platform_wakeup_init(&wakeup, "ns-broker-proxy");
    if(wakeup_rc != NS_OK) return wakeup_rc;

    req.op = op;
    req.watcher = watcher;
    req.wakeup = &wakeup;

    queue_rc = ns_broker_queue_op(broker, &req);
    if(queue_rc != NS_OK) goto out;

    (void)ns_broker_wait_op_completion(&req);
    queue_rc = req.rc;
out:
    (void)ns_platform_wakeup_deinit(&wakeup);
    return queue_rc;
}

int ns_broker_add(ns_watcher_t *watcher)
{
    return ns_broker_submit_op(watcher, NS_BROKER_OP_ADD);
}

int ns_broker_remove(ns_watcher_t *watcher)
{
    return ns_broker_submit_op(watcher, NS_BROKER_OP_REMOVE);
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

    ns_atomic_init(&broker->quit_requested, 0);
    ns_atomic_init(&broker->shutdown_started, 0);
    ns_list_init(&broker->op_queue_head);

    rc = ns_platform_waitset_init(&broker->waitset);
    if(rc != NS_OK) goto out_free;

    rc = ns_platform_event_init(&broker->event, "nanosig-broker");
    if(rc != NS_OK) goto out_waitset;

    broker->event.waitable.events = NS_WAITABLE_EVENT_IN;
    broker->event.waitable.user_data = NULL;
    rc = ns_platform_waitset_add(&broker->waitset, &broker->event.waitable);
    if(rc != NS_OK) goto out_event;

    rc = ns_platform_mutex_init(&broker->op_lock, "nanosig-broker-op");
    if(rc != NS_OK) goto out_add;

    rc = ns_timer_mgr_global_init(ns_broker_notify, broker);
    if(rc != NS_OK) goto out_op_lock;

    rc = ns_platform_thread_init(&broker->thread, ns_broker_run, broker, "nanosig-broker");
    if(rc != NS_OK) goto out_timer_mgr;

    g_broker = broker;
    return NS_OK;

out_timer_mgr:
    ns_timer_mgr_global_shutdown();
out_op_lock:
    (void)ns_platform_mutex_deinit(&broker->op_lock);
out_add:
    (void)ns_platform_waitset_remove(&broker->waitset, &broker->event.waitable);
out_event:
    (void)ns_platform_event_deinit(&broker->event);
out_waitset:
    (void)ns_platform_waitset_deinit(&broker->waitset);
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
    int rc;

    if(broker == NULL) return;

    /* ① 阻止后续 add/remove 入队，并唤醒 loop 线程读取 quit_requested */
    ns_atomic_store_explicit(&broker->shutdown_started, 1, ns_memory_order_release);
    ns_atomic_store_explicit(&broker->quit_requested, 1, ns_memory_order_release);
    (void)ns_platform_event_signal(&broker->event);

    /* ② join */
    /* g_broker != NULL 蕴含 init 已完成线程启动，join 无条件执行 */
    (void)ns_platform_thread_join(&broker->thread);

    /* ③ 诊断：队列非空说明调用方违反契约（应在 shutdown 前完成全部 add/remove）。
       本函数不救援这些 op——它们所属的用户线程不会被唤醒。
       刻意不持 op_lock：违规线程可能正持锁卡在 ns_broker_queue_op，加锁会把
       静默违反升级为 shutdown 死锁。此处只比较链表头指针，不解引用节点。 */
    if(!ns_list_empty(&broker->op_queue_head)){
        ns_mwarnln(BROKER, "shutdown: op queue not empty; caller violated teardown contract");
    }

    /* ④ 后续清理——销毁 broker 资源。残留 watcher 由调用方负责注销：
       waitset_deinit 会因 count != 0 返回 NS_E_EXISTS 并记录错误。 */
    ns_timer_mgr_global_shutdown();

    rc = ns_platform_waitset_remove(&broker->waitset, &broker->event.waitable);
    if(rc != NS_OK) ns_merrln(BROKER, "shutdown: waitset_remove failed: %d", rc);
    rc = ns_platform_waitset_deinit(&broker->waitset);
    if(rc != NS_OK) ns_merrln(BROKER, "shutdown: waitset_deinit failed: %d", rc);
    rc = ns_platform_event_deinit(&broker->event);
    if(rc != NS_OK) ns_merrln(BROKER, "shutdown: event_deinit failed: %d", rc);
    (void)ns_platform_mutex_deinit(&broker->op_lock);

    g_broker = NULL;
    ns_platform_free(broker);
}
