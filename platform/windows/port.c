/**
 * @file port.c
 * @brief nanosig Windows loop-only platform backend.
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#define WIN32_LEAN_AND_MEAN

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <process.h>

#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_port.h>

struct ns_platform_wakeup {
    HANDLE event;
};

struct ns_platform_mutex {
    SRWLOCK lock;
};

struct ns_platform_thread {
    HANDLE thread;
    ns_platform_thread_fn entry;
    void *arg;
};

static DWORD ns_windows_timeout_ms(ns_platform_time_us_t timeout_us)
{
    uint64_t timeout_ms;

    if(timeout_us == NS_PLATFORM_WAIT_INFINITE_US) return INFINITE;

    timeout_ms = (timeout_us + 999u) / 1000u;
    if(timeout_ms >= (uint64_t)INFINITE) return INFINITE - 1u;

    return (DWORD)timeout_ms;
}

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
    return malloc(size);
}

void ns_platform_free(void *ptr)
{
    free(ptr);
}

void stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    platform_stdout_write(stream, buf, size);
}

NS_FUNCTION_WEAK void platform_stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    (void)stream;
    (void)fwrite(buf, 1, size, stdout);
}

int ns_platform_wakeup_create(ns_platform_wakeup_t **out_wakeup, const char *debug_name)
{
    ns_platform_wakeup_t *wakeup;

    (void)debug_name;

    if(out_wakeup == NULL) return NS_E_INVAL;

    *out_wakeup = NULL;
    wakeup = (ns_platform_wakeup_t *)ns_platform_alloc(sizeof(*wakeup));
    if(wakeup == NULL) return NS_E_NOMEM;

    wakeup->event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if(wakeup->event == NULL){
        ns_platform_free(wakeup);
        return NS_E_NOMEM;
    }

    *out_wakeup = wakeup;
    return NS_OK;
}

int ns_platform_wakeup_destroy(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;

    if(CloseHandle(wakeup->event) == 0){
        ns_platform_free(wakeup);
        return NS_E_INVAL;
    }

    ns_platform_free(wakeup);
    return NS_OK;
}

int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;
    if(SetEvent(wakeup->event) == 0) return NS_E_INVAL;

    return NS_OK;
}

int ns_platform_wakeup_wait(
    ns_platform_wakeup_t *wakeup,
    ns_platform_time_us_t timeout_us,
    ns_platform_wait_result_t *out_result)
{
    DWORD wait_result;

    if((wakeup == NULL) || (out_result == NULL)) return NS_E_INVAL;

    wait_result = WaitForSingleObject(wakeup->event, ns_windows_timeout_ms(timeout_us));
    if(wait_result == WAIT_OBJECT_0){
        *out_result = NS_PLATFORM_WAIT_SIGNALED;
        return NS_OK;
    }
    if(wait_result == WAIT_TIMEOUT){
        *out_result = NS_PLATFORM_WAIT_TIMEOUT;
        return NS_OK;
    }

    return NS_E_INVAL;
}

ns_platform_waitable_t ns_platform_wakeup_get_waitable(ns_platform_wakeup_t *wakeup)
{
    ns_platform_waitable_t waitable;

    ns_waitable_init(&waitable);

    if(wakeup != NULL){
        waitable.primitive.handle = wakeup->event;
    }

    return waitable;
}

int ns_platform_mutex_create(ns_platform_mutex_t **out_mutex, const char *debug_name)
{
    ns_platform_mutex_t *mutex;

    (void)debug_name;

    if(out_mutex == NULL) return NS_E_INVAL;

    *out_mutex = NULL;
    mutex = (ns_platform_mutex_t *)ns_platform_alloc(sizeof(*mutex));
    if(mutex == NULL) return NS_E_NOMEM;

    InitializeSRWLock(&mutex->lock);
    *out_mutex = mutex;
    return NS_OK;
}

int ns_platform_mutex_destroy(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    ns_platform_free(mutex);
    return NS_OK;
}

int ns_platform_mutex_lock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    AcquireSRWLockExclusive(&mutex->lock);
    return NS_OK;
}

int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    ReleaseSRWLockExclusive(&mutex->lock);
    return NS_OK;
}

int ns_platform_clock_monotonic_us(ns_platform_time_us_t *out_now_us)
{
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    uint64_t count;
    uint64_t freq;

    if(out_now_us == NULL) return NS_E_INVAL;
    if(QueryPerformanceFrequency(&frequency) == 0) return NS_E_INVAL;
    if(QueryPerformanceCounter(&counter) == 0) return NS_E_INVAL;
    if(frequency.QuadPart <= 0) return NS_E_INVAL;

    count = (uint64_t)counter.QuadPart;
    freq = (uint64_t)frequency.QuadPart;
    *out_now_us = ((count / freq) * 1000000u) + (((count % freq) * 1000000u) / freq);
    return NS_OK;
}

static unsigned int __stdcall ns_windows_thread_main(void *arg)
{
    ns_platform_thread_t *thread = (ns_platform_thread_t *)arg;

    thread->entry(thread->arg);
    return 0u;
}

int ns_platform_thread_create(
    ns_platform_thread_t **out_thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name)
{
    ns_platform_thread_t *thread;

    (void)debug_name;

    if((out_thread == NULL) || (entry == NULL)) return NS_E_INVAL;

    *out_thread = NULL;
    thread = (ns_platform_thread_t *)ns_platform_alloc(sizeof(*thread));
    if(thread == NULL) return NS_E_NOMEM;

    thread->entry = entry;
    thread->arg = arg;
    thread->thread = (HANDLE)_beginthreadex(NULL, 0u, ns_windows_thread_main, thread, 0u, NULL);
    if(thread->thread == NULL){
        ns_platform_free(thread);
        return NS_E_NOMEM;
    }

    *out_thread = thread;
    return NS_OK;
}

int ns_platform_thread_join(ns_platform_thread_t *thread)
{
    int rc = NS_OK;

    if(thread == NULL) return NS_E_INVAL;

    if(WaitForSingleObject(thread->thread, INFINITE) != WAIT_OBJECT_0) rc = NS_E_INVAL;
    if(CloseHandle(thread->thread) == 0) rc = NS_E_INVAL;

    ns_platform_free(thread);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  waitset                                                            */
