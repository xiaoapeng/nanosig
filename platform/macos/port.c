/**
 * @file port.c
 * @brief nanosig macOS loop-only platform backend.
 * @date 2026-06-19
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_port.h>
#define NS_DBG_MODULE_LEVEL_PLATFORM NS_DBG_SYS
#include <nanosig/ns_debug.h>

/* 平台类型（ns_platform_wakeup_t / mutex / thread / waitset）由构建内头
   platform/macos/nanosig_platform_port.h 提供，均为 macOS 原语的透明别名，
   存储归调用方所有。 */

/* 宿主后端 ABI 探针符号；FreeRTOS 后端导出对称的 ns_platform_abi_freertos。 */
const int ns_platform_abi_host = 0;

static int ns_macos_set_cloexec(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFD, 0);
    if(flags < 0) return NS_E_INVAL;
    if(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) return NS_E_INVAL;

    return NS_OK;
}

static int ns_macos_close_fd(int fd)
{
    int rc;

    do{
        rc = close(fd);
    }while((rc < 0) && (errno == EINTR));

    return (rc == 0) ? NS_OK : NS_E_INVAL;
}

#define NS_MACOS_WAKEUP_IDENT ((uintptr_t)1u)

static int ns_macos_user_event_create(void)
{
    struct kevent kev;
    int kq;

    kq = kqueue();
    if(kq < 0) return -1;

    if(ns_macos_set_cloexec(kq) != NS_OK){
        (void)ns_macos_close_fd(kq);
        return -1;
    }

    EV_SET(&kev, NS_MACOS_WAKEUP_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0u, 0, NULL);
    if(kevent(kq, &kev, 1, NULL, 0, NULL) < 0){
        (void)ns_macos_close_fd(kq);
        return -1;
    }

    return kq;
}

static int ns_macos_user_event_signal(int kq)
{
    struct kevent kev;

    EV_SET(&kev, NS_MACOS_WAKEUP_IDENT, EVFILT_USER, 0u, NOTE_TRIGGER, 0, NULL);

    /* Release fence：保证 signal 前的所有写（如 req->rc = op_rc）在 kevent
     * 触发前对等待线程可见，与 Linux eventfd write / Windows SetEvent 的
     * 内核提供语义对齐。详见 docs/review/broker-code-review.md BROKER-023。 */
    atomic_thread_fence(memory_order_release);

    for(;;){
        if(kevent(kq, &kev, 1, NULL, 0, NULL) == 0) return NS_OK;
        if(errno == EINTR) continue;
        return NS_E_INVAL;
    }
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

NS_FUNCTION_WEAK void platform_stdout_write(void *stream, const uint8_t *buf, size_t size);

void stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    platform_stdout_write(stream, buf, size);
}

NS_FUNCTION_WEAK void platform_stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    (void)stream;
    (void)fwrite(buf, 1, size, stdout);
}

int ns_platform_wakeup_init(ns_platform_wakeup_t *wakeup, const char *debug_name)
{
    (void)debug_name;

    if(wakeup == NULL) return NS_E_INVAL;

    (*wakeup) = ns_macos_user_event_create();
    if((*wakeup) < 0){
        ns_merrln(PLATFORM, "user_event_create (kqueue) failed");
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_wakeup_deinit(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;

    return ns_macos_close_fd((*wakeup));
}

int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;

    return ns_macos_user_event_signal((*wakeup));
}

int ns_platform_wakeup_wait(
    ns_platform_wakeup_t *wakeup,
    ns_platform_time_us_t timeout_us,
    ns_platform_wait_result_t *out_result)
{
    struct kevent event;
    struct timespec timeout;
    const struct timespec *timeout_ptr = NULL;
    int rc;

    if((wakeup == NULL) || (out_result == NULL)) return NS_E_INVAL;

    if(timeout_us != NS_PLATFORM_WAIT_INFINITE_US){
        timeout.tv_sec = (time_t)(timeout_us / 1000000u);
        timeout.tv_nsec = (long)((timeout_us % 1000000u) * 1000u);
        timeout_ptr = &timeout;
    }

    for(;;){
        rc = kevent((*wakeup), NULL, 0, &event, 1, timeout_ptr);
        if(rc > 0){
            /* Acquire fence：保证 signal 侧 release 之前的所有写（如 req->rc）
             * 在本线程读取前可见。与 Linux poll / Windows WaitForSingleObject 对齐。 */
            atomic_thread_fence(memory_order_acquire);
            *out_result = NS_PLATFORM_WAIT_SIGNALED;
            return NS_OK;
        }
        if(rc == 0){
            *out_result = NS_PLATFORM_WAIT_TIMEOUT;
            return NS_OK;
        }
        if(errno == EINTR) continue;
        return NS_E_INVAL;
    }
}

static int ns_macos_set_nonblock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0) return NS_E_INVAL;
    if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return NS_E_INVAL;

    return NS_OK;
}

