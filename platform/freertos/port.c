/**
 * @file port.c
 * @brief nanosig FreeRTOS queue-set platform backend.
 * @date 2026-10-04
 *
 * 本后端把 `nanosig/nanosig_port.h` 的平台契约映射到 FreeRTOS：
 *
 * - mutex：`xSemaphoreCreateMutex`。
 * - 线程：`xTaskCreate` + join 信号量（被 join 任务在 `vTaskDelete(NULL)` 前 signal）。
 * - wakeup：**每对象独立二值信号量**；禁止用 task notification 实现 wakeup，
 *   loop 与 broker op-proxy 的唤醒共享每任务 notification value 会互相误消费。
 * - event：二值信号量，`waitable` 与 `signal_handle` 指向同一信号量。
 * - waitset：queue set；`waitable.primitive.handle` 是调用方提供的
 *   queue-set-capable 对象（二值/计数信号量或队列）。
 * - 时间：`xTaskGetTickCount`；内存：`pvPortMalloc` / `vPortFree`。
 *
 * 本后端只支持 `NS_WAITABLE_EVENT_IN`；含 OUT/ERR 的 waitable 在
 * `ns_platform_waitset_add` 返回 `NS_E_INVAL`。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_port.h>
#define NS_DBG_MODULE_LEVEL_PLATFORM NS_DBG_SYS
#include <nanosig/ns_debug.h>

#if ( configUSE_QUEUE_SETS != 1 )
#error "nanosig FreeRTOS backend requires configUSE_QUEUE_SETS == 1"
#endif
#if ( configSUPPORT_DYNAMIC_ALLOCATION != 1 )
#error "nanosig FreeRTOS backend requires configSUPPORT_DYNAMIC_ALLOCATION == 1"
#endif
#if ( configUSE_MUTEXES != 1 )
#error "nanosig FreeRTOS backend requires configUSE_MUTEXES == 1"
#endif

/* ------------------------------------------------------------------ */
/*  可配置常量（可由构建命令覆盖）                                        */
/* ------------------------------------------------------------------ */

/** queue set 固定容量；`waitset_add` 校验 Σ成员容量 + max(成员容量) 不超过它。 */
#ifndef NANOSIG_FREERTOS_WAITSET_CAP
#define NANOSIG_FREERTOS_WAITSET_CAP 32u
#endif

/** 平台线程任务的 FreeRTOS 栈深度（字数，StackType_t 单位）。 */
#ifndef NANOSIG_FREERTOS_THREAD_STACK_WORDS
#define NANOSIG_FREERTOS_THREAD_STACK_WORDS 1024u
#endif

/** 平台线程任务的 FreeRTOS 优先级。 */
#ifndef NANOSIG_FREERTOS_TASK_PRIORITY
#define NANOSIG_FREERTOS_TASK_PRIORITY 1
#endif

/* ------------------------------------------------------------------ */
/*  平台对象定义                                                        */
/* ------------------------------------------------------------------ */

struct ns_platform_wakeup {
    SemaphoreHandle_t sem;
};

struct ns_platform_mutex {
    SemaphoreHandle_t sem;
};

struct ns_platform_thread {
    TaskHandle_t       task;
    SemaphoreHandle_t  join_sem;
    ns_platform_thread_fn entry;
    void              *arg;
};

struct ns_platform_waitset {
    QueueSetHandle_t       set;
    size_t                 count;
    UBaseType_t            sum_cap;
    UBaseType_t            max_cap;
    ns_platform_waitable_t *slots[NANOSIG_FREERTOS_WAITSET_CAP];
};

/* FreeRTOS 后端 ABI 探针符号；宿主后端导出对称的 ns_platform_abi_host。 */
const int ns_platform_abi_freertos = 0;

/* ------------------------------------------------------------------ */
/*  stdout 覆盖点（与桌面后端一致的 weak 钩子）                           */
/* ------------------------------------------------------------------ */

NS_FUNCTION_WEAK void platform_stdout_write(void *stream, const uint8_t *buf, size_t size);

void stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    platform_stdout_write(stream, buf, size);
}

/* 默认丢弃；应用可强定义 platform_stdout_write 接管串口/ITM 输出。 */
NS_FUNCTION_WEAK void platform_stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    (void)stream;
    (void)buf;
    (void)size;
}