/* ------------------------------------------------------------------ */

#define NS_PLATFORM_WAITSET_MAX_HANDLES 64u
/* 预留 1 个 slot 给内部 timer handle */
#define NS_PLATFORM_WAITSET_USER_HANDLES (NS_PLATFORM_WAITSET_MAX_HANDLES - 1u)

/* Windows：WFMO 返回 index，需要内部存 waitable 指针做映射 */
struct ns_platform_waitset {
    HANDLE                  handles[NS_PLATFORM_WAITSET_MAX_HANDLES];
    const ns_platform_waitable_t *waitables[NS_PLATFORM_WAITSET_USER_HANDLES]; /* index → caller */
    DWORD                   count;
    HANDLE                  timer;
};

int ns_platform_waitset_create(ns_platform_waitset_t **out_waitset)
{
    ns_platform_waitset_t *ws;

    if(out_waitset == NULL) return NS_E_INVAL;

    *out_waitset = NULL;
    ws = (ns_platform_waitset_t *)ns_platform_alloc(sizeof(*ws));
    if(ws == NULL) return NS_E_NOMEM;

    ws->timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    if(ws->timer == NULL){
        ns_platform_free(ws);
        return NS_E_NOMEM;
    }

    ws->count = 0u;
    *out_waitset = ws;
    return NS_OK;
}

int ns_platform_waitset_destroy(ns_platform_waitset_t *waitset)
{
    if(waitset == NULL) return NS_E_INVAL;
    if(waitset->count != 0u) return NS_E_EXISTS;

    CloseHandle(waitset->timer);
    ns_platform_free(waitset);
    return NS_OK;
}

static int ns_waitset_find(const ns_platform_waitset_t *waitset, HANDLE handle)
{
    DWORD i;
    for(i = 0u; i < waitset->count; i++){
        if(waitset->handles[i] == handle) return (int)i;
    }
    return -1;
}

static int ns_waitable_handle_is_invalid(const ns_platform_waitable_t *waitable)
{
    if(waitable == NULL) return 1;

    return (waitable->primitive.handle == NULL)
        || (waitable->primitive.handle == INVALID_HANDLE_VALUE);
}

