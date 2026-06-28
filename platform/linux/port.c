/**
 * @file port.c
 * @brief nanosig Linux loop-only platform backend.
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <nanosig/nanosig_port.h>

struct ns_platform_wakeup {
    int fd;
};

struct ns_platform_mutex {
    pthread_mutex_t mutex;
};

struct ns_platform_thread {
    pthread_t thread;
    ns_platform_thread_fn entry;
    void *arg;
};

static int ns_linux_wakeup_drain(ns_platform_wakeup_t *wakeup)
{
    uint64_t value;

    for(;;){
        ssize_t rc = read(wakeup->fd, &value, sizeof(value));
        if(rc == (ssize_t)sizeof(value)) continue;
        if((rc < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) return NS_OK;
        if((rc < 0) && (errno == EINTR)) continue;
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

int ns_platform_wakeup_create(ns_platform_wakeup_t **out_wakeup, const char *debug_name)
{
    ns_platform_wakeup_t *wakeup;

    (void)debug_name;

    if(out_wakeup == NULL) return NS_E_INVAL;

    *out_wakeup = NULL;
    wakeup = (ns_platform_wakeup_t *)ns_platform_alloc(sizeof(*wakeup));
    if(wakeup == NULL) return NS_E_NOMEM;

    wakeup->fd = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
    if(wakeup->fd < 0){
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

    do{
        rc = close(wakeup->fd);
    }while((rc < 0) && (errno == EINTR));

    ns_platform_free(wakeup);
    return (rc == 0) ? NS_OK : NS_E_INVAL;
}

int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup)
{
    uint64_t value = 1u;

    if(wakeup == NULL) return NS_E_INVAL;

    for(;;){
        ssize_t rc = write(wakeup->fd, &value, sizeof(value));
        if(rc == (ssize_t)sizeof(value)) return NS_OK;
        if((rc < 0) && (errno == EINTR)) continue;
        if((rc < 0) && (errno == EAGAIN)) return NS_E_QUEUE_FULL;
        return NS_E_INVAL;
    }
}

/**
 * @brief 等待单个 wakeup。
 *
 * 超时精度为毫秒（poll 的原生粒度）。本函数只用于 loop 的简单等待路径，
 * 不需要微秒精度。需要微秒精度的场景应使用 ns_platform_waitset_wait。
 */