/* ------------------------------------------------------------------ */
/*  内部辅助                                                            */
/* ------------------------------------------------------------------ */

static TickType_t ns_freertos_us_to_ticks(ns_platform_time_us_t timeout_us)
{
    uint64_t ticks;

    if(timeout_us == NS_PLATFORM_WAIT_INFINITE_US) return portMAX_DELAY;
    if(timeout_us == 0u) return 0u;

    ticks = (timeout_us / 1000000u) * (uint64_t)configTICK_RATE_HZ;
    ticks += (((timeout_us % 1000000u) * (uint64_t)configTICK_RATE_HZ) + 999999u) / 1000000u;

    if(ticks >= (uint64_t)portMAX_DELAY) return portMAX_DELAY;

    return (TickType_t)ticks;
}

static ns_platform_time_us_t ns_freertos_now_us(void)
{
    TickType_t ticks = xTaskGetTickCount();

    return ((ns_platform_time_us_t)ticks * 1000000u) / (uint64_t)configTICK_RATE_HZ;
}

static UBaseType_t ns_freertos_member_capacity(QueueSetMemberHandle_t member)
{
    /* 空成员：messages = 0，spaces = 满容量；两者之和恒等于该对象容量。 */
    return uxQueueMessagesWaiting(member) + uxQueueSpacesAvailable(member);
}

static ns_platform_waitable_t *ns_freertos_find_active_member(
    ns_platform_waitset_t *waitset, QueueSetMemberHandle_t member)
{
    size_t i;

    for(i = 0; i < NANOSIG_FREERTOS_WAITSET_CAP; i++){
        ns_platform_waitable_t *wp = waitset->slots[i];
        if((wp != NULL) && (wp->primitive.handle == (void *)member)) return wp;
    }

    return NULL;
}

static int ns_freertos_member_is_active(
    ns_platform_waitset_t *waitset, QueueSetMemberHandle_t member)
{
    return ns_freertos_find_active_member(waitset, member) != NULL;
}

/*
 * 清空 queue set 中不指向任何活跃 slot 的残留 token，再把活跃 token 放回 set。
 *
 * queue set 的 xQueueRemoveFromSet 不会撤销已经入队的 token，被移除成员的 token
 * 可能残留。若之后新成员复用了同一句柄值，残留 token 会被误认成新成员的就绪。
 * 这里在选择/放回之前，先把所有 token 摘下，只把活跃成员的 token 送回。
 * xQueueSelectFromSet 从 set 取 token 但不消费成员本身，因此活跃成员数据不丢。
 */
static void ns_freertos_flush_stale_tokens(ns_platform_waitset_t *waitset)
{
    QueueSetMemberHandle_t pending[NANOSIG_FREERTOS_WAITSET_CAP];
    size_t n = 0;
    size_t i;

    for(;;){
        QueueSetMemberHandle_t member = xQueueSelectFromSet(waitset->set, 0);
        if(member == NULL) break;
        if(n < NANOSIG_FREERTOS_WAITSET_CAP) pending[n++] = member;
    }

    for(i = 0; i < n; i++){
        if(ns_freertos_member_is_active(waitset, pending[i])){
            (void)xQueueSendToBack((QueueHandle_t)waitset->set, &pending[i], 0);
        }
    }
}

static int ns_freertos_completion_contains(
    const ns_platform_waitset_completion_t *completions, size_t count,
    const ns_platform_waitable_t *waitable)
{
    size_t i;

    for(i = 0; i < count; i++){
        if(completions[i].waitable == waitable) return 1;
    }

    return 0;
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
    return pvPortMalloc(size);
}

void ns_platform_free(void *ptr)
{
    if(ptr != NULL) vPortFree(ptr);
}

int ns_platform_clock_monotonic_us(ns_platform_time_us_t *out_now_us)
{
    if(out_now_us == NULL) return NS_E_INVAL;

    *out_now_us = ns_freertos_now_us();
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  wakeup                                                             */
/* ------------------------------------------------------------------ */

int ns_platform_wakeup_create(ns_platform_wakeup_t **out_wakeup, const char *debug_name)
{
    ns_platform_wakeup_t *wakeup;

    (void)debug_name;

    if(out_wakeup == NULL) return NS_E_INVAL;

    wakeup = (ns_platform_wakeup_t *)ns_platform_alloc(sizeof(*wakeup));
    if(wakeup == NULL) return NS_E_NOMEM;

    wakeup->sem = xSemaphoreCreateBinary();
    if(wakeup->sem == NULL){
        ns_platform_free(wakeup);
        return NS_E_NOMEM;
    }

    *out_wakeup = wakeup;
    return NS_OK;
}

int ns_platform_wakeup_destroy(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;

    if(wakeup->sem != NULL) vSemaphoreDelete(wakeup->sem);
    ns_platform_free(wakeup);
    return NS_OK;
}

int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup)
{
    if((wakeup == NULL) || (wakeup->sem == NULL)) return NS_E_INVAL;

    /* 布尔语义：已 signaled 时 give 失败也视为成功。 */
    (void)xSemaphoreGive(wakeup->sem);
    return NS_OK;
}

