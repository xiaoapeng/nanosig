/**
 * @file test_helpers.h
 * @brief Shared platform-specific test helpers for raw waitable primitives.
 *
 * Provides test_create_raw_waitable / test_destroy_raw_waitable /
 * test_signal_raw_waitable used by multiple test translation units.
 */

#ifndef NANOSIG_TEST_HELPERS_H
#define NANOSIG_TEST_HELPERS_H

#include <nanosig/nanosig_port.h>

#if defined(__GNUC__)
#define NS_TEST_MAYBE_UNUSED __attribute__((unused))
#else
#define NS_TEST_MAYBE_UNUSED
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <errno.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/*  macOS pipe-backed raw waitable registry                            */
/*                                                                     */
/*  macOS has no eventfd. The closest cross-platform equivalent of the */
/*  Linux raw waitable is a non-blocking pipe: the read end is the     */
/*  waitable fd (registered with EVFILT_READ and drained with read()), */
/*  matching the read()-based drain used by consume_fn/slot tests.     */
/*  The write end is kept in a small per-translation-unit registry so  */
/*  test_signal_raw_waitable can trigger it from the waitable alone.   */
/* ------------------------------------------------------------------ */

#ifdef __APPLE__
#define NS_TEST_MACOS_RAW_MAX 64

struct ns_test_macos_raw_pair {
    int read_fd;
    int write_fd;
};

static struct ns_test_macos_raw_pair g_test_macos_raw_pairs[NS_TEST_MACOS_RAW_MAX];
static int g_test_macos_raw_pairs_ready = 0;

static NS_TEST_MAYBE_UNUSED void test_macos_raw_registry_init(void)
{
    int i;

    if(g_test_macos_raw_pairs_ready != 0) return;
    for(i = 0; i < NS_TEST_MACOS_RAW_MAX; ++i){
        g_test_macos_raw_pairs[i].read_fd = -1;
        g_test_macos_raw_pairs[i].write_fd = -1;
    }
    g_test_macos_raw_pairs_ready = 1;
}

static NS_TEST_MAYBE_UNUSED void test_macos_raw_registry_add(int read_fd, int write_fd)
{
    int i;

    test_macos_raw_registry_init();
    for(i = 0; i < NS_TEST_MACOS_RAW_MAX; ++i){
        if(g_test_macos_raw_pairs[i].read_fd < 0){
            g_test_macos_raw_pairs[i].read_fd = read_fd;
            g_test_macos_raw_pairs[i].write_fd = write_fd;
            return;
        }
    }
}

static NS_TEST_MAYBE_UNUSED int test_macos_raw_registry_write_fd(int read_fd)
{
    int i;

    test_macos_raw_registry_init();
    for(i = 0; i < NS_TEST_MACOS_RAW_MAX; ++i){
        if(g_test_macos_raw_pairs[i].read_fd == read_fd){
            return g_test_macos_raw_pairs[i].write_fd;
        }
    }
    return -1;
}

static NS_TEST_MAYBE_UNUSED int test_macos_raw_registry_take_write_fd(int read_fd)
{
    int i;

    test_macos_raw_registry_init();
    for(i = 0; i < NS_TEST_MACOS_RAW_MAX; ++i){
        if(g_test_macos_raw_pairs[i].read_fd == read_fd){
            int write_fd = g_test_macos_raw_pairs[i].write_fd;

            g_test_macos_raw_pairs[i].read_fd = -1;
            g_test_macos_raw_pairs[i].write_fd = -1;
            return write_fd;
        }
    }
    return -1;
}

static NS_TEST_MAYBE_UNUSED int test_macos_set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD, 0);

    if(flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static NS_TEST_MAYBE_UNUSED int test_macos_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if(flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static NS_TEST_MAYBE_UNUSED int test_create_macos_raw_pipe(void)
{
    int fds[2];

    if(pipe(fds) < 0) return -1;

    if((test_macos_set_cloexec(fds[0]) < 0) || (test_macos_set_cloexec(fds[1]) < 0)
        || (test_macos_set_nonblock(fds[0]) < 0) || (test_macos_set_nonblock(fds[1]) < 0)){
        (void)close(fds[0]);
        (void)close(fds[1]);
        return -1;
    }

    test_macos_raw_registry_add(fds[0], fds[1]);
    return fds[0];
}
#endif /* __APPLE__ */

/* ------------------------------------------------------------------ */
/*  Raw waitable helpers                                               */
/* ------------------------------------------------------------------ */

static NS_TEST_MAYBE_UNUSED ns_platform_waitable_t test_create_raw_waitable(void)
{
    ns_platform_waitable_t w;

    ns_waitable_init(&w);
#ifdef _WIN32
    w.primitive.handle = CreateEventA(NULL, FALSE, FALSE, NULL);
#elif defined(__APPLE__)
    w.primitive.fd = test_create_macos_raw_pipe();
#else
    w.primitive.fd = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
#endif
    w.events = NS_WAITABLE_EVENT_IN;
    return w;
}

static NS_TEST_MAYBE_UNUSED void test_destroy_raw_waitable(ns_platform_waitable_t w)
{
#ifdef _WIN32
    if(w.primitive.handle != NULL) CloseHandle((HANDLE)w.primitive.handle);
#elif defined(__APPLE__)
    if(w.primitive.fd >= 0){
        int write_fd = test_macos_raw_registry_take_write_fd(w.primitive.fd);

        (void)close(w.primitive.fd);
        if(write_fd >= 0) (void)close(write_fd);
    }
#else
    if(w.primitive.fd >= 0) close(w.primitive.fd);
#endif
}

static NS_TEST_MAYBE_UNUSED void test_signal_raw_waitable(ns_platform_waitable_t w)
{
#ifdef _WIN32
    (void)SetEvent((HANDLE)w.primitive.handle);
#elif defined(__APPLE__)
    {
        int write_fd = test_macos_raw_registry_write_fd(w.primitive.fd);
        uint64_t val = 1u;
        ssize_t n;

        if(write_fd < 0) return;
        do {
            n = write(write_fd, &val, sizeof(val));
        } while(n < 0 && errno == EINTR);
        (void)n;
    }
#else
    {
        uint64_t val = 1u;
        ssize_t n;

        do {
            n = write(w.primitive.fd, &val, sizeof(val));
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
    return w.primitive.handle != NULL;
#else
    return w.primitive.fd >= 0;
#endif
}

#endif /* NANOSIG_TEST_HELPERS_H */
