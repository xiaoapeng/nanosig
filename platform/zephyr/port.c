/**
 * @file port.c
 * @brief nanosig Zephyr `k_poll` platform backend.
 * @date 2026-10-11
 *
 * 本后端把 `nanosig/nanosig_port.h` 的平台契约映射到 Zephyr：
 *
 * - 内存：`k_malloc` / `k_free`；平台对象（wakeup / mutex / waitset）由调用方拥有
 *   存储，唯一例外是线程对象（见下条）。
 * - mutex：`k_mutex`（`k_mutex_lock` / `k_mutex_unlock`）。
 * - 线程：`k_thread_create` + `k_thread_join`，栈由 `k_thread_stack_alloc` **运行期**
 *   单独分配（不再内嵌进对象），故依赖 `CONFIG_DYNAMIC_THREAD` +
 *   `CONFIG_DYNAMIC_THREAD_ALLOC`；线程对象仍由本后端分配、`ns_platform_thread_join`
 *   回收，使栈的对齐要求不外传给通用分配器（见 `nanosig_platform_port.h` 的 @note）。
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
 * - **无效句柄**：`NULL` 与 `ns_waitable_init` 的 `(void *)~0` 哨兵都视为无效，
 *   `event_signal` / `event_drain` / `waitset_add` 统一返回 `NS_E_INVAL`，
 *   不把哨兵交给 `k_poll` / 内核。
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
#error "nanosig Zephyr backend requires CONFIG_HEAP_MEM_POOL_SIZE > 0 (k_malloc / k_free)"
#endif

/* 可配置常量（NS_ZEPHYR_WAITSET_CAP / NS_ZEPHYR_THREAD_PRIORITY /
   NS_ZEPHYR_DEFAULT_THREAD_STACK_SIZE / NS_ZEPHYR_MIN_THREAD_STACK_SIZE）与平台类型由
   构建内头 platform/zephyr/nanosig_platform_port.h 提供（按固定名由 include 路径
   选中）；平台对象存储归调用方所有。 */

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
    return k_malloc(size);
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

/* 线程对象由本后端拥有：内核栈不再内嵌进对象，而是由 `k_thread_stack_alloc`
   单独分配，对象本身只剩 `struct k_thread` + 两个指针，用普通 `ns_platform_alloc`
   即可，栈对齐要求不再外传给通用分配器。 */
struct ns_platform_thread {
    struct k_thread        thread;
    ns_platform_thread_fn  entry;
    void                  *arg;
    k_thread_stack_t      *stack;
};

static void ns_zephyr_thread_trampoline(void *p1, void *p2, void *p3)
{
    struct ns_platform_thread *thread = (struct ns_platform_thread *)p1;

    (void)p2;
    (void)p3;

    thread->entry(thread->arg);
}

int ns_platform_thread_stack_range(size_t *min_out, size_t *max_out)
{
    /* Zephyr 3.7 没有 Z_MIN_THREAD_STACK_SIZE，k_thread_stack_alloc 自身也不做
       任何下限检查 —— 过小的栈会被静默分配然后运行时溢栈（踩坏相邻堆）。故下限
       由本后端把关：这是 nanosig 策略值，不是 Zephyr 平台保证。 */
    if(min_out != NULL) *min_out = (size_t)NS_ZEPHYR_MIN_THREAD_STACK_SIZE;
    if(max_out != NULL) *max_out = SIZE_MAX;
    return NS_OK;
}