static int ns_macos_event_drain(ns_platform_event_t *event)
{
    char buf[256];

    for(;;){
        ssize_t rc = read(event->waitable.primitive, buf, sizeof(buf));
        if(rc > 0) continue;
        if((rc < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) return NS_OK;
        if((rc < 0) && (errno == EINTR)) continue;
        if(rc == 0) return NS_OK;
        return NS_E_INVAL;
    }
}

int ns_platform_event_init(ns_platform_event_t *event, const char *debug_name)
{
    int fds[2];

    (void)debug_name;

    if(event == NULL) return NS_E_INVAL;

    ns_waitable_init(&event->waitable);
    event->signal_handle = -1;

    if(pipe(fds) < 0){
        ns_merrln(PLATFORM, "pipe failed: %s", strerror(errno));
        return NS_E_NOMEM;
    }

    if((ns_macos_set_cloexec(fds[0]) != NS_OK) || (ns_macos_set_cloexec(fds[1]) != NS_OK)
        || (ns_macos_set_nonblock(fds[0]) != NS_OK) || (ns_macos_set_nonblock(fds[1]) != NS_OK)){
        (void)ns_macos_close_fd(fds[0]);
        (void)ns_macos_close_fd(fds[1]);
        return NS_E_NOMEM;
    }

    event->waitable.primitive = fds[0];
    event->signal_handle = fds[1];
    return NS_OK;
}

int ns_platform_event_signal(ns_platform_event_t *event)
{
    char byte = 1;

    if(event == NULL) return NS_E_INVAL;

    for(;;){
        ssize_t rc = write(event->signal_handle, &byte, sizeof(byte));
        if(rc == (ssize_t)sizeof(byte)) return NS_OK;
        if((rc < 0) && (errno == EINTR)) continue;
        if((rc < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) return NS_OK; /* pipe 满 = 已 signaled */
        ns_merrln(PLATFORM, "event_signal write failed: %s", strerror(errno));
        return NS_E_INVAL;
    }
}

int ns_platform_event_drain(ns_platform_event_t *event)
{
    if(event == NULL) return NS_E_INVAL;
    return ns_macos_event_drain(event);
}

int ns_platform_event_deinit(ns_platform_event_t *event)
{
    int rc_r;
    int rc_w;

    if(event == NULL) return NS_E_INVAL;
    if(event->waitable.registered_waitset != NULL) return NS_E_BUSY;

    rc_r = (event->waitable.primitive >= 0)
        ? ns_macos_close_fd(event->waitable.primitive) : NS_OK;
    rc_w = (event->signal_handle >= 0)
        ? ns_macos_close_fd(event->signal_handle) : NS_OK;

    event->waitable.primitive = -1;
    event->signal_handle = -1;
    return ((rc_r == NS_OK) && (rc_w == NS_OK)) ? NS_OK : NS_E_INVAL;
}

int ns_platform_mutex_init(ns_platform_mutex_t *mutex, const char *debug_name)
{
    (void)debug_name;

    if(mutex == NULL) return NS_E_INVAL;

    if(pthread_mutex_init(mutex, NULL) != 0){
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_mutex_deinit(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    return (pthread_mutex_destroy(mutex) == 0) ? NS_OK : NS_E_INVAL;
}

int ns_platform_mutex_lock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;
    int rc = pthread_mutex_lock(mutex);
    if(rc != 0){
        ns_merrln(PLATFORM, "pthread_mutex_lock failed: %d", rc);
    }
    return (rc == 0) ? NS_OK : NS_E_INVAL;
}

int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;
    int rc = pthread_mutex_unlock(mutex);
    if(rc != 0){
        ns_merrln(PLATFORM, "pthread_mutex_unlock failed: %d", rc);
    }
    return (rc == 0) ? NS_OK : NS_E_INVAL;
}

int ns_platform_clock_monotonic_us(ns_platform_time_us_t *out_now_us)
{
    struct timespec ts;

    if(out_now_us == NULL) return NS_E_INVAL;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return NS_E_INVAL;

    *out_now_us = ((uint64_t)ts.tv_sec * 1000000u) + ((uint64_t)ts.tv_nsec / 1000u);
    return NS_OK;
}

static void *ns_macos_thread_main(void *arg)
{
    ns_platform_thread_t *thread = (ns_platform_thread_t *)arg;

    thread->entry(thread->arg);
    return NULL;
}

int ns_platform_thread_stack_range(size_t *min_out, size_t *max_out)
{
    if(min_out != NULL) *min_out = (size_t)PTHREAD_STACK_MIN;
    if(max_out != NULL) *max_out = SIZE_MAX;
    return NS_OK;
}

/* POSIX 线程栈的分配粒度（页大小）。
   Darwin 的 pthread_attr_setstacksize **要求**请求是页大小的整数倍，否则直接
   返回 EINVAL（它不代为取整）；Linux 侧 glibc 内部同样按页取整，提前取整对
   结果无影响。统一在这里按页粒度向上取整，兑现公共契约"实际分配的栈永远
   不小于请求值"。主流 POSIX 平台页大小恒为 4096，故用常量而非 getpagesize()，
   行为可预测。 */
#define NS_POSIX_THREAD_STACK_GRANULARITY 4096u

int ns_platform_thread_init(
    ns_platform_thread_t *thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name,
    size_t stack_size)
{
    pthread_attr_t attr;
    size_t stack_aligned;
    int rc;

    (void)debug_name;

    if((thread == NULL) || (entry == NULL)) return NS_E_INVAL;

    /* entry/arg 必须在 pthread_create 之前就位：trampoline 一启动就会读它们。 */
    thread->entry = entry;
    thread->arg = arg;
    thread->created = 0;

    if(stack_size == 0u){
        rc = pthread_create(&thread->thread, NULL, ns_macos_thread_main, thread);
        if(rc != 0){
            return (rc == EAGAIN) ? NS_E_NOMEM : NS_E_INVAL;
        }
        thread->created = 1;
        return NS_OK;
    }

    /* 下界用**原始请求**判定：先取整再判下界会让"略小于 PTHREAD_STACK_MIN"
       的请求被补到下限而获准，违反"min - 1 必被拒"的契约。
       PTHREAD_STACK_MIN 在部分 libc 上是运行期值，不是常量表达式，故不做
       静态断言。 */
    if(stack_size < (size_t)PTHREAD_STACK_MIN) return NS_E_INVAL;

    /* 向上取整到页粒度（防溢出）。 */
    if(stack_size > SIZE_MAX - (size_t)(NS_POSIX_THREAD_STACK_GRANULARITY - 1u)) return NS_E_INVAL;
    stack_aligned = ((stack_size + (size_t)(NS_POSIX_THREAD_STACK_GRANULARITY - 1u))
                     / (size_t)NS_POSIX_THREAD_STACK_GRANULARITY)
                    * (size_t)NS_POSIX_THREAD_STACK_GRANULARITY;

    rc = pthread_attr_init(&attr);
    if(rc != 0) return NS_E_NOMEM;

    rc = pthread_attr_setstacksize(&attr, stack_aligned);
    if(rc != 0){
        (void)pthread_attr_destroy(&attr);
        return (rc == ENOMEM) ? NS_E_NOMEM : NS_E_INVAL;
    }

    rc = pthread_create(&thread->thread, &attr, ns_macos_thread_main, thread);
    (void)pthread_attr_destroy(&attr);
    if(rc != 0){
        return (rc == EAGAIN) ? NS_E_NOMEM : NS_E_INVAL;
    }

    thread->created = 1;
    return NS_OK;
}

int ns_platform_thread_join(ns_platform_thread_t *thread)
{
    int rc;

    if((thread == NULL) || (thread->created == 0)) return NS_E_INVAL;

    /* 先清标记（防重复 join）。不能在此清 entry/arg —— 目标线程可能还在
       trampoline 里读它们。 */
    thread->created = 0;

    rc = pthread_join(thread->thread, NULL);
    return (rc == 0) ? NS_OK : NS_E_INVAL;
}

/* ------------------------------------------------------------------ */
/*  waitset                                                            */
/* ------------------------------------------------------------------ */

#define NS_MACOS_WAITSET_MAX_ENTRIES 64u

/* 结构体定义见 platform/macos/nanosig_platform_port.h。 */

int ns_platform_waitset_init(ns_platform_waitset_t *waitset)
{
    if(waitset == NULL) return NS_E_INVAL;

    waitset->kq = kqueue();
    if(waitset->kq < 0){
        return NS_E_NOMEM;
    }

    if(ns_macos_set_cloexec(waitset->kq) != NS_OK){
        (void)ns_macos_close_fd(waitset->kq);
        return NS_E_NOMEM;
    }

    waitset->count = 0u;
    return NS_OK;
}

int ns_platform_waitset_deinit(ns_platform_waitset_t *waitset)
{
    if(waitset == NULL) return NS_E_INVAL;
    if(waitset->count != 0u) return NS_E_EXISTS;

    return ns_macos_close_fd(waitset->kq);
}

static int ns_macos_waitable_fd_is_invalid(const ns_platform_waitable_t *waitable)
{
    return (waitable == NULL) || (waitable->primitive < 0);
}

static size_t ns_macos_waitset_make_changes(
    struct kevent *changes,
    int fd,
    uint32_t events,
    int edge_triggered,
    uint16_t flags)
{
    size_t count = 0u;
    uint16_t read_flags = flags;
    uint16_t write_flags = flags;

    if(edge_triggered != 0){
        read_flags = (uint16_t)(read_flags | EV_CLEAR);
        write_flags = (uint16_t)(write_flags | EV_CLEAR);
    }

    if((events & NS_WAITABLE_EVENT_IN) != 0u){
        EV_SET(&changes[count], (uintptr_t)fd, EVFILT_READ, read_flags, 0u, 0, NULL);
        count++;
    }

    if((events & NS_WAITABLE_EVENT_OUT) != 0u){
        EV_SET(&changes[count], (uintptr_t)fd, EVFILT_WRITE, write_flags, 0u, 0, NULL);
        count++;
    }

    return count;
}

static int ns_macos_waitset_apply_changes(
    ns_platform_waitset_t *waitset,
    struct kevent *changes,
    size_t change_count,
    const ns_platform_waitable_t *waitable)
{
    size_t i;

    if(change_count == 0u) return NS_OK;

    for(i = 0u; i < change_count; i++){
        changes[i].udata = (void *)waitable;
    }

    for(;;){
        if(kevent(waitset->kq, changes, (int)change_count, NULL, 0, NULL) == 0) return NS_OK;
        if(errno == EINTR) continue;
        ns_merrln(PLATFORM, "kevent failed: %s", strerror(errno));
        return NS_E_INVAL;
    }
}

int ns_platform_waitset_add(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    struct kevent changes[2];
    size_t change_count;
    int rc;

    if((waitset == NULL) || ns_macos_waitable_fd_is_invalid(waitable)) return NS_E_INVAL;
    if(waitable->registered_waitset != NULL) return NS_E_EXISTS;
    if(waitset->count >= NS_MACOS_WAITSET_MAX_ENTRIES) return NS_E_TOO_MANY_HANDLES;

    change_count = ns_macos_waitset_make_changes(
        changes,
        waitable->primitive,
        waitable->events,
        waitable->edge_triggered,
        EV_ADD | EV_ENABLE);

    rc = ns_macos_waitset_apply_changes(waitset, changes, change_count, waitable);
    if(rc != NS_OK) return rc;

    waitset->count++;
    waitable->registered_waitset = waitset;
    return NS_OK;
}

int ns_platform_waitset_remove(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    struct kevent changes[2];
    size_t change_count;
    int rc;

    if((waitset == NULL) || ns_macos_waitable_fd_is_invalid(waitable)) return NS_E_INVAL;
    if(waitable->registered_waitset != waitset) return NS_E_INVAL;

    change_count = ns_macos_waitset_make_changes(
        changes,
        waitable->primitive,
        waitable->events,
        0,
        EV_DELETE);

    rc = ns_macos_waitset_apply_changes(waitset, changes, change_count, NULL);
    if(rc != NS_OK) return rc;

    waitset->count--;
    waitable->registered_waitset = NULL;
    return NS_OK;
}

static uint32_t ns_macos_waitset_from_kevent(const struct kevent *event)
{
    uint32_t events = 0u;

    if(event->filter == EVFILT_READ) events |= NS_WAITABLE_EVENT_IN;
    if(event->filter == EVFILT_WRITE) events |= NS_WAITABLE_EVENT_OUT;
    if((event->flags & EV_ERROR) != 0u) events |= NS_WAITABLE_EVENT_ERR;
    if((event->flags & EV_EOF) != 0u) events |= NS_WAITABLE_EVENT_ERR;

    return events;
}

/**
 * @brief Wait for events with microsecond timeout input.
 *
 * kqueue accepts the timeout directly in kevent, so macOS does not need a
 * separate timerfd-style waitable. Wakeups are kqueue descriptors whose
 * internal EVFILT_USER event makes the descriptor readable until drained by
 * ns_platform_wakeup_wait.
 */
int ns_platform_waitset_wait(
    ns_platform_waitset_t *waitset,
    ns_platform_time_us_t timeout_us,
    ns_platform_waitset_completion_t *completions,
    size_t max_completions,
    size_t *out_count)
{
    struct kevent events[NS_MACOS_WAITSET_MAX_ENTRIES];
    struct timespec timeout;
    const struct timespec *timeout_ptr = NULL;
    int max_events;
    int nfds;
    int i;

    if((waitset == NULL) || (completions == NULL) || (out_count == NULL)) return NS_E_INVAL;

    *out_count = 0u;
    if(max_completions == 0u) return NS_OK;

    /* 空 waitset 没有可唤醒的事件源，无限等待会永久睡死；封顶为有界等待。 */
    if((waitset->count == 0u) && (timeout_us == NS_PLATFORM_WAIT_INFINITE_US)){
        timeout_us = NS_PLATFORM_WAIT_EMPTY_MAX_US;
    }

    if(timeout_us != NS_PLATFORM_WAIT_INFINITE_US){
        timeout.tv_sec = (time_t)(timeout_us / 1000000u);
        timeout.tv_nsec = (long)((timeout_us % 1000000u) * 1000u);
        timeout_ptr = &timeout;
    }

    max_events = (max_completions > NS_MACOS_WAITSET_MAX_ENTRIES)
        ? (int)NS_MACOS_WAITSET_MAX_ENTRIES
        : (int)max_completions;

    nfds = kevent(waitset->kq, NULL, 0, events, max_events, timeout_ptr);
    if(nfds < 0){
        if(errno == EINTR) return NS_OK;
        ns_merrln(PLATFORM, "kevent wait failed: %s", strerror(errno));
        return NS_E_INVAL;
    }

    for(i = 0; i < nfds && *out_count < max_completions; i++){
        const ns_platform_waitable_t *waitable = (const ns_platform_waitable_t *)events[i].udata;
        uint32_t triggered_events = ns_macos_waitset_from_kevent(&events[i]);
        size_t j;
        int merged = 0;

        if(waitable == NULL) continue;

        for(j = 0u; j < *out_count; j++){
            if(completions[j].waitable == waitable){
                completions[j].triggered_events |= triggered_events;
                merged = 1;
                break;
            }
        }

        if(merged) continue;

        completions[*out_count].waitable = waitable;
        completions[*out_count].triggered_events = triggered_events;
        (*out_count)++;
    }

    return NS_OK;
}
