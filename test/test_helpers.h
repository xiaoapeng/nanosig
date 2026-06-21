/**
 * @file test_helpers.h
 * @brief Shared platform-specific test helpers for raw waitable primitives.
 *
 * Provides test_create_raw_waitable / test_destroy_raw_waitable /
 * test_signal_raw_waitable used by multiple test translation units.
 */

#ifndef NANOSIG_TEST_HELPERS_H
#define NANOSIG_TEST_HELPERS_H

#include "platform/port.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <errno.h>
#include <fcntl.h>
#include <sys/event.h>
#include <unistd.h>
#else
#include <sys/eventfd.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/*  macOS kqueue EVFILT_USER helpers                                   */
/* ------------------------------------------------------------------ */

#ifdef __APPLE__
#define NS_TEST_MACOS_USER_EVENT_IDENT ((uintptr_t)1u)

static int test_macos_set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD, 0);

    if(flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int test_create_macos_user_event(void)
{
    struct kevent kev;
    int kq = kqueue();

    if(kq < 0) return -1;
    if(test_macos_set_cloexec(kq) < 0){
        (void)close(kq);
        return -1;
    }

    EV_SET(&kev, NS_TEST_MACOS_USER_EVENT_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0u, 0, NULL);
    if(kevent(kq, &kev, 1, NULL, 0, NULL) < 0){
        (void)close(kq);
        return -1;
    }

    return kq;
}

static int test_signal_macos_user_event(int kq)
{
    struct kevent kev;

    EV_SET(&kev, NS_TEST_MACOS_USER_EVENT_IDENT, EVFILT_USER, 0u, NOTE_TRIGGER, 0, NULL);

    for(;;){
        if(kevent(kq, &kev, 1, NULL, 0, NULL) == 0) return 0;
        if(errno == EINTR) continue;
        return -1;
    }
}
#endif /* __APPLE__ */

/* ------------------------------------------------------------------ */
/*  Raw waitable helpers                                               */
/* ------------------------------------------------------------------ */

static ns_platform_waitable_t test_create_raw_waitable(void)
{
    ns_platform_waitable_t w;

    ns_waitable_init(&w);
#ifdef _WIN32
    w.handle = CreateEventA(NULL, FALSE, FALSE, NULL);
#elif defined(__APPLE__)
    w.fd = test_create_macos_user_event();
#else
    w.fd = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
#endif
    w.events = NS_WAITABLE_EVENT_IN;
    return w;
}

static void test_destroy_raw_waitable(ns_platform_waitable_t w)
{
#ifdef _WIN32
    if(w.handle != NULL) CloseHandle((HANDLE)w.handle);
#else
    if(w.fd >= 0) close(w.fd);
#endif
}

static void test_signal_raw_waitable(ns_platform_waitable_t w)
{
#ifdef _WIN32
    (void)SetEvent((HANDLE)w.handle);
#elif defined(__APPLE__)
    (void)test_signal_macos_user_event(w.fd);
#else
    {
        uint64_t val = 1u;
        ssize_t n;
        do {
            n = write(w.fd, &val, sizeof(val));
        } while(n < 0 && errno == EINTR);
        (void)n;
    }
#endif
}

/**
 * @brief Check whether a raw waitable was created successfully.
 * @return 0 if invalid, non-zero if valid.
 */
static inline int test_raw_waitable_is_valid(ns_platform_waitable_t w)
{
#if defined(_WIN32)
    return w.handle != NULL;
#else
    return w.fd >= 0;
#endif
}

#endif /* NANOSIG_TEST_HELPERS_H */