int ns_platform_waitset_add(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    if((waitset == NULL) || ns_waitable_handle_is_invalid(waitable)){
        return NS_E_INVAL;
    }
    if(waitable->registered_waitset != NULL) return NS_E_EXISTS;
    if(ns_waitset_find(waitset, (HANDLE)waitable->primitive.handle) >= 0) return NS_E_EXISTS;
    if(waitset->count >= NS_PLATFORM_WAITSET_USER_HANDLES) return NS_E_TOO_MANY_HANDLES;

    waitset->handles[waitset->count] = (HANDLE)waitable->primitive.handle;
    waitset->waitables[waitset->count] = waitable;
    waitset->count++;
    waitable->registered_waitset = waitset;
    return NS_OK;
}

int ns_platform_waitset_remove(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    int idx;
    DWORD last;

    if((waitset == NULL) || ns_waitable_handle_is_invalid(waitable)){
        return NS_E_INVAL;
    }
    if(waitable->registered_waitset != waitset) return NS_E_INVAL;

    idx = ns_waitset_find(waitset, (HANDLE)waitable->primitive.handle);
    if(idx < 0) return NS_E_INVAL;

    last = waitset->count - 1u;
    if((DWORD)idx < last){
        waitset->handles[idx] = waitset->handles[last];
        waitset->waitables[idx] = waitset->waitables[last];
    }
    waitset->count--;
    waitable->registered_waitset = NULL;
    return NS_OK;
}

/**
 * @brief 等待事件（微秒精度）。
 *
 * timeout 使用预创建的 WaitableTimer 实现微秒精度。
 * - timeout > 0：SetWaitableTimer，timer handle 加入 WFMO 数组。
 * - timeout == 0：WFMO(INFINITE 不用，timeout=0) 非阻塞。
 * - timeout == INFINITE：WFMO(INFINITE) 无限等待。
 */
int ns_platform_waitset_wait(
    ns_platform_waitset_t *waitset,
    ns_platform_time_us_t timeout_us,
    ns_platform_waitset_completion_t *completions,
    size_t max_completions,
    size_t *out_count)
{
    HANDLE handles[NS_PLATFORM_WAITSET_MAX_HANDLES];
    DWORD handle_count;
    DWORD timeout_ms;
    DWORD result;
    DWORD i;
    int timer_armed = 0;

    if((waitset == NULL) || (completions == NULL) || (out_count == NULL)) return NS_E_INVAL;

    *out_count = 0u;
    if(waitset->count == 0u && timeout_us == 0u) return NS_OK;

    /* 从 waitables 构建临时 handles 数组 */
    for(i = 0u; i < waitset->count; i++){
        handles[i] = (HANDLE)waitset->waitables[i]->primitive.handle;
    }

    /* arm timer 或设置 timeout */
    if(timeout_us != 0u && timeout_us != NS_PLATFORM_WAIT_INFINITE_US){
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(timeout_us * 10u);
        if(!SetWaitableTimer(waitset->timer, &due, 0, NULL, NULL, FALSE)){
            return NS_E_INVAL;
        }
        handles[waitset->count] = waitset->timer;
        handle_count = waitset->count + 1u;
        timer_armed = 1;
        timeout_ms = INFINITE;
    }else if(timeout_us == 0u){
        handle_count = waitset->count;
        timeout_ms = 0;
    }else{
        handle_count = waitset->count;
        timeout_ms = INFINITE;
    }

    result = WaitForMultipleObjects(handle_count, handles, FALSE, timeout_ms);

    if(result == WAIT_TIMEOUT) return NS_OK;

    if(result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + handle_count){
        i = result - WAIT_OBJECT_0;

        /* timer fired → timeout，不报告 completion */
        if(timer_armed && i == waitset->count) return NS_OK;

        /* 首个 signaled 用户 handle */
        if(*out_count < max_completions){
            completions[*out_count].waitable = waitset->waitables[i];
            completions[*out_count].triggered_events = NS_WAITABLE_EVENT_IN;
            (*out_count)++;
        }

        /* 扫描其余用户 handle，捕获同时 signaled 的 */
        for(i = 0u; i < waitset->count && *out_count < max_completions; i++){
            if(i == result - WAIT_OBJECT_0) continue;
            if(WaitForSingleObject(handles[i], 0u) == WAIT_OBJECT_0){
                completions[*out_count].waitable = waitset->waitables[i];
                completions[*out_count].triggered_events = NS_WAITABLE_EVENT_IN;
                (*out_count)++;
            }
        }

        return NS_OK;
    }

    return NS_E_INVAL;
}