int ns_platform_thread_init(
    ns_platform_thread_t *thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name,
    size_t stack_size)
{
    struct ns_platform_thread *obj;
    size_t stack_bytes;
    k_tid_t tid;

    (void)debug_name;

    if(thread == NULL) return NS_E_INVAL;

    *thread = NULL;
    if(entry == NULL) return NS_E_INVAL;

    if(stack_size == 0u){
        stack_bytes = (size_t)NS_ZEPHYR_DEFAULT_THREAD_STACK_SIZE;
    }else{
        if(stack_size < (size_t)NS_ZEPHYR_MIN_THREAD_STACK_SIZE) return NS_E_INVAL;
        stack_bytes = stack_size;
    }

    obj = (struct ns_platform_thread *)ns_platform_alloc(sizeof(*obj));
    if(obj == NULL) return NS_E_NOMEM;

    obj->entry = entry;
    obj->arg = arg;
    obj->stack = k_thread_stack_alloc(stack_bytes, 0);
    if(obj->stack == NULL){
        ns_platform_free(obj);
        return NS_E_NOMEM;
    }

    /* k_thread_create 返回 k_tid_t（struct k_thread *），不是 int：Zephyr 3.7 的
       实现要么成功返回线程 id、要么内部断言失败，不会返回错误码。这里仍做
       NULL 检查（防御性），失败时回收栈与对象。 */
    tid = k_thread_create(&obj->thread, obj->stack, stack_bytes,
                          ns_zephyr_thread_trampoline, obj, NULL, NULL,
                          NS_ZEPHYR_THREAD_PRIORITY, 0u, K_NO_WAIT);
    if(tid == NULL){
        if(k_thread_stack_free(obj->stack) != 0){
            printk("nanosig: thread stack free failed after create failure\n");
        }
        ns_platform_free(obj);
        return NS_E_NOMEM;
    }

    *thread = obj;
    return NS_OK;
}

int ns_platform_thread_join(ns_platform_thread_t *thread)
{
    struct ns_platform_thread *obj;
    int rc;

    if((thread == NULL) || (*thread == NULL)) return NS_E_INVAL;

    obj = *thread;

    if(k_thread_join(&obj->thread, K_FOREVER) != 0){
        /* join 失败（如自 join 返回 -EDEADLK）时目标线程可能仍在运行：**不得**
           释放它的栈与对象，否则运行中的线程会踩到已释放内存。保留句柄，
           交由调用方重试或按平台契约处理。 */
        printk("nanosig: thread join failed\n");
        return NS_E_INVAL;
    }

    *thread = NULL;
    rc = NS_OK;
    if(k_thread_stack_free(obj->stack) != 0){
        printk("nanosig: thread stack free failed\n");
        rc = NS_E_INVAL;
    }

    ns_platform_free(obj);
    return rc;
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

    if((event == NULL) || !ns_waitable_handle_is_valid(event->signal_handle)) return NS_E_INVAL;

    sig = (struct k_poll_signal *)event->signal_handle;

    /* 布尔语义：-EAGAIN 表示投递时 poller 超时正在到期，但 signaled 仍被置位，
       目标等待面已处于 signaled，按成功返回。 */
    (void)k_poll_signal_raise(sig, 0);
    return NS_OK;
}

int ns_platform_event_drain(ns_platform_event_t *event)
{
    struct k_poll_signal *sig;

    if((event == NULL) || !ns_waitable_handle_is_valid(event->waitable.primitive)) return NS_E_INVAL;

    sig = (struct k_poll_signal *)event->waitable.primitive;

    /* 幂等复位；这是 k_poll_signal_reset 的唯一调用点。 */
    k_poll_signal_reset(sig);
    return NS_OK;
}

int ns_platform_event_deinit(ns_platform_event_t *event)
{
    if(event == NULL) return NS_E_INVAL;
    if(event->waitable.registered_waitset != NULL) return NS_E_BUSY;

    if(ns_waitable_handle_is_valid(event->waitable.primitive)){
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

    /* 无效句柄（`ns_waitable_init` 的 `(void *)~0` 哨兵或 NULL）返回 NS_E_INVAL，
       与 Linux/macOS 一致；否则哨兵会被交给 k_poll 解引用。 */
    if(!ns_waitable_handle_is_valid(waitable->primitive)) return NS_E_INVAL;

    /* 入口已排除 count >= CAP，且 count 与 slots[] 在 add/remove 中成对维护，
       故此处必有空槽（i < CAP）；无需不可达的越界返回分支。 */
    for(i = 0; i < (size_t)NS_ZEPHYR_WAITSET_CAP; i++){
        if(waitset->slots[i] == NULL) break;
    }

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
