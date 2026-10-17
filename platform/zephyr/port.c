/**
 * @file port.c
 * @brief nanosig Zephyr `k_poll` platform backend.
 * @date 2026-10-11
 *
 * 本后端把 `nanosig/nanosig_port.h` 的平台契约映射到 Zephyr：
 *
 * - 内存：`k_aligned_alloc` / `k_free`（按 `Z_KERNEL_STACK_OBJ_ALIGN` 对齐，
 *   因为堆上的 ns_loop_t / broker 内联了含内核栈的 `ns_platform_thread_t`）；
 *   平台对象（wakeup / mutex / thread / waitset）由调用方拥有存储。
 * - mutex：`k_mutex`（`k_mutex_lock` / `k_mutex_unlock`）。
 * - 线程：`k_thread_create` + `k_thread_join`，栈用 `K_KERNEL_STACK_MEMBER`
 *   内嵌在 `struct ns_platform_thread` 中，不依赖 `CONFIG_DYNAMIC_THREAD`。
 * - wakeup：**每对象独立 `k_sem`**（上限 1，二值语义）；`signal` = `k_sem_give`
 *   （已满视为成功），`wait` = `k_sem_take`。
 * - event：`k_poll_signal`；`waitable` 与 `signal_handle` 指向同一 `k_poll_signal`；
 *   `signal` = `k_poll_signal_raise`，`drain` = `k_poll_signal_reset`。
 * - waitset：`k_poll`；每次 `wait` 由活跃 slot 重建 `struct k_poll_event[]`，
 *   固定使用 `K_POLL_TYPE_SIGNAL` + `K_POLL_MODE_NOTIFY_ONLY`。
 * - 时间：`k_uptime_ticks` 经 `k_ticks_to_us_floor64` 换算为微秒。
 *
 * 语义边界：
 *
 * - **只接受 `NS_WAITABLE_EVENT_IN`**：含 OUT/ERR 的 waitable 在
 *   `ns_platform_waitset_add` 返回 `NS_E_INVAL`。
 * - **只接受 signal 类 waitable**：`primitive` 被解释为
 *   `struct k_poll_signal *`；v1 不支持 `k_sem`/FIFO/pipe/socket 等异构 waitable。
 * - **`k_poll_signal` 是电平触发**：`raise` 后 `signaled` 保持置位，直到
 *   `ns_platform_event_drain` 调用 `k_poll_signal_reset`。`reset` 是唯一的复位点，
 *   且按 Zephyr 建议只应由 poller 线程（即调用 `ns_platform_waitset_wait` 的线程）
 *   执行；`waitset_remove` 不 reset 信号（见 `ns_platform_waitset_remove`）。
 * - **微秒→tick 向上取整**：`K_USEC()` 使用 ceil 转换，禁止向下取整导致忙等；
 *   目标频率由 `CONFIG_SYS_CLOCK_TICKS_PER_SEC` 决定。
 * - `edge_triggered` 忽略（`k_poll` 无原生边沿触发，同 Windows 后端）。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_port.h>

#if !defined(CONFIG_POLL) || !CONFIG_POLL
#error "nanosig Zephyr backend requires CONFIG_POLL=y (k_poll / k_poll_signal)"
#endif
#if !defined(CONFIG_MULTITHREADING) || !CONFIG_MULTITHREADING
#error "nanosig Zephyr backend requires CONFIG_MULTITHREADING=y"
#endif
#if !defined(CONFIG_HEAP_MEM_POOL_SIZE) || (CONFIG_HEAP_MEM_POOL_SIZE == 0)
#error "nanosig Zephyr backend requires CONFIG_HEAP_MEM_POOL_SIZE > 0 (k_aligned_alloc / k_free)"
#endif

/* 可配置常量（NS_ZEPHYR_WAITSET_CAP / NS_ZEPHYR_THREAD_STACK_SIZE /
   NS_ZEPHYR_THREAD_PRIORITY）与平台类型由构建内头
   platform/zephyr/nanosig_platform_port.h 提供（由 NANOSIG_PLATFORM_HEADER
   注入）；平台对象存储归调用方所有。 */

/* Zephyr 后端 ABI 探针符号；宿主后端导出 ns_platform_abi_host，FreeRTOS 导出
   ns_platform_abi_freertos。非 static，供 test/zephyr 的主 TU 引用触发链接检查。 */
const int ns_platform_abi_zephyr = 0;

/* ------------------------------------------------------------------ */
/*  stdout 覆盖点（与桌面/FreeRTOS 后端一致的 weak 钩子）                  */
/* ------------------------------------------------------------------ */

NS_FUNCTION_WEAK void platform_stdout_write(void *stream, const uint8_t *buf, size_t size);

void stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    platform_stdout_write(stream, buf, size);
}

/* 默认走 Zephyr console；应用可强定义 platform_stdout_write 接管输出。 */
NS_FUNCTION_WEAK void platform_stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    size_t i;

    (void)stream;

    for(i = 0; i < size; i++){
        printk("%c", (char)buf[i]);
    }
}

/* ------------------------------------------------------------------ */
/*  内部辅助                                                            */
/* ------------------------------------------------------------------ */

/* 微秒→k_timeout_t：K_FOREVER / K_NO_WAIT / K_USEC（ceil 向上取整）。 */
static k_timeout_t ns_zephyr_us_to_timeout(ns_platform_time_us_t timeout_us)
{
    if(timeout_us == NS_PLATFORM_WAIT_INFINITE_US) return K_FOREVER;
    if(timeout_us == 0u) return K_NO_WAIT;

    /* 避免 ceil 换算溢出；超长超时按无限等待处理。 */
    if(timeout_us > (ns_platform_time_us_t)(INT64_MAX / 1000)) return K_FOREVER;

    return K_USEC(timeout_us);
}

/* ------------------------------------------------------------------ */
/*  生命周期 / 内存 / 时间                                              */
/* ------------------------------------------------------------------ */

int ns_platform_init(void)
{
    return NS_OK;
}

int ns_platform_shutdown(void)
{
    return NS_OK;
}

void *ns_platform_alloc(size_t size)
{
    /* `ns_platform_thread_t` 内嵌内核栈，要求 Z_KERNEL_STACK_OBJ_ALIGN 对齐；
       库把该类型按值内联在堆上的 ns_loop_t / ns_event_broker_t 中，故这里比
       k_malloc 的指针宽度对齐更严格。k_aligned_alloc 接受 size 非 2 的幂倍数，
       且结果仍由 k_free 释放。 */
    return k_aligned_alloc((size_t)Z_KERNEL_STACK_OBJ_ALIGN, size);
}

void ns_platform_free(void *ptr)
{
    if(ptr != NULL) k_free(ptr);
}

