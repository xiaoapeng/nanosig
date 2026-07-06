/**
 * @file ns_timer.c
 * @brief nanosig timer manager and public timer API.
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include "nanosig/internal/ns_timer_mgr.h"

#include <nanosig/nanosig.h>

typedef struct ns_timer_mgr {
    ns_platform_mutex_t *mutex;
    ns_rbtree_t tree;
    ns_platform_time_us_t now;
    ns_timer_notify_fn notify;
    void *notify_ctx;
    int initialized;
} ns_timer_mgr_t;

/**
 * @brief 全局 timer 管理器。
 *
 * @note 生命周期规则：
 * - `global_init` 由 broker 在 ns_init 流程中单线程调用，不是线程安全的。
 * - `global_shutdown` 在 ns_shutdown 流程中调用，必须保证此时没有其他线程
 *   在执行 timer API（start/cancel/restart/deinit）。
 * - 调用方必须在 shutdown 前完成所有 timer 的 deinit，否则定时器的信号
 *   互斥锁会泄漏。
 */
static ns_timer_mgr_t g_timer_mgr;

static int ns_timer_attr_is_valid(uint32_t attr)
{
    const uint32_t valid_attr = NS_TIMER_ATTR_REPEAT | NS_TIMER_ATTR_RELOAD_FROM_NOW;

    return (attr & ~valid_attr) == 0u;
}

static int64_t ns_timer_remaining_us(ns_time_us_t expire_us, ns_platform_time_us_t now_us)
{
    return (int64_t)((uint64_t)expire_us - (uint64_t)now_us);
}

static int ns_timer_cmp(ns_rbtree_node_t *a, ns_rbtree_node_t *b)
{
    const ns_timer_t *ta = ns_rbtree_entry(a, const ns_timer_t, rb_node);
    const ns_timer_t *tb = ns_rbtree_entry(b, const ns_timer_t, rb_node);
    int64_t ra = ns_timer_remaining_us(ta->expire_us, g_timer_mgr.now);
    int64_t rb = ns_timer_remaining_us(tb->expire_us, g_timer_mgr.now);

    if(ra < rb) return -1;
    if(ra > rb) return 1;
    return 0;
}

static int ns_timer_runtime_ready(void)
{
    return ns_is_initialized() ? NS_OK : NS_E_SHUTDOWN;
}

static int ns_timer_mgr_lock(void)
{
    if(!g_timer_mgr.initialized || (g_timer_mgr.mutex == NULL)) return NS_E_SHUTDOWN;
    return ns_platform_mutex_lock(g_timer_mgr.mutex);
}

static int ns_timer_mgr_unlock(void)
{
    return ns_platform_mutex_unlock(g_timer_mgr.mutex);
}

static int ns_timer_mgr_refresh_now(void)
{
    return ns_platform_clock_monotonic_us(&g_timer_mgr.now);
}

static void ns_timer_mgr_notify(int should_notify)
{
    ns_timer_notify_fn notify = g_timer_mgr.notify;

    if(should_notify && (notify != NULL)){
        notify(g_timer_mgr.notify_ctx);
    }
}

static int ns_timer_is_running(const ns_timer_t *timer)
{
    return !ns_rbtree_node_is_empty(&timer->rb_node);
}

static int ns_timer_validate_created(const ns_timer_t *timer)
{
    if(timer->signal.mutex == NULL) return NS_E_INVAL;
    if(timer->signal.payload_size != 0u) return NS_E_INVAL;
    return NS_OK;
}

static int ns_timer_validate_user_input(const ns_timer_t *timer)
{
    int rc = ns_timer_validate_created(timer);

    if(rc != NS_OK) return rc;
    if(timer->interval_us == 0u) return NS_E_INVAL;
    if(!ns_timer_attr_is_valid(timer->attr)) return NS_E_INVAL;
    return NS_OK;
}

static int ns_timer_start_locked(ns_timer_t *timer, int *out_should_notify)
{
    ns_rbtree_node_t *add_result;
    int rc;

    *out_should_notify = 0;

    if(ns_timer_is_running(timer)) return NS_E_EXISTS;

    rc = ns_timer_mgr_refresh_now();
    if(rc != NS_OK) return rc;

    timer->expire_us = (ns_time_us_t)(g_timer_mgr.now + timer->interval_us);
    add_result = ns_rbtree_add(&timer->rb_node, &g_timer_mgr.tree);

    *out_should_notify = (add_result == &timer->rb_node);
    return NS_OK;
}

static int ns_timer_cancel_locked(ns_timer_t *timer, int *out_should_notify)
{
    ns_rbtree_node_t *before_first;

    *out_should_notify = 0;

    if(!ns_timer_is_running(timer)) return NS_OK;

    before_first = ns_rbtree_first(&g_timer_mgr.tree);
    *out_should_notify = before_first == &timer->rb_node;
    ns_rbtree_del(&timer->rb_node, &g_timer_mgr.tree);
    return NS_OK;
}