int ns_platform_wakeup_wait(
    ns_platform_wakeup_t *wakeup,
    ns_platform_time_us_t timeout_us,
    ns_platform_wait_result_t *out_result)
{
    BaseType_t rc;

    if((wakeup == NULL) || (wakeup->sem == NULL) || (out_result == NULL)) return NS_E_INVAL;

    rc = xSemaphoreTake(wakeup->sem, ns_freertos_us_to_ticks(timeout_us));
    *out_result = (rc == pdTRUE) ? NS_PLATFORM_WAIT_SIGNALED : NS_PLATFORM_WAIT_TIMEOUT;
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  mutex                                                              */
/* ------------------------------------------------------------------ */

int ns_platform_mutex_create(ns_platform_mutex_t **out_mutex, const char *debug_name)
{
    ns_platform_mutex_t *mutex;

    (void)debug_name;

    if(out_mutex == NULL) return NS_E_INVAL;

    mutex = (ns_platform_mutex_t *)ns_platform_alloc(sizeof(*mutex));
    if(mutex == NULL) return NS_E_NOMEM;

    mutex->sem = xSemaphoreCreateMutex();
    if(mutex->sem == NULL){
        ns_platform_free(mutex);
        return NS_E_NOMEM;
    }

    *out_mutex = mutex;
    return NS_OK;
}

int ns_platform_mutex_destroy(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    if(mutex->sem != NULL) vSemaphoreDelete(mutex->sem);
    ns_platform_free(mutex);
    return NS_OK;
}

int ns_platform_mutex_lock(ns_platform_mutex_t *mutex)
{
    if((mutex == NULL) || (mutex->sem == NULL)) return NS_E_INVAL;

    if(xSemaphoreTake(mutex->sem, portMAX_DELAY) != pdTRUE) return NS_E_INVAL;
    return NS_OK;
}

int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex)
{
    if((mutex == NULL) || (mutex->sem == NULL)) return NS_E_INVAL;

    if(xSemaphoreGive(mutex->sem) != pdTRUE) return NS_E_INVAL;
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  thread                                                             */
/* ------------------------------------------------------------------ */

static void ns_freertos_task_entry(void *arg)
{
    ns_platform_thread_t *thread = (ns_platform_thread_t *)arg;

    thread->entry(thread->arg);

    /* 先 signal join 信号量，再自删；join 侧 take 到后即可安全释放句柄。 */
    (void)xSemaphoreGive(thread->join_sem);
    vTaskDelete(NULL);
}

int ns_platform_thread_create(
    ns_platform_thread_t **out_thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name)
{
    ns_platform_thread_t *thread;
    BaseType_t rc;

    (void)debug_name;

    if((out_thread == NULL) || (entry == NULL)) return NS_E_INVAL;

    thread = (ns_platform_thread_t *)ns_platform_alloc(sizeof(*thread));
    if(thread == NULL) return NS_E_NOMEM;

    thread->task = NULL;
    thread->entry = entry;
    thread->arg = arg;
    thread->join_sem = xSemaphoreCreateBinary();
    if(thread->join_sem == NULL){
        ns_platform_free(thread);
        return NS_E_NOMEM;
    }

    rc = xTaskCreate(ns_freertos_task_entry, "nanosig",
                     (configSTACK_DEPTH_TYPE)NANOSIG_FREERTOS_THREAD_STACK_WORDS,
                     thread, (UBaseType_t)NANOSIG_FREERTOS_TASK_PRIORITY,
                     &thread->task);
    if(rc != pdPASS){
        vSemaphoreDelete(thread->join_sem);
        ns_platform_free(thread);
        return NS_E_NOMEM;
    }

    *out_thread = thread;
    return NS_OK;
}

int ns_platform_thread_join(ns_platform_thread_t *thread)
{
    if(thread == NULL) return NS_E_INVAL;

    (void)xSemaphoreTake(thread->join_sem, portMAX_DELAY);
    vSemaphoreDelete(thread->join_sem);
    ns_platform_free(thread);
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  event                                                              */
/* ------------------------------------------------------------------ */

int ns_platform_event_init(ns_platform_event_t *event, const char *debug_name)
{
    SemaphoreHandle_t sem;

    (void)debug_name;

    if(event == NULL) return NS_E_INVAL;

    ns_waitable_init(&event->waitable);
    event->signal_handle.handle = NULL;

    sem = xSemaphoreCreateBinary();
    if(sem == NULL) return NS_E_NOMEM;

    event->waitable.primitive.handle = (void *)sem;
    event->signal_handle.handle = (void *)sem;
    return NS_OK;
}

int ns_platform_event_signal(ns_platform_event_t *event)
{
    if((event == NULL) || (event->signal_handle.handle == NULL)) return NS_E_INVAL;

    /* 布尔语义：已 signaled 时 give 失败也视为成功。 */
    (void)xSemaphoreGive((SemaphoreHandle_t)event->signal_handle.handle);
    return NS_OK;
}

int ns_platform_event_drain(ns_platform_event_t *event)
{
    if((event == NULL) || (event->waitable.primitive.handle == NULL)) return NS_E_INVAL;

    (void)xSemaphoreTake((SemaphoreHandle_t)event->waitable.primitive.handle, 0);
    return NS_OK;
}

int ns_platform_event_deinit(ns_platform_event_t *event)
{
    if(event == NULL) return NS_E_INVAL;
    if(event->waitable.registered_waitset != NULL) return NS_E_BUSY;

    if(event->waitable.primitive.handle != NULL){
        vSemaphoreDelete((SemaphoreHandle_t)event->waitable.primitive.handle);
    }

    ns_waitable_init(&event->waitable);
    event->signal_handle.handle = NULL;
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  waitset                                                            */
/* ------------------------------------------------------------------ */

int ns_platform_waitset_create(ns_platform_waitset_t **out_waitset)
{
    ns_platform_waitset_t *waitset;

    if(out_waitset == NULL) return NS_E_INVAL;

    waitset = (ns_platform_waitset_t *)ns_platform_alloc(sizeof(*waitset));
    if(waitset == NULL) return NS_E_NOMEM;

    memset(waitset, 0, sizeof(*waitset));
    waitset->set = xQueueCreateSet((UBaseType_t)NANOSIG_FREERTOS_WAITSET_CAP);
    if(waitset->set == NULL){
        ns_platform_free(waitset);
        return NS_E_NOMEM;
    }

    *out_waitset = waitset;
    return NS_OK;
}

int ns_platform_waitset_destroy(ns_platform_waitset_t *waitset)
{
    if(waitset == NULL) return NS_E_INVAL;
    if(waitset->count != 0u) return NS_E_EXISTS;

    vQueueDelete(waitset->set);
    ns_platform_free(waitset);
    return NS_OK;
}

int ns_platform_waitset_add(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    QueueSetMemberHandle_t member;
    UBaseType_t cap;
    UBaseType_t new_sum;
    UBaseType_t new_max;
    size_t i;
    BaseType_t rc;

    if((waitset == NULL) || (waitable == NULL)) return NS_E_INVAL;

    /* FreeRTOS 后端 IN-only。 */
    if((waitable->events & NS_WAITABLE_EVENT_IN) == 0u) return NS_E_INVAL;
    if((waitable->events & (NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR)) != 0u) return NS_E_INVAL;

    if(waitable->registered_waitset != NULL) return NS_E_EXISTS;
    if(waitset->count >= (size_t)NANOSIG_FREERTOS_WAITSET_CAP) return NS_E_TOO_MANY_HANDLES;

    member = (QueueSetMemberHandle_t)waitable->primitive.handle;
    if(member == NULL) return NS_E_INVAL;

    /* FreeRTOS queue set 要求成员在加入时为空。 */
    if(uxQueueMessagesWaiting(member) != 0u) return NS_E_INVAL;

    cap = ns_freertos_member_capacity(member);
    new_sum = waitset->sum_cap + cap;
    new_max = (cap > waitset->max_cap) ? cap : waitset->max_cap;
    if((uint64_t)new_sum + (uint64_t)new_max > (uint64_t)NANOSIG_FREERTOS_WAITSET_CAP){
        return NS_E_TOO_MANY_HANDLES;
    }

    ns_freertos_flush_stale_tokens(waitset);

    rc = xQueueAddToSet(member, waitset->set);
    if(rc != pdPASS) return NS_E_INVAL;

    for(i = 0; i < (size_t)NANOSIG_FREERTOS_WAITSET_CAP; i++){
        if(waitset->slots[i] == NULL) break;
    }
    if(i == (size_t)NANOSIG_FREERTOS_WAITSET_CAP){
        (void)xQueueRemoveFromSet(member, waitset->set);
        return NS_E_TOO_MANY_HANDLES;
    }

    waitset->slots[i] = waitable;
    waitset->count++;
    waitset->sum_cap = new_sum;
    waitset->max_cap = new_max;
    waitable->registered_waitset = waitset;
    return NS_OK;
}

int ns_platform_waitset_remove(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    QueueSetMemberHandle_t member;
    UBaseType_t cap;
    size_t i;
    BaseType_t rc;

    if((waitset == NULL) || (waitable == NULL)) return NS_E_INVAL;
    if(waitable->registered_waitset != waitset) return NS_E_INVAL;

    for(i = 0; i < (size_t)NANOSIG_FREERTOS_WAITSET_CAP; i++){
        if(waitset->slots[i] == waitable) break;
    }
    if(i == (size_t)NANOSIG_FREERTOS_WAITSET_CAP) return NS_E_INVAL;

    member = (QueueSetMemberHandle_t)waitable->primitive.handle;
    cap = ns_freertos_member_capacity(member);

    /* 先 drain 成员使其为空，xQueueRemoveFromSet 才能成功。残留 set token 由
       wait 的 stale 过滤消化，契约对调用方只暴露 NS_OK / NS_E_INVAL。 */
    xQueueReset((QueueHandle_t)member);

    rc = xQueueRemoveFromSet(member, waitset->set);
    if(rc != pdPASS) return NS_E_INVAL;

    waitset->slots[i] = NULL;
    waitset->count--;
    waitset->sum_cap = (cap <= waitset->sum_cap) ? (waitset->sum_cap - cap) : 0u;

    {
        UBaseType_t max_cap = 0u;
        size_t j;

        for(j = 0; j < (size_t)NANOSIG_FREERTOS_WAITSET_CAP; j++){
            if(waitset->slots[j] != NULL){
                UBaseType_t c = ns_freertos_member_capacity(
                    (QueueSetMemberHandle_t)waitset->slots[j]->primitive.handle);
                if(c > max_cap) max_cap = c;
            }
        }
        waitset->max_cap = max_cap;
    }

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
    TickType_t ticks;
    size_t count = 0;

    if((waitset == NULL) || (completions == NULL) || (out_count == NULL)) return NS_E_INVAL;

    *out_count = 0u;
    if(max_completions == 0u) return NS_OK;

    ticks = ns_freertos_us_to_ticks(timeout_us);

    for(;;){
        QueueSetMemberHandle_t member = xQueueSelectFromSet(waitset->set, ticks);
        ns_platform_waitable_t *wp;

        if(member == NULL) break; /* timeout：无更多就绪 token */
        ticks = 0;                /* 首轮之后一律非阻塞 */

        wp = ns_freertos_find_active_member(waitset, member);
        if(wp == NULL) continue; /* stale token：跳过 */

        if(!ns_freertos_completion_contains(completions, count, wp)){
            completions[count].waitable = wp;
            completions[count].triggered_events = NS_WAITABLE_EVENT_IN;
            count++;
        }

        if(count >= max_completions) break;
    }

    *out_count = count;
    return NS_OK;
}
