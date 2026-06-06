/**
 * @file port.c
 * @brief nanosig Linux loop-only platform backend.
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#define _POSIX_C_SOURCE 200809L

#include "platform/port.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

struct ns_platform_tls_key {
    pthread_key_t key;
};

struct ns_platform_wakeup {
    int fd;
};

struct ns_platform_mutex {
    pthread_mutex_t mutex;
};

static int ns_linux_timeout_ms(ns_platform_time_us_t timeout_us)
{
    uint64_t timeout_ms;

    if(timeout_us == NS_PLATFORM_WAIT_INFINITE_US) return -1;

    timeout_ms = (timeout_us + 999u) / 1000u;
    if(timeout_ms > (uint64_t)INT32_MAX) return INT32_MAX;

    return (int)timeout_ms;
}

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

int ns_platform_tls_key_create(ns_platform_tls_key_t **out_key)
{
    ns_platform_tls_key_t *key;

    if(out_key == NULL) return NS_E_INVAL;

    *out_key = NULL;
    key = (ns_platform_tls_key_t *)ns_platform_alloc(sizeof(*key));
    if(key == NULL) return NS_E_NOMEM;

    if(pthread_key_create(&key->key, NULL) != 0){
        ns_platform_free(key);
        return NS_E_NOMEM;
    }

    *out_key = key;
    return NS_OK;
}

int ns_platform_tls_key_destroy(ns_platform_tls_key_t *key)
{
    if(key == NULL) return NS_E_INVAL;

    if(pthread_key_delete(key->key) != 0){
        ns_platform_free(key);
        return NS_E_INVAL;
    }

    ns_platform_free(key);
    return NS_OK;
}

int ns_platform_tls_get(ns_platform_tls_key_t *key, void **out_value)
{
    if((key == NULL) || (out_value == NULL)) return NS_E_INVAL;

    *out_value = pthread_getspecific(key->key);
    return NS_OK;
}

int ns_platform_tls_set(ns_platform_tls_key_t *key, void *value)
{
    if(key == NULL) return NS_E_INVAL;
    if(pthread_setspecific(key->key, value) != 0) return NS_E_INVAL;

    return NS_OK;
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

int ns_platform_wakeup_wait(
    ns_platform_wakeup_t *wakeup,
    ns_platform_time_us_t timeout_us,
    ns_platform_wait_result_t *out_result)
{
    struct pollfd pfd;

    if((wakeup == NULL) || (out_result == NULL)) return NS_E_INVAL;

    pfd.fd = wakeup->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    for(;;){
        int rc = poll(&pfd, 1u, ns_linux_timeout_ms(timeout_us));
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
