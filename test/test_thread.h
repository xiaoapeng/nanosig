/**
 * @file test_thread.h
 * @brief Cross-platform test thread abstraction for nanosig tests.
 * @date 2026-06-28
 *
 * Replaces 6+ duplicated broker_loop_ctx_t / cross_thread_ctx_t / loop_thread_ctx_t
 * variants previously scattered across test_broker.c, test_signal.c, test_loop.c,
 * test_mpsc_record_ring.c, and integration scenarios.
 *
 * Header-only by design: every translation unit that includes this gets its own
 * static copy of the OS adapter. Tests don't care about binary size.
 *
 * Usage:
 *   static int my_worker(void *arg) { ... }
 *   test_thread_t t;
 *   test_thread_init(&t, my_worker, &my_state);
 *   EXPECT_OK(test_thread_start(&t));
 *   EXPECT_OK(test_thread_wait_ready(&t));
 *   ... exercise from caller thread ...
 *   test_thread_join(&t);
 */

#ifndef NANOSIG_TEST_THREAD_H
#define NANOSIG_TEST_THREAD_H

#include <stdatomic.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test thread handle.
 *
 * The entry function runs on a worker thread. It is expected to:
 *   1. Initialize the resource under test (e.g. ns_loop_init).
 *   2. Store the resource in @c arg (or in @c state if caller allocated).
 *   3. Set @c ready to 1 via test_thread_signal_ready().
 *   4. Run until told to stop (e.g. ns_loop_run blocks until quit).
 *   5. Tear down and return.
 *
 * Return value from entry is captured into @c rc for inspection after join.
 */
typedef struct test_thread {
#if defined(_WIN32)
    HANDLE handle;
#else
    pthread_t tid;
#endif
    int (*entry)(void *arg);
    void *arg;
    atomic_int ready;
    atomic_int failed;
    int rc;
} test_thread_t;

/* ------------------------------------------------------------- */
/*  Public API                                                   */
/* ------------------------------------------------------------- */

/**
 * @brief Initialize a test_thread_t. Must be called exactly once before start.
 */
static inline void test_thread_init(test_thread_t *t, int (*entry)(void *), void *arg)
{
    t->entry = entry;
    t->arg = arg;
    atomic_init(&t->ready, 0);
    atomic_init(&t->failed, 0);
    t->rc = 0;
#if defined(_WIN32)
    t->handle = NULL;
#else
    t->tid = (pthread_t)0;
#endif
}

/**
 * @brief Signal from worker thread that setup is complete and main thread may proceed.
 *
 * Convenience for the most common case: set ready=1 and failed=0. Tests that need
 * to publish an error code from setup should manipulate @c ready/@c failed directly.
 */
static inline void test_thread_signal_ready(test_thread_t *t)
{
    atomic_store_explicit(&t->ready, 1, memory_order_release);
}

/**
 * @brief Signal from worker thread that setup failed with the given rc.
 */
static inline void test_thread_signal_failed(test_thread_t *t, int rc)
{
    t->rc = rc;
    atomic_store_explicit(&t->failed, 1, memory_order_release);
    atomic_store_explicit(&t->ready, 1, memory_order_release);
}

#if defined(_WIN32)
static DWORD WINAPI test_thread_trampoline(LPVOID param)
{
    test_thread_t *t = (test_thread_t *)param;
    t->rc = t->entry(t->arg);
    return (DWORD)t->rc;
}
#else
static void *test_thread_trampoline(void *param)
{
    test_thread_t *t = (test_thread_t *)param;
    t->rc = t->entry(t->arg);
    return NULL;
}
#endif

/**
 * @brief Start the worker thread. Returns NS_OK or NS_E_NOMEM.
 */
static inline int test_thread_start(test_thread_t *t)
{
#if defined(_WIN32)
    t->handle = CreateThread(NULL, 0u, test_thread_trampoline, t, 0u, NULL);
    return (t->handle != NULL) ? 0 : -1; /* treat both as NS_OK / NS_E_NOMEM */
#else
    return (pthread_create(&t->tid, NULL, test_thread_trampoline, t) == 0) ? 0 : -1;
#endif
}

/**
 * @brief Block until worker signals ready (or fails). 1M-iter spin with yield.
 *
 * Returns 0 on success, -1 on timeout. Tests wrap with EXPECT_OK.
 */
static inline int test_thread_wait_ready(test_thread_t *t)
{
    int i;
    for(i = 0; i < 1000000; ++i){
        if(atomic_load_explicit(&t->ready, memory_order_acquire) != 0){
            return (atomic_load_explicit(&t->failed, memory_order_acquire) != 0) ? -2 : 0;
        }
#if defined(_WIN32)
        SwitchToThread();
#else
        sched_yield();
#endif
    }
    return -1;
}

/**
 * @brief Join worker thread and release OS resources. Idempotent.
 */
static inline void test_thread_join(test_thread_t *t)
{
#if defined(_WIN32)
    if(t->handle != NULL){
        (void)WaitForSingleObject(t->handle, INFINITE);
        (void)CloseHandle(t->handle);
        t->handle = NULL;
    }
#else
    if((int)1){ /* always joinable; pthread_t(0) on Linux is "unset" but join() works */
        (void)pthread_join(t->tid, NULL);
    }
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_TEST_THREAD_H */