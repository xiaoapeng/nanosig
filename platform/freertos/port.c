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
 * - waitset：queue set；`waitable.primitive` 是调用方提供的
 *   queue-set-capable 对象（二值/计数信号量或队列）。
 * - 时间：`xTaskGetTickCount`；内存：`pvPortMalloc` / `vPortFree`。
 *
 * 本后端只支持 `NS_WAITABLE_EVENT_IN`；含 OUT/ERR 的 waitable 在
 * `ns_platform_waitset_add` 返回 `NS_E_INVAL`。无效句柄（`NULL` 或
 * `ns_waitable_init` 的 `(void *)~0` 哨兵）同样在 add / event_signal /
 * event_drain 返回 `NS_E_INVAL`，不把哨兵交给 queue set 原语解引用。
 * 后端忽略 `waitable.edge_triggered`
 * （同 Windows/Zephyr）；就绪语义是 queue set 的 token 语义：每次 send/give 记一次
 * 就绪，取走 token 不消费成员数据，也不对仍非空的成员重复上报。
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

/** 平台线程任务的 FreeRTOS 栈深度（字数，StackType_t 单位）。后端内部默认值，
 *  **不是配置面**（不是宏，避免被误当成可配旋钮）；对外可配的编译期旋钮只有
 *  broker 线程的 NANOSIG_BROKER_STACK_SIZE（字节，见 src/ns_broker.c）。 */
static const size_t k_ns_freertos_default_stack_words = 1024u;

/** 平台线程任务的 FreeRTOS 优先级。 */
#ifndef NANOSIG_FREERTOS_TASK_PRIORITY
#define NANOSIG_FREERTOS_TASK_PRIORITY 1
#endif

/* ------------------------------------------------------------------ */
/*  平台对象定义                                                        */
/* ------------------------------------------------------------------ */

/* 平台类型（ns_platform_wakeup_t / mutex / thread / waitset）由构建内头
   platform/freertos/nanosig_platform_port.h 提供，均为 FreeRTOS 原语的透明
   别名，存储归调用方所有。 */

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

/*
 * FreeRTOS 的 TickType_t 会在 2^(8*sizeof(TickType_t)) 个 tick 后回绕，典型 32 位目标
 * 在 configTICK_RATE_HZ=1000 时约 49.7 天（configUSE_16_BIT_TICKS=1 时更短）。公共头
 * 文件承诺 ns_platform_clock_monotonic_us "只保证单调递增"，直接换算原始 tick 会在回绕
 * 瞬间让时间倒流。这里维护一个回绕扩展的 64 位 tick 累加器：用无符号回绕差值
 * (now - last) 累加，回绕后差值仍为非负，累加值只增不减，从而消除时间倒流。
 *
 * 前提（重要）：FreeRTOS 自身的 tick 回绕不能太快——必须保证相邻两次采样之间最多只发生
 * 一次回绕，即采样间隔小于一个 TickType_t 回绕周期。单次采样无法区分"回绕了 1 圈"和
 * "回绕了 N 圈"，若采样间隔跨越多个周期就会少计整周期、表现为时间跳变。因此
 * configTICK_RATE_HZ 与 tick 位宽的组合必须让回绕周期远大于正常采样间隔：nanosig 的
 * timer 路径在每次阻塞等待前后都会采样时钟，常规配置（32 位 tick @1kHz ≈ 49.7 天，或
 * 16 位 tick @1kHz ≈ 65.5 秒）满足；若回绕周期明显更短，需提高采样频率或改用更宽的 tick。
 */
static TickType_t ns_freertos_ticks_last;
static uint64_t   ns_freertos_ticks_accum;