/**
 * @brief 初始化全局 timer 管理器。
 *
 * 由 broker 在 ns_init 流程中调用。本函数**不是线程安全的**——调用方必须保证
 * global_init 和 global_shutdown 之间不存在并发初始化/销毁。
 *
 * @note 调用方生命周期契约：
 * - `ns_timer_mgr_global_shutdown` 必须在所有 timer 业务线程已停止、所有 timer
 *   已 deinit 后才能调用。
 * - 违反此约定会导致正在执行锁内操作的线程在已销毁的互斥锁上执行
 *   `mutex_lock`，造成未定义行为。
 * - 在 `ns_shutdown()` 前应确保所有 `ns_timer_t` 已完成 `ns_timer_deinit`，
 *   否则 timer manager 清空树时不会自动释放定时器的信号资源。
 */
int ns_timer_mgr_global_init(ns_timer_notify_fn notify, void *ctx)
{
    int rc;

    if(g_timer_mgr.initialized) return NS_E_EXISTS;

    g_timer_mgr.mutex = NULL;
    g_timer_mgr.now = 0u;
    g_timer_mgr.notify = notify;
    g_timer_mgr.notify_ctx = ctx;
    ns_rbtree_root_init((&g_timer_mgr.tree), ns_timer_cmp);

    rc = ns_platform_mutex_create(&g_timer_mgr.mutex, "nanosig-timer-mgr");
    if(rc != NS_OK){
        g_timer_mgr.notify = NULL;
        g_timer_mgr.notify_ctx = NULL;
        return rc;
    }

    g_timer_mgr.initialized = 1;
    return NS_OK;
}

void ns_timer_mgr_global_shutdown(void)
{
    ns_rbtree_node_t *node;

    if(!g_timer_mgr.initialized) return;

    /* from this point on, every new caller to ns_timer_mgr_lock() will see
       initialized == 0 and return NS_E_SHUTDOWN immediately */
    g_timer_mgr.initialized = 0;

    /* ② 等待正在执行锁内操作者完成，然后清空树 */
    if(ns_platform_mutex_lock(g_timer_mgr.mutex) == NS_OK){
        while((node = ns_rbtree_first(&g_timer_mgr.tree)) != NULL){
            ns_rbtree_del(node, &g_timer_mgr.tree);
        }
        (void)ns_platform_mutex_unlock(g_timer_mgr.mutex);
    }

    /* ③ 安全销毁 mutex — 所有潜在调用者已被 initialized==0 阻断 */
    (void)ns_platform_mutex_destroy(g_timer_mgr.mutex);
    g_timer_mgr.mutex = NULL;
    g_timer_mgr.now = 0u;
    g_timer_mgr.notify = NULL;
    g_timer_mgr.notify_ctx = NULL;
    ns_rbtree_root_init((&g_timer_mgr.tree), ns_timer_cmp);
}

int ns_timer_mgr_next_timeout(ns_platform_time_us_t *out_timeout_us)
{
    ns_rbtree_node_t *first;
    ns_timer_t *timer;
    int64_t remaining;
    int rc;

    if(out_timeout_us == NULL) return NS_E_INVAL;

    rc = ns_timer_mgr_lock();
    if(rc != NS_OK) return rc;

    rc = ns_timer_mgr_refresh_now();
    if(rc != NS_OK) goto out_unlock;

    first = ns_rbtree_first(&g_timer_mgr.tree);
    if(first == NULL){
        rc = NS_E_NO_TIMER;
        goto out_unlock;
    }

    timer = ns_rbtree_entry(first, ns_timer_t, rb_node);
    remaining = ns_timer_remaining_us(timer->expire_us, g_timer_mgr.now);
    *out_timeout_us = (remaining <= 0) ? 0u : (ns_platform_time_us_t)remaining;
    rc = NS_OK;

out_unlock:
    (void)ns_timer_mgr_unlock();
    return rc;
}

/**
 * @brief 触发所有到期定时器。
 *
 * 遍历红黑树，对 expire_us ≤ now 的定时器调用 ns_signal_emit_raw。
 * 对重复定时器按 reload 策略重新计算并插入。
 *
 * @note 本函数在 g_timer_mgr.mutex 下调用 ns_signal_emit_raw（非阻塞
 *       MPSC 环推送）。由于锁被持有，槽位回调中**不得调用**任何 timer API
 *       （如 ns_timer_start / ns_timer_cancel），否则会死锁。
 *       ns_signal_emit_raw 仅为队列推送不会阻塞，但槽位回调本身应保持简短。
 *
 * @note 首次 emit 失败后后续失败被静默丢弃（first_error 仅记录第一个错误）。
 *       这是有意取舍：批量触发场景下报告第一个错误，避免通过累积错误码
 *       增加 API 复杂度。
 */
