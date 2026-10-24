/**
 * @file port.c
 * @brief nanosig Linux loop-only platform backend.
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_port.h>
#define NS_DBG_MODULE_LEVEL_PLATFORM NS_DBG_SYS
#include <nanosig/ns_debug.h>

/* 平台类型（ns_platform_wakeup_t / mutex / thread / waitset）由构建内头
   platform/linux/nanosig_platform_port.h 提供，均为 Linux 原语的透明别名，
   存储归调用方所有。 */

/* 宿主后端 ABI 探针符号；FreeRTOS 后端导出对称的 ns_platform_abi_freertos。 */
const int ns_platform_abi_host = 0;

/* EBADF 告警去重：热重试路径下每轮 drain 都会失败 */
static int ns_linux_warned_closed_eventfd = 0;

/* 读空 eventfd 计数。非可恢复错误的处置分两层：
   ① 先把可读计数消费掉——否则调用方重试时 poll 会因计数器仍为正而立即返回
      POLLIN，形成零等待热自旋；
   ② EBADF 是例外：poll 对已关闭 fd 直接返回 POLLNVAL，清计数无法消除自旋，
      只告警一次，由调用方决定是否终止重试。 */
static int ns_linux_eventfd_drain(int fd)
{
    uint64_t value;
    int saved_errno;

    for(;;){
        ssize_t rc = read(fd, &value, sizeof(value));
        if(rc == (ssize_t)sizeof(value)) continue;
        if((rc < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) return NS_OK;
        if((rc < 0) && (errno == EINTR)) continue;

        saved_errno = errno;
        while(read(fd, &value, sizeof(value)) == (ssize_t)sizeof(value)){
        }
        if((saved_errno == EBADF) && !ns_linux_warned_closed_eventfd){
            ns_linux_warned_closed_eventfd = 1;
            ns_mwarnln(PLATFORM,
                "drain: eventfd closed (EBADF); poll keeps returning POLLNVAL, caller must not retry hot");
        }
        errno = saved_errno;
        return NS_E_INVAL;
    }
}

static int ns_linux_wakeup_drain(ns_platform_wakeup_t *wakeup)
{
    return ns_linux_eventfd_drain((*wakeup));
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

    (*wakeup) = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
    if((*wakeup) < 0){
        ns_merrln(PLATFORM, "eventfd failed: %s", strerror(errno));
        return NS_E_NOMEM;
    }

    return NS_OK;
}

int ns_platform_wakeup_deinit(ns_platform_wakeup_t *wakeup)
{
    int rc;

    if(wakeup == NULL) return NS_E_INVAL;

    do{
        rc = close((*wakeup));
    }while((rc < 0) && (errno == EINTR));

    return (rc == 0) ? NS_OK : NS_E_INVAL;
}

int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup)
{
    uint64_t value = 1u;

    if(wakeup == NULL) return NS_E_INVAL;

    for(;;){
        ssize_t rc = write((*wakeup), &value, sizeof(value));
        if(rc == (ssize_t)sizeof(value)) return NS_OK;
        if((rc < 0) && (errno == EINTR)) continue;
        if((rc < 0) && (errno == EAGAIN)) return NS_E_QUEUE_FULL;
        ns_merrln(PLATFORM, "wakeup_signal write failed: %s", strerror(errno));
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

    pfd.fd = (*wakeup);
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

static int ns_linux_event_drain(ns_platform_event_t *event)
{
    return ns_linux_eventfd_drain(event->waitable.primitive);
}

int ns_platform_event_init(ns_platform_event_t *event, const char *debug_name)
{
    int efd;

    (void)debug_name;

    if(event == NULL) return NS_E_INVAL;

    ns_waitable_init(&event->waitable);
    event->signal_handle = -1;

    efd = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
    if(efd < 0){
        ns_merrln(PLATFORM, "eventfd failed: %s", strerror(errno));
        return NS_E_NOMEM;
    }

    event->waitable.primitive = efd;
    event->signal_handle = efd;
    return NS_OK;
}

int ns_platform_event_signal(ns_platform_event_t *event)
{
    uint64_t value = 1u;

    if(event == NULL) return NS_E_INVAL;

    for(;;){
        ssize_t rc = write(event->signal_handle, &value, sizeof(value));
        if(rc == (ssize_t)sizeof(value)) return NS_OK;
        if((rc < 0) && (errno == EINTR)) continue;
        if((rc < 0) && (errno == EAGAIN)) return NS_OK; /* 计数器已满 = 已处于 signaled */
        ns_merrln(PLATFORM, "event_signal write failed: %s", strerror(errno));
        return NS_E_INVAL;
    }
}

int ns_platform_event_drain(ns_platform_event_t *event)
{
    if(event == NULL) return NS_E_INVAL;
    return ns_linux_event_drain(event);
}

int ns_platform_event_deinit(ns_platform_event_t *event)
{
    int rc;

    if(event == NULL) return NS_E_INVAL;
    if(event->waitable.registered_waitset != NULL) return NS_E_BUSY;
    if(event->signal_handle < 0) return NS_OK;

    do{
        rc = close(event->signal_handle);
    }while((rc < 0) && (errno == EINTR));

    event->signal_handle = -1;
    event->waitable.primitive = -1;
    return (rc == 0) ? NS_OK : NS_E_INVAL;
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

static void *ns_linux_thread_main(void *arg)
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
        rc = pthread_create(&thread->thread, NULL, ns_linux_thread_main, thread);
        if(rc != 0){
            return (rc == EAGAIN) ? NS_E_NOMEM : NS_E_INVAL;
        }
        thread->created = 1;
        return NS_OK;
    }

    /* 下界用**原始请求**判定：先取整再判下界会让"略小于 PTHREAD_STACK_MIN"
       的请求被补到下限而获准，违反"min - 1 必被拒"的契约。
       PTHREAD_STACK_MIN 在 glibc >= 2.34 是运行期值（sysconf(_SC_THREAD_STACK_MIN)），
       不是常量表达式，故不做静态断言。 */
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

    rc = pthread_create(&thread->thread, &attr, ns_linux_thread_main, thread);
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

#define NS_WAITSET_MAX_ENTRIES 64u
#define NS_WAITSET_TIMER_SENTINEL ((void *)(intptr_t)0x1)

/* Linux：epoll data.ptr 直接指向 caller 的 waitable，不需要内部存储。
   结构体定义见 platform/linux/nanosig_platform_port.h。 */

int ns_platform_waitset_init(ns_platform_waitset_t *waitset)
{
    int epfd;
    int tfd;

    if(waitset == NULL) return NS_E_INVAL;

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if(epfd < 0){
        ns_merrln(PLATFORM, "epoll_create1 failed: %s", strerror(errno));
        return NS_E_NOMEM;
    }

    tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(tfd < 0){
        ns_merrln(PLATFORM, "timerfd_create failed: %s", strerror(errno));
        close(epfd);
        return NS_E_NOMEM;
    }

    /* timerfd 永久加入 epoll，data.ptr 用 sentinel 区分 */
    {
        struct epoll_event tev;
        tev.events = EPOLLIN;
        tev.data.ptr = NS_WAITSET_TIMER_SENTINEL;
        if(epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &tev) < 0){
            ns_merrln(PLATFORM, "epoll_ctl ADD timerfd failed: %s", strerror(errno));
            close(tfd);
            close(epfd);
            return NS_E_NOMEM;
        }
    }

    waitset->epoll_fd = epfd;
    waitset->timer_fd = tfd;
    waitset->count = 0u;
    return NS_OK;
}

int ns_platform_waitset_deinit(ns_platform_waitset_t *waitset)
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

    if((waitset == NULL) || (waitable == NULL) || (waitable->primitive < 0)) return NS_E_INVAL;
    if(waitable->registered_waitset != NULL) return NS_E_EXISTS;
    if(waitset->count >= NS_WAITSET_MAX_ENTRIES) return NS_E_TOO_MANY_HANDLES;

    ev.events = ns_waitset_to_epoll_events(waitable->events);
    if(waitable->edge_triggered) ev.events |= EPOLLET;
    ev.data.ptr = (void *)waitable; /* 零拷贝：直接引用 caller 的 waitable */

    if(epoll_ctl(waitset->epoll_fd, EPOLL_CTL_ADD, waitable->primitive, &ev) < 0){
        if(errno == EEXIST) return NS_E_EXISTS;
        ns_merrln(PLATFORM, "epoll_ctl ADD fd=%d failed: %s", waitable->primitive, strerror(errno));
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
    if((waitset == NULL) || (waitable == NULL) || (waitable->primitive < 0)) return NS_E_INVAL;
    if(waitable->registered_waitset != waitset) return NS_E_INVAL;

    if(epoll_ctl(waitset->epoll_fd, EPOLL_CTL_DEL, waitable->primitive, NULL) < 0){
        ns_merrln(PLATFORM, "epoll_ctl DEL fd=%d failed: %s", waitable->primitive, strerror(errno));
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
 * 每次 wait 前 arm；epoll_wait 返回后立即 disarm——若在定时器到期前就因某个 fd
 * 就绪（或 EINTR）返回而不解散，遗留的内核定时器会在后续无限的 wait 期间自己
 * fire，凭空产生一次 out_count == 0 的虚假唤醒。it_value 全 0 的 settime 同时
 * 清空过期计数器，因此正常路径不需要 read。
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

    /* 空 waitset 没有可唤醒的事件源，无限等待会永久睡死；封顶为有界等待。 */
    if((waitset->count == 0u) && (timeout_us == NS_PLATFORM_WAIT_INFINITE_US)){
        timeout_us = NS_PLATFORM_WAIT_EMPTY_MAX_US;
    }

    if(timeout_us != 0u && timeout_us != NS_PLATFORM_WAIT_INFINITE_US){
        struct itimerspec its;
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = (time_t)(timeout_us / 1000000u);
        its.it_value.tv_nsec = (long)((timeout_us % 1000000u) * 1000u);
        if(timerfd_settime(waitset->timer_fd, 0, &its, NULL) < 0){
            ns_merrln(PLATFORM, "timerfd_settime failed: %s", strerror(errno));
            return NS_E_INVAL;
        }
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

        /* epoll_wait 已返回（含 EINTR / 出错），立即解散定时器：否则提前返回时
           遗留的内核定时器会在后续无限等待期间 fire，产生一次 0 耗时虚假唤醒。
           it_value 全 0 同时清空过期计数器，正常路径无需 read。 */
        if(timer_armed){
            struct itimerspec off;
            off.it_interval.tv_sec = 0;
            off.it_interval.tv_nsec = 0;
            off.it_value.tv_sec = 0;
            off.it_value.tv_nsec = 0;
            if(timerfd_settime(waitset->timer_fd, 0, &off, NULL) < 0){
                /* 解散失败时 ticks 可能仍非零，电平触发的 timerfd 会持续就绪；
                   兜底读走计数，避免后续等待变成热自旋。 */
                uint64_t dummy;
                ssize_t rd;
                ns_merrln(PLATFORM, "timerfd_settime disarm failed: %s", strerror(errno));
                do{
                    rd = read(waitset->timer_fd, &dummy, sizeof(dummy));
                }while((rd < 0) && (errno == EINTR));
            }
        }

        if(nfds < 0){
            if(errno == EINTR) return NS_OK;
            ns_merrln(PLATFORM, "epoll_wait failed: %s", strerror(errno));
            return NS_E_INVAL;
        }

        for(i = 0; i < nfds && (size_t)*out_count < max_completions; i++){
            ns_platform_waitable_t *wp = (ns_platform_waitable_t *)events[i].data.ptr;

            /* timerfd sentinel → 不报告 completion。不能就此返回：同一次 epoll_wait
               里排在后面的 fd 也已就绪，边沿触发的边沿已被本次调用消费，丢弃即
               永久丢失。继续循环，末尾统一返回 NS_OK。 */
            if(wp == NS_WAITSET_TIMER_SENTINEL) continue;

            completions[*out_count].waitable = wp;
            completions[*out_count].triggered_events = ns_waitset_from_epoll_events(events[i].events);
            (*out_count)++;
        }
    }

    return NS_OK;
}
