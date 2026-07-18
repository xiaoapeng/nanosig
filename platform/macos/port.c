/**
 * @file port.c
 * @brief nanosig macOS loop-only platform backend.
 * @date 2026-06-19
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <errno.h>
#include <fcntl.h>
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

struct ns_platform_wakeup {
    int kq;
};

struct ns_platform_mutex {
    pthread_mutex_t mutex;
};

struct ns_platform_thread {
    pthread_t thread;
    ns_platform_thread_fn entry;
    void *arg;
};

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

int ns_platform_wakeup_create(ns_platform_wakeup_t **out_wakeup, const char *debug_name)
{
    ns_platform_wakeup_t *wakeup;

    (void)debug_name;

    if(out_wakeup == NULL) return NS_E_INVAL;

    *out_wakeup = NULL;
    wakeup = (ns_platform_wakeup_t *)ns_platform_alloc(sizeof(*wakeup));
    if(wakeup == NULL) return NS_E_NOMEM;

    wakeup->kq = ns_macos_user_event_create();
    if(wakeup->kq < 0){
        ns_merrln(PLATFORM, "user_event_create (kqueue) failed");
        ns_platform_free(wakeup);
        return NS_E_NOMEM;
    }

    *out_wakeup = wakeup;
    return NS_OK;
}

int ns_platform_wakeup_destroy(ns_platform_wakeup_t *wakeup)
{
    int rc;

    if(wakeup == NULL) return NS_E_INVAL;

    rc = ns_macos_close_fd(wakeup->kq);
    ns_platform_free(wakeup);
    return rc;
}

int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup)
{
    if(wakeup == NULL) return NS_E_INVAL;

    return ns_macos_user_event_signal(wakeup->kq);
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
        rc = kevent(wakeup->kq, NULL, 0, &event, 1, timeout_ptr);
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

int ns_platform_wakeup_get_waitable(const ns_platform_wakeup_t *wakeup,
    ns_platform_waitable_t *out_waitable)
{
    if(wakeup == NULL || out_waitable == NULL) return NS_E_INVAL;

    ns_waitable_init(out_waitable);
    out_waitable->primitive.fd = wakeup->kq;
    return NS_OK;
}

int ns_platform_mutex_create(ns_platform_mutex_t **out_mutex, const char *debug_name)
{
    ns_platform_mutex_t *mutex;

    (void)debug_name;

    if(out_mutex == NULL) return NS_E_INVAL;

    *out_mutex = NULL;
    mutex = (ns_platform_mutex_t *)ns_platform_alloc(sizeof(*mutex));
    if(mutex == NULL) return NS_E_NOMEM;

    if(pthread_mutex_init(&mutex->mutex, NULL) != 0){
        ns_platform_free(mutex);
        return NS_E_NOMEM;
    }

    *out_mutex = mutex;
    return NS_OK;
}

int ns_platform_mutex_destroy(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;

    if(pthread_mutex_destroy(&mutex->mutex) != 0){
        ns_platform_free(mutex);
        return NS_E_INVAL;
    }

    ns_platform_free(mutex);
    return NS_OK;
}

int ns_platform_mutex_lock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;
    int rc = pthread_mutex_lock(&mutex->mutex);
    if(rc != 0){
        ns_merrln(PLATFORM, "pthread_mutex_lock failed: %d", rc);
    }
    return (rc == 0) ? NS_OK : NS_E_INVAL;
}

int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;
    int rc = pthread_mutex_unlock(&mutex->mutex);
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

int ns_platform_thread_create(
    ns_platform_thread_t **out_thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name)
{
    ns_platform_thread_t *thread;
    int rc;

    (void)debug_name;

    if((out_thread == NULL) || (entry == NULL)) return NS_E_INVAL;

    *out_thread = NULL;
    thread = (ns_platform_thread_t *)ns_platform_alloc(sizeof(*thread));
    if(thread == NULL) return NS_E_NOMEM;

    thread->entry = entry;
    thread->arg = arg;
    rc = pthread_create(&thread->thread, NULL, ns_macos_thread_main, thread);
    if(rc != 0){
        ns_platform_free(thread);
        return (rc == EAGAIN) ? NS_E_NOMEM : NS_E_INVAL;
    }

    *out_thread = thread;
    return NS_OK;
}

int ns_platform_thread_join(ns_platform_thread_t *thread)
{
    int rc;

    if(thread == NULL) return NS_E_INVAL;

    rc = pthread_join(thread->thread, NULL);
    ns_platform_free(thread);
    return (rc == 0) ? NS_OK : NS_E_INVAL;
}

/* ------------------------------------------------------------------ */
/*  waitset                                                            */
/* ------------------------------------------------------------------ */

#define NS_MACOS_WAITSET_MAX_ENTRIES 64u

struct ns_platform_waitset {
    int kq;
    size_t count;
};

int ns_platform_waitset_create(ns_platform_waitset_t **out_waitset)
{
    ns_platform_waitset_t *ws;

    if(out_waitset == NULL) return NS_E_INVAL;

    *out_waitset = NULL;
    ws = (ns_platform_waitset_t *)ns_platform_alloc(sizeof(*ws));
    if(ws == NULL) return NS_E_NOMEM;

    ws->kq = kqueue();
    if(ws->kq < 0){
        ns_platform_free(ws);
        return NS_E_NOMEM;
    }

    if(ns_macos_set_cloexec(ws->kq) != NS_OK){
        (void)ns_macos_close_fd(ws->kq);
        ns_platform_free(ws);
        return NS_E_NOMEM;
    }

    ws->count = 0u;
    *out_waitset = ws;
    return NS_OK;
}

int ns_platform_waitset_destroy(ns_platform_waitset_t *waitset)
{
    int rc;

    if(waitset == NULL) return NS_E_INVAL;
    if(waitset->count != 0u) return NS_E_EXISTS;

    rc = ns_macos_close_fd(waitset->kq);
    ns_platform_free(waitset);
    return rc;
}

static int ns_macos_waitable_fd_is_invalid(const ns_platform_waitable_t *waitable)
{
    return (waitable == NULL) || (waitable->primitive.fd < 0);
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
        waitable->primitive.fd,
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
        waitable->primitive.fd,
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