int ns_timer_mgr_fire_expired(void)
{
    int first_error = NS_OK;
    int rc;

    rc = ns_timer_mgr_lock();
    if(rc != NS_OK) return rc;

    rc = ns_timer_mgr_refresh_now();
    if(rc != NS_OK) goto out_unlock;

    for(;;){
        ns_rbtree_node_t *first = ns_rbtree_first(&g_timer_mgr.tree);
        ns_timer_t *timer;
        ns_time_us_t previous_expire;
        int64_t remaining;

        if(first == NULL) break;

        timer = ns_rbtree_entry(first, ns_timer_t, rb_node);
        remaining = ns_timer_remaining_us(timer->expire_us, g_timer_mgr.now);
        if(remaining > 0) break;

        previous_expire = timer->expire_us;
        ns_rbtree_del(&timer->rb_node, &g_timer_mgr.tree);

        rc = ns_signal_emit_raw(&timer->signal, NS_NO_PAYLOAD, 0u);
        if((rc != NS_OK) && (first_error == NS_OK)) first_error = rc;

        if((timer->attr & NS_TIMER_ATTR_REPEAT) != 0u){
            if((timer->attr & NS_TIMER_ATTR_RELOAD_FROM_NOW) != 0u){
                timer->expire_us = (ns_time_us_t)(g_timer_mgr.now + timer->interval_us);
            } else {
                ns_time_us_t next_expire = (ns_time_us_t)(previous_expire + timer->interval_us);
                if(ns_timer_remaining_us(next_expire, g_timer_mgr.now) <= 0){
                    timer->expire_us = (ns_time_us_t)(g_timer_mgr.now + timer->interval_us);
                } else {
                    timer->expire_us = next_expire;
                }
            }

            ns_rbtree_add(&timer->rb_node, &g_timer_mgr.tree);
        }
    }

    rc = first_error;

out_unlock:
    (void)ns_timer_mgr_unlock();
    return rc;
}

int ns_timer_init(ns_timer_t *timer, ns_time_us_t interval_us, uint32_t attr)
{
    int rc;

    if(timer == NULL) return NS_E_INVAL;
    if(interval_us == 0u) return NS_E_INVAL;
    if(!ns_timer_attr_is_valid(attr)) return NS_E_INVAL;

    rc = ns_timer_runtime_ready();
    if(rc != NS_OK) return rc;

    rc = ns_signal_init_raw(&timer->signal, 0u, 0u, "ns-timer");
    if(rc != NS_OK) return rc;

    timer->interval_us = interval_us;
    timer->expire_us = 0u;
    timer->attr = attr;
    ns_rbtree_node_init(&timer->rb_node);
    return NS_OK;
}

int ns_timer_start(ns_timer_t *timer)
{
    int should_notify = 0;
    int rc;

    if(timer == NULL) return NS_E_INVAL;

    rc = ns_timer_runtime_ready();
    if(rc != NS_OK) return rc;

    rc = ns_timer_validate_user_input(timer);
    if(rc != NS_OK) return rc;

    rc = ns_timer_mgr_lock();
    if(rc != NS_OK) return rc;

    rc = ns_timer_start_locked(timer, &should_notify);
    (void)ns_timer_mgr_unlock();

    if(rc == NS_OK) ns_timer_mgr_notify(should_notify);
    return rc;
}

int ns_timer_cancel(ns_timer_t *timer)
{
    int should_notify = 0;
    int rc;

    if(timer == NULL) return NS_E_INVAL;

    rc = ns_timer_runtime_ready();
    if(rc != NS_OK) return rc;

    rc = ns_timer_validate_created(timer);
    if(rc != NS_OK) return rc;

    rc = ns_timer_mgr_lock();
    if(rc != NS_OK) return rc;

    rc = ns_timer_cancel_locked(timer, &should_notify);
    (void)ns_timer_mgr_unlock();

    if(rc == NS_OK) ns_timer_mgr_notify(should_notify);
    return rc;
}

int ns_timer_restart(ns_timer_t *timer)
{
    int was_first = 0;
    int became_first = 0;
    int should_notify;
    int rc;

    if(timer == NULL) return NS_E_INVAL;

    rc = ns_timer_runtime_ready();
    if(rc != NS_OK) return rc;

    rc = ns_timer_validate_user_input(timer);
    if(rc != NS_OK) return rc;

    rc = ns_timer_mgr_lock();
    if(rc != NS_OK) return rc;

    was_first = ns_rbtree_first(&g_timer_mgr.tree) == &timer->rb_node;
    if(ns_timer_is_running(timer)){
        ns_rbtree_del(&timer->rb_node, &g_timer_mgr.tree);
    }

    rc = ns_timer_mgr_refresh_now();
    if(rc == NS_OK){
        timer->expire_us = (ns_time_us_t)(g_timer_mgr.now + timer->interval_us);
        ns_rbtree_add(&timer->rb_node, &g_timer_mgr.tree);
        became_first = ns_rbtree_first(&g_timer_mgr.tree) == &timer->rb_node;
    }

    (void)ns_timer_mgr_unlock();

    should_notify = was_first || became_first;
    if(rc == NS_OK) ns_timer_mgr_notify(should_notify);
    return rc;
}

int ns_timer_deinit(ns_timer_t *timer)
{
    int rc;
    int deinit_rc;

    if(timer == NULL) return NS_E_INVAL;

    rc = ns_timer_cancel(timer);
    if(rc != NS_OK) return rc;

    deinit_rc = ns_signal_deinit_raw(&timer->signal);
    ns_rbtree_node_init(&timer->rb_node);
    timer->expire_us = 0u;
    return deinit_rc;
}