int ns_platform_clock_monotonic_us(ns_platform_time_us_t *out_now_us)
{
    if(out_now_us == NULL) return NS_E_INVAL;

    *out_now_us = (ns_platform_time_us_t)k_ticks_to_us_floor64((uint64_t)k_uptime_ticks());
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  wakeup                                                             */
/* ------------------------------------------------------------------ */

int ns_platform_wakeup_init(ns_platform_wakeup_t *wakeup, const char *debug_name)
{
    (void)debug_name;

    if(wakeup == NULL) return NS_E_INVAL;

    if(k_sem_init(wakeup, 0u, 1u) != 0){
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_wakeup_deinit(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;

    /* k_sem 无显式销毁：停止使用即可，存储归调用方。 */
    return NS_OK;
}

int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;

    /* 布尔语义：已 signaled 时 give 无效也视为成功。 */
    k_sem_give(wakeup);
    return NS_OK;
}

int ns_platform_wakeup_wait(
    ns_platform_wakeup_t *wakeup,
    ns_platform_time_us_t timeout_us,
    ns_platform_wait_result_t *out_result)
{
    int rc;

    if((wakeup == NULL) || (out_result == NULL)) return NS_E_INVAL;

    rc = k_sem_take(wakeup, ns_zephyr_us_to_timeout(timeout_us));
    *out_result = (rc == 0) ? NS_PLATFORM_WAIT_SIGNALED : NS_PLATFORM_WAIT_TIMEOUT;
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  mutex                                                              */
/* ------------------------------------------------------------------ */

int ns_platform_mutex_init(ns_platform_mutex_t *mutex, const char *debug_name)
{
    (void)debug_name;

    if(mutex == NULL) return NS_E_INVAL;

    if(k_mutex_init(mutex) != 0){
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_mutex_deinit(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    /* k_mutex 无显式销毁：停止使用即可，存储归调用方。 */
    return NS_OK;
}

int ns_platform_mutex_lock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    if(k_mutex_lock(mutex, K_FOREVER) != 0) return NS_E_INVAL;
    return NS_OK;
}

int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    if(k_mutex_unlock(mutex) != 0) return NS_E_INVAL;
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  thread                                                             */
/* ------------------------------------------------------------------ */

static void ns_zephyr_thread_trampoline(void *p1, void *p2, void *p3)
{
    ns_platform_thread_t *thread = (ns_platform_thread_t *)p1;

    (void)p2;
    (void)p3;

    thread->entry(thread->arg);
}

int ns_platform_thread_init(
    ns_platform_thread_t *thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name)
{
    (void)debug_name;

    if((thread == NULL) || (entry == NULL)) return NS_E_INVAL;

    thread->entry = entry;
    thread->arg = arg;

    (void)k_thread_create(&thread->thread, thread->stack,
                          (size_t)K_KERNEL_STACK_SIZEOF(thread->stack),
                          ns_zephyr_thread_trampoline, thread, NULL, NULL,
                          NS_ZEPHYR_THREAD_PRIORITY, 0u, K_NO_WAIT);

    return NS_OK;
}

int ns_platform_thread_join(ns_platform_thread_t *thread)
{
    if(thread == NULL) return NS_E_INVAL;

    if(k_thread_join(&thread->thread, K_FOREVER) != 0) return NS_E_INVAL;

    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  event                                                              */
/* ------------------------------------------------------------------ */

int ns_platform_event_init(ns_platform_event_t *event, const char *debug_name)
{
    struct k_poll_signal *sig;

    (void)debug_name;

    if(event == NULL) return NS_E_INVAL;

    ns_waitable_init(&event->waitable);
    event->signal_handle = NULL;

    sig = (struct k_poll_signal *)ns_platform_alloc(sizeof(*sig));
    if(sig == NULL) return NS_E_NOMEM;

    k_poll_signal_init(sig);

    event->waitable.primitive = (void *)sig;
    event->signal_handle = (void *)sig;
    return NS_OK;
}

int ns_platform_event_signal(ns_platform_event_t *event)
{
    struct k_poll_signal *sig;

    if((event == NULL) || (event->signal_handle == NULL)) return NS_E_INVAL;

    sig = (struct k_poll_signal *)event->signal_handle;

    /* 布尔语义：-EAGAIN 表示投递时 poller 超时正在到期，但 signaled 仍被置位，
       目标等待面已处于 signaled，按成功返回。 */
    (void)k_poll_signal_raise(sig, 0);
    return NS_OK;
}

int ns_platform_event_drain(ns_platform_event_t *event)
{
    struct k_poll_signal *sig;

    if((event == NULL) || (event->waitable.primitive == NULL)) return NS_E_INVAL;

    sig = (struct k_poll_signal *)event->waitable.primitive;

    /* 幂等复位；这是 k_poll_signal_reset 的唯一调用点。 */
    k_poll_signal_reset(sig);
    return NS_OK;
}

int ns_platform_event_deinit(ns_platform_event_t *event)
{
    if(event == NULL) return NS_E_INVAL;
    if(event->waitable.registered_waitset != NULL) return NS_E_BUSY;

    if(event->waitable.primitive != NULL){
        ns_platform_free(event->waitable.primitive);
    }

    ns_waitable_init(&event->waitable);
    event->signal_handle = NULL;
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  waitset                                                            */
/* ------------------------------------------------------------------ */

int ns_platform_waitset_init(ns_platform_waitset_t *waitset)
{
    if(waitset == NULL) return NS_E_INVAL;

    memset(waitset, 0, sizeof(*waitset));
    return NS_OK;
}

int ns_platform_waitset_deinit(ns_platform_waitset_t *waitset)
{
    if(waitset == NULL) return NS_E_INVAL;
    if(waitset->count != 0u) return NS_E_EXISTS;

    return NS_OK;
}

int ns_platform_waitset_add(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    size_t i;

    if((waitset == NULL) || (waitable == NULL)) return NS_E_INVAL;

    /* Zephyr 后端 IN-only，且只接受 signal 类 waitable。 */
    if((waitable->events & NS_WAITABLE_EVENT_IN) == 0u) return NS_E_INVAL;
    if((waitable->events & (NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR)) != 0u){
        return NS_E_INVAL;
    }

    if(waitable->registered_waitset != NULL) return NS_E_EXISTS;
    if(waitset->count >= (size_t)NS_ZEPHYR_WAITSET_CAP) return NS_E_TOO_MANY_HANDLES;

    if(waitable->primitive == NULL) return NS_E_INVAL;

    for(i = 0; i < (size_t)NS_ZEPHYR_WAITSET_CAP; i++){
        if(waitset->slots[i] == NULL) break;
    }
    if(i == (size_t)NS_ZEPHYR_WAITSET_CAP) return NS_E_TOO_MANY_HANDLES;

    waitset->slots[i] = waitable;
    waitset->count++;
    waitable->registered_waitset = waitset;
    return NS_OK;
}

int ns_platform_waitset_remove(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    size_t i;

    if((waitset == NULL) || (waitable == NULL)) return NS_E_INVAL;
    if(waitable->registered_waitset != waitset) return NS_E_INVAL;

    for(i = 0; i < (size_t)NS_ZEPHYR_WAITSET_CAP; i++){
        if(waitset->slots[i] == waitable) break;
    }
    if(i == (size_t)NS_ZEPHYR_WAITSET_CAP) return NS_E_INVAL;

    /* 只摘除 slot。摘除后该 waitable 不再出现在 k_poll 的 event 数组中，因此
       remove 返回后不再产生 completion（契约成立）。k_poll_signal 的复位只由
       ns_platform_event_drain 在 poller 线程执行，这里不 reset；若信号仍为
       signaled，re-add 后会立即再次触发，直到调用方 drain。 */
    waitset->slots[i] = NULL;
    waitset->count--;
    waitable->registered_waitset = NULL;
    return NS_OK;
}

int ns_platform_waitset_wait(
    ns_platform_waitset_t *waitset,
    ns_platform_time_us_t timeout_us,
    ns_platform_waitset_completion_t *completions,
    size_t max_completions,
    size_t *out_count)
{
    ns_platform_waitable_t *active[NS_ZEPHYR_WAITSET_CAP];
    size_t n = 0;
    size_t i;
    size_t count = 0;
    int rc;

    if((waitset == NULL) || (completions == NULL) || (out_count == NULL)) return NS_E_INVAL;

    *out_count = 0u;
    if(max_completions == 0u) return NS_OK;

    /* 重建 event 数组：k_poll 要求每次调用前 state 为 K_POLL_STATE_NOT_READY。 */
    for(i = 0; i < (size_t)NS_ZEPHYR_WAITSET_CAP; i++){
        ns_platform_waitable_t *wp = waitset->slots[i];
        struct k_poll_signal   *sig;

        if(wp == NULL) continue;

        sig = (struct k_poll_signal *)wp->primitive;
        if(sig == NULL) continue;

        k_poll_event_init(&waitset->events[n], K_POLL_TYPE_SIGNAL,
                          K_POLL_MODE_NOTIFY_ONLY, (void *)sig);
        active[n] = wp;
        n++;
    }

    /* 无可等待事件（空 waitset 或全部句柄为空）：k_poll 不接受 0 个 event，
       按 timeout 睡眠后返回超时。无限等待在该分支封顶，避免永久睡死——
       没有事件源时 `k_poll` 无法被唤醒，真无限等待等价于静默挂死。 */
    if(n == 0u){
        if(timeout_us == 0u) return NS_OK;
        if(timeout_us == NS_PLATFORM_WAIT_INFINITE_US){
            timeout_us = NS_PLATFORM_WAIT_EMPTY_MAX_US;
        }
        (void)k_sleep(ns_zephyr_us_to_timeout(timeout_us));
        return NS_OK;
    }

    rc = k_poll(waitset->events, (int)n, ns_zephyr_us_to_timeout(timeout_us));
    if(rc == -EAGAIN){
        /* 超时：无事件就绪。 */
        return NS_OK;
    }
    if(rc == -EINTR){
        /* 被取消（K_POLL_STATE_CANCELLED）；不重试，直接返回错误码。 */
        return NS_E_INVAL;
    }
    if(rc != 0){
        return NS_E_INVAL;
    }

    for(i = 0; (i < n) && (count < max_completions); i++){
        if((waitset->events[i].state & K_POLL_STATE_SIGNALED) != 0u){
            completions[count].waitable = active[i];
            completions[count].triggered_events = NS_WAITABLE_EVENT_IN;
            count++;
        }
    }

    *out_count = count;
    return NS_OK;
}