int ns_platform_wakeup_wait(
    ns_platform_wakeup_t *wakeup,
    ns_platform_time_us_t timeout_us,
    ns_platform_wait_result_t *out_result)
{
    struct pollfd pfd;
    int poll_timeout;

    if((wakeup == NULL) || (out_result == NULL)) return NS_E_INVAL;

    if(timeout_us == 0u) poll_timeout = 0;
    else if(timeout_us == NS_PLATFORM_WAIT_INFINITE_US) poll_timeout = -1;
    else{
        uint64_t ms = (timeout_us + 999u) / 1000u;
        poll_timeout = (ms > (uint64_t)INT32_MAX) ? INT32_MAX : (int)ms;
    }

    pfd.fd = wakeup->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    for(;;){
        int rc = poll(&pfd, 1u, poll_timeout);
        if(rc > 0){
            int drain_rc = ns_linux_wakeup_drain(wakeup);
            if(drain_rc != NS_OK) return drain_rc;
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

ns_platform_waitable_t ns_platform_wakeup_get_waitable(ns_platform_wakeup_t *wakeup)
{
    ns_platform_waitable_t waitable;

    ns_waitable_init(&waitable);

    if(wakeup != NULL){
        waitable.primitive.fd = wakeup->fd;
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
    if(pthread_mutex_lock(&mutex->mutex) != 0) return NS_E_INVAL;

    return NS_OK;
}

int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex)
{
    if(mutex == NULL) return NS_E_INVAL;
    if(pthread_mutex_unlock(&mutex->mutex) != 0) return NS_E_INVAL;

    return NS_OK;
}

int ns_platform_clock_monotonic_us(ns_platform_time_us_t *out_now_us)
{
    struct timespec ts;

    if(out_now_us == NULL) return NS_E_INVAL;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return NS_E_INVAL;

    *out_now_us = ((uint64_t)ts.tv_sec * 1000000u) + ((uint64_t)ts.tv_nsec / 1000u);
    return NS_OK;
}

static void *ns_linux_thread_main(void *arg)
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
    rc = pthread_create(&thread->thread, NULL, ns_linux_thread_main, thread);
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

#define NS_WAITSET_MAX_ENTRIES 64u
#define NS_WAITSET_TIMER_SENTINEL ((void *)(intptr_t)0x1)

/* Linux：epoll data.ptr 直接指向 caller 的 waitable，不需要内部存储 */
struct ns_platform_waitset {
    int    epoll_fd;
    int    timer_fd;
    size_t count;
};

int ns_platform_waitset_create(ns_platform_waitset_t **out_waitset)
{
    ns_platform_waitset_t *ws;
    int epfd;
    int tfd;

    if(out_waitset == NULL) return NS_E_INVAL;

    *out_waitset = NULL;
    ws = (ns_platform_waitset_t *)ns_platform_alloc(sizeof(*ws));
    if(ws == NULL) return NS_E_NOMEM;

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if(epfd < 0){
        ns_platform_free(ws);
        return NS_E_NOMEM;
    }

    tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(tfd < 0){
        close(epfd);
        ns_platform_free(ws);
        return NS_E_NOMEM;
    }

    /* timerfd 永久加入 epoll，data.ptr 用 sentinel 区分 */
    {
        struct epoll_event tev;
        tev.events = EPOLLIN;
        tev.data.ptr = NS_WAITSET_TIMER_SENTINEL;
        if(epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &tev) < 0){
            close(tfd);
            close(epfd);
            ns_platform_free(ws);
            return NS_E_NOMEM;
        }
    }

    ws->epoll_fd = epfd;
    ws->timer_fd = tfd;
    ws->count = 0u;
    *out_waitset = ws;
    return NS_OK;
}

int ns_platform_waitset_destroy(ns_platform_waitset_t *waitset)
{
    int rc_epoll, rc_timer;

    if(waitset == NULL) return NS_E_INVAL;
    if(waitset->count != 0u) return NS_E_EXISTS;

    do{
        rc_epoll = close(waitset->epoll_fd);
    }while((rc_epoll < 0) && (errno == EINTR));

    do{
        rc_timer = close(waitset->timer_fd);
    }while((rc_timer < 0) && (errno == EINTR));

    ns_platform_free(waitset);
    return (rc_epoll == 0 && rc_timer == 0) ? NS_OK : NS_E_INVAL;
}

static uint32_t ns_waitset_to_epoll_events(uint32_t events)
{
    uint32_t ep_events = 0u;

    if(events & NS_WAITABLE_EVENT_IN)  ep_events |= EPOLLIN;
    if(events & NS_WAITABLE_EVENT_OUT) ep_events |= EPOLLOUT;
    if(events & NS_WAITABLE_EVENT_ERR) ep_events |= EPOLLERR;

    return ep_events;
}

static uint32_t ns_waitset_from_epoll_events(uint32_t ep_events)
{
    uint32_t events = 0u;

    if(ep_events & EPOLLIN)  events |= NS_WAITABLE_EVENT_IN;
    if(ep_events & EPOLLOUT) events |= NS_WAITABLE_EVENT_OUT;
    if(ep_events & EPOLLERR) events |= NS_WAITABLE_EVENT_ERR;
    if(ep_events & EPOLLHUP) events |= NS_WAITABLE_EVENT_IN;

    return events;
}

int ns_platform_waitset_add(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    struct epoll_event ev;

    if((waitset == NULL) || (waitable == NULL) || (waitable->primitive.fd < 0)) return NS_E_INVAL;
    if(waitable->registered_waitset != NULL) return NS_E_EXISTS;
    if(waitset->count >= NS_WAITSET_MAX_ENTRIES) return NS_E_TOO_MANY_HANDLES;

    ev.events = ns_waitset_to_epoll_events(waitable->events);
    if(waitable->edge_triggered) ev.events |= EPOLLET;
    ev.data.ptr = (void *)waitable; /* 零拷贝：直接引用 caller 的 waitable */

    if(epoll_ctl(waitset->epoll_fd, EPOLL_CTL_ADD, waitable->primitive.fd, &ev) < 0){
        if(errno == EEXIST) return NS_E_EXISTS;
        return NS_E_INVAL;
    }

    waitset->count++;
    waitable->registered_waitset = waitset;
    return NS_OK;
}

int ns_platform_waitset_remove(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable)
{
    if((waitset == NULL) || (waitable == NULL) || (waitable->primitive.fd < 0)) return NS_E_INVAL;
    if(waitable->registered_waitset != waitset) return NS_E_INVAL;

    if(epoll_ctl(waitset->epoll_fd, EPOLL_CTL_DEL, waitable->primitive.fd, NULL) < 0){
        return NS_E_INVAL;
    }

    waitset->count--;
    waitable->registered_waitset = NULL;
    return NS_OK;
}

/**
 * @brief 等待事件（微秒精度）。
 *
 * timeout 使用预创建的 timerfd 实现微秒精度。timerfd 永久加入 epoll，
 * 每次 wait 前 arm（timerfd_settime 会重置过期计数器），不需要 disarm 或 read。
 *
 * epoll_data.ptr 指向 caller 的 ns_platform_waitable_t，wait 时直接解引用，
 * 不需要额外存储。
 */
int ns_platform_waitset_wait(
    ns_platform_waitset_t *waitset,
    ns_platform_time_us_t timeout_us,
    ns_platform_waitset_completion_t *completions,
    size_t max_completions,
    size_t *out_count)
{
    int timeout_ms;
    int nfds;
    int i;
    int timer_armed = 0;

    if((waitset == NULL) || (completions == NULL) || (out_count == NULL)) return NS_E_INVAL;

    *out_count = 0u;
    if(max_completions == 0u) return NS_OK;

    if(timeout_us != 0u && timeout_us != NS_PLATFORM_WAIT_INFINITE_US){
        struct itimerspec its;
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = (time_t)(timeout_us / 1000000u);
        its.it_value.tv_nsec = (long)((timeout_us % 1000000u) * 1000u);
        if(timerfd_settime(waitset->timer_fd, 0, &its, NULL) < 0) return NS_E_INVAL;
        timer_armed = 1;
        timeout_ms = -1;
    }else if(timeout_us == 0u){
        timeout_ms = 0;
    }else{
        timeout_ms = -1;
    }

    {
        struct epoll_event events[NS_WAITSET_MAX_ENTRIES];
        int max_events = (max_completions > NS_WAITSET_MAX_ENTRIES)
            ? (int)NS_WAITSET_MAX_ENTRIES : (int)max_completions;

        nfds = epoll_wait(waitset->epoll_fd, events, max_events, timeout_ms);
        if(nfds < 0){
            if(errno == EINTR) return NS_OK;
            return NS_E_INVAL;
        }

        for(i = 0; i < nfds && (size_t)*out_count < max_completions; i++){
            ns_platform_waitable_t *wp = (ns_platform_waitable_t *)events[i].data.ptr;

            /* timerfd sentinel → 不报告 completion */
            if(wp == NS_WAITSET_TIMER_SENTINEL){
                if(timer_armed) return NS_OK; /* 有限超时 → 到期 */
                /* 非 armed 路径（INFINITE/0）：残余 readiness，drain timerfd */
                {
                    uint64_t dummy;
                    ssize_t rd = read(waitset->timer_fd, &dummy, sizeof(dummy));
                    (void)rd;
                }
                continue;
            }

            completions[*out_count].waitable = wp;
            completions[*out_count].triggered_events = ns_waitset_from_epoll_events(events[i].events);
            (*out_count)++;
        }
    }

    return NS_OK;
}