static ns_platform_time_us_t ns_freertos_now_us(void)
{
    uint64_t extended;

    taskENTER_CRITICAL();
    {
        TickType_t now   = xTaskGetTickCount();
        uint64_t   delta = (uint64_t)(TickType_t)(now - ns_freertos_ticks_last);

        ns_freertos_ticks_last   = now;
        ns_freertos_ticks_accum += delta;
        extended                 = ns_freertos_ticks_accum;
    }
    taskEXIT_CRITICAL();

    return (extended * 1000000u) / (uint64_t)configTICK_RATE_HZ;
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
        if((wp != NULL) && (wp->primitive == (void *)member)) return wp;
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

int ns_platform_wakeup_init(ns_platform_wakeup_t *wakeup, const char *debug_name)
{
    (void)debug_name;

    if(wakeup == NULL) return NS_E_INVAL;

    (*wakeup) = xSemaphoreCreateBinary();
    if((*wakeup) == NULL){
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_wakeup_deinit(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;

    if((*wakeup) != NULL) vSemaphoreDelete((*wakeup));
    return NS_OK;
}

int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup)
{
    if((wakeup == NULL) || ((*wakeup) == NULL)) return NS_E_INVAL;

    /* 布尔语义：已 signaled 时 give 失败也视为成功。 */
    (void)xSemaphoreGive((*wakeup));
    return NS_OK;
}

int ns_platform_wakeup_wait(
    ns_platform_wakeup_t *wakeup,
    ns_platform_time_us_t timeout_us,
    ns_platform_wait_result_t *out_result)
{
    BaseType_t rc;

    if((wakeup == NULL) || ((*wakeup) == NULL) || (out_result == NULL)) return NS_E_INVAL;

    rc = xSemaphoreTake((*wakeup), ns_freertos_us_to_ticks(timeout_us));
    *out_result = (rc == pdTRUE) ? NS_PLATFORM_WAIT_SIGNALED : NS_PLATFORM_WAIT_TIMEOUT;
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  mutex                                                              */
/* ------------------------------------------------------------------ */

int ns_platform_mutex_init(ns_platform_mutex_t *mutex, const char *debug_name)
{
    (void)debug_name;

    if(mutex == NULL) return NS_E_INVAL;

    (*mutex) = xSemaphoreCreateMutex();
    if((*mutex) == NULL){
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_mutex_deinit(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    if((*mutex) != NULL) vSemaphoreDelete((*mutex));
    return NS_OK;
}

int ns_platform_mutex_lock(ns_platform_mutex_t *mutex)
{
    if((mutex == NULL) || ((*mutex) == NULL)) return NS_E_INVAL;

    if(xSemaphoreTake((*mutex), portMAX_DELAY) != pdTRUE) return NS_E_INVAL;
    return NS_OK;
}

int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex)
{
    if((mutex == NULL) || ((*mutex) == NULL)) return NS_E_INVAL;

    if(xSemaphoreGive((*mutex)) != pdTRUE) return NS_E_INVAL;
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

int ns_platform_thread_stack_range(size_t *min_out, size_t *max_out)
{
    /* configMINIMAL_STACK_SIZE 是 words 值（xTaskCreate 的深度单位是 StackType_t），
       xTaskCreate 本身并不强制它；本检查是以 FreeRTOS 自身配置为基准的 nanosig 策略。

       返回的 min 是**最小可接受请求字节数**，不是"下限字节数"：请求会向上取整到
       sizeof(StackType_t) 的整数倍，所以 floor-字节数 - sizeof(StackType_t) 这一档
       仍会被补回 floor 而被接受。min - 1 才是必然被拒的那个值。
       例（64 位 harness，StackType_t = 8 字节，configMINIMAL_STACK_SIZE = 1024）：
       floor = 8192 B，min = 8185 B，8184 B 被拒、8185 B 被接受。 */
    if(min_out != NULL){
        *min_out = ((size_t)configMINIMAL_STACK_SIZE - 1u) * sizeof(StackType_t) + 1u;
    }
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
    BaseType_t rc;
    configSTACK_DEPTH_TYPE depth;

    (void)debug_name;

    if((thread == NULL) || (entry == NULL)) return NS_E_INVAL;

    /* 先把资源指针置空：任何提前返回的失败路径都不会留下"看起来已初始化"的
       句柄，否则 join 会拿着没创建过的信号量永久阻塞。 */
    thread->task = NULL;
    thread->join_sem = NULL;

    if(stack_size == 0u){
        depth = (configSTACK_DEPTH_TYPE)k_ns_freertos_default_stack_words;
    }else{
        /* 字节 → words，**向上取整**：保证实际栈字节数 >= 请求字节数。
           拆成 / 与 % 避免 `bytes + sizeof - 1` 在极大值上回绕。 */
        size_t words = (stack_size / sizeof(StackType_t))
                     + ((stack_size % sizeof(StackType_t)) ? 1u : 0u);
        if(words < (size_t)configMINIMAL_STACK_SIZE) return NS_E_INVAL;
        /* configSTACK_DEPTH_TYPE 的位宽由 FreeRTOS 配置决定（默认 uint32_t），
           用它的最大值做上界，避免截断。 */
        if(words > (size_t)(configSTACK_DEPTH_TYPE)(~(configSTACK_DEPTH_TYPE)0)) return NS_E_INVAL;
        depth = (configSTACK_DEPTH_TYPE)words;
    }

    thread->entry = entry;
    thread->arg = arg;
    thread->join_sem = xSemaphoreCreateBinary();
    if(thread->join_sem == NULL){
        return NS_E_NOMEM;
    }

    rc = xTaskCreate(ns_freertos_task_entry, "nanosig",
                     depth,
                     thread, (UBaseType_t)NANOSIG_FREERTOS_TASK_PRIORITY,
                     &thread->task);
    if(rc != pdPASS){
        vSemaphoreDelete(thread->join_sem);
        thread->join_sem = NULL;
        thread->task = NULL;
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_thread_join(ns_platform_thread_t *thread)
{
    if((thread == NULL) || (thread->join_sem == NULL)) return NS_E_INVAL;

    (void)xSemaphoreTake(thread->join_sem, portMAX_DELAY);
    vSemaphoreDelete(thread->join_sem);
    thread->join_sem = NULL;
    thread->task = NULL;
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
    event->signal_handle = NULL;

    sem = xSemaphoreCreateBinary();
    if(sem == NULL) return NS_E_NOMEM;

    event->waitable.primitive = (void *)sem;
    event->signal_handle = (void *)sem;
    return NS_OK;
}

int ns_platform_event_signal(ns_platform_event_t *event)
{
    if((event == NULL) || !ns_waitable_handle_is_valid(event->signal_handle)) return NS_E_INVAL;

    /* 布尔语义：已 signaled 时 give 失败也视为成功。 */
    (void)xSemaphoreGive((SemaphoreHandle_t)event->signal_handle);
    return NS_OK;
}

int ns_platform_event_drain(ns_platform_event_t *event)
{
    if((event == NULL) || !ns_waitable_handle_is_valid(event->waitable.primitive)) return NS_E_INVAL;

    (void)xSemaphoreTake((SemaphoreHandle_t)event->waitable.primitive, 0);
    return NS_OK;
}

int ns_platform_event_deinit(ns_platform_event_t *event)
{
    if(event == NULL) return NS_E_INVAL;
    if(event->waitable.registered_waitset != NULL) return NS_E_BUSY;

    if(ns_waitable_handle_is_valid(event->waitable.primitive)){
        vSemaphoreDelete((SemaphoreHandle_t)event->waitable.primitive);
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
    waitset->set = xQueueCreateSet((UBaseType_t)NANOSIG_FREERTOS_WAITSET_CAP);
    if(waitset->set == NULL){
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_waitset_deinit(ns_platform_waitset_t *waitset)
{
    if(waitset == NULL) return NS_E_INVAL;
    if(waitset->count != 0u) return NS_E_EXISTS;

    vQueueDelete(waitset->set);
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

    /* 无效句柄（`ns_waitable_init` 的 `(void *)~0` 哨兵或 NULL）返回 NS_E_INVAL，
       与 Linux/macOS 一致；否则哨兵会被交给 queue set 原语解引用。 */
    if(!ns_waitable_handle_is_valid(waitable->primitive)) return NS_E_INVAL;

    member = (QueueSetMemberHandle_t)waitable->primitive;

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

    /* 入口已排除 count >= CAP，且 count 与 slots[] 在 add/remove 中成对维护，
       故此处必有空槽（i < CAP）；无需回滚分支。 */
    for(i = 0; i < (size_t)NANOSIG_FREERTOS_WAITSET_CAP; i++){
        if(waitset->slots[i] == NULL) break;
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

    member = (QueueSetMemberHandle_t)waitable->primitive;
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
                    (QueueSetMemberHandle_t)waitset->slots[j]->primitive);
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

    /* 空 waitset 没有可唤醒的事件源，无限等待会永久睡死；封顶为有界等待。 */
    if((waitset->count == 0u) && (timeout_us == NS_PLATFORM_WAIT_INFINITE_US)){
        timeout_us = NS_PLATFORM_WAIT_EMPTY_MAX_US;
    }

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
