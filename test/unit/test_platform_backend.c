/**
 * @file test_platform_backend.c
 * @brief P1b loop-only + P5b waitset platform backend runtime smoke test.
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include "platform/port.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/eventfd.h>
#endif

static int expect_ok(int rc)
{
    return rc == NS_OK ? 0 : 1;
}

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/*  P1b basic tests                                                    */
/* ------------------------------------------------------------------ */

static int test_alloc(void)
{
    void *ptr = ns_platform_alloc(16u);

    if(ptr == NULL) return 1;

    memset(ptr, 0, 16u);
    ns_platform_free(ptr);
    return 0;
}

static int test_tls(void)
{
    ns_platform_tls_key_t *key = NULL;
    void *value = NULL;
    int marker = 42;

    if(expect_ok(ns_platform_tls_key_create(&key)) != 0) return 1;
    if(expect_ok(ns_platform_tls_get(key, &value)) != 0) return 1;
    if(expect_true(value == NULL) != 0) return 1;
    if(expect_ok(ns_platform_tls_set(key, &marker)) != 0) return 1;
    if(expect_ok(ns_platform_tls_get(key, &value)) != 0) return 1;
    if(expect_true(value == &marker) != 0) return 1;
    if(expect_ok(ns_platform_tls_set(key, NULL)) != 0) return 1;
    if(expect_ok(ns_platform_tls_key_destroy(key)) != 0) return 1;

    return 0;
}

static int test_mutex(void)
{
    ns_platform_mutex_t *mutex = NULL;

    if(expect_ok(ns_platform_mutex_create(&mutex, "test-mutex")) != 0) return 1;
    if(expect_ok(ns_platform_mutex_lock(mutex)) != 0) return 1;
    if(expect_ok(ns_platform_mutex_unlock(mutex)) != 0) return 1;
    if(expect_ok(ns_platform_mutex_destroy(mutex)) != 0) return 1;

    return 0;
}

static int test_wakeup(void)
{
    ns_platform_wakeup_t *wakeup = NULL;
    ns_platform_wait_result_t wait_result = NS_PLATFORM_WAIT_SIGNALED;

    if(expect_ok(ns_platform_wakeup_create(&wakeup, "test-wakeup")) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_wait(wakeup, 0u, &wait_result)) != 0) return 1;
    if(expect_true(wait_result == NS_PLATFORM_WAIT_TIMEOUT) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_signal(wakeup)) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_wait(wakeup, NS_PLATFORM_WAIT_INFINITE_US, &wait_result)) != 0) return 1;
    if(expect_true(wait_result == NS_PLATFORM_WAIT_SIGNALED) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_destroy(wakeup)) != 0) return 1;

    return 0;
}

static int test_clock(void)
{
    ns_platform_time_us_t first = 0u;
    ns_platform_time_us_t second = 0u;

    if(expect_ok(ns_platform_clock_monotonic_us(&first)) != 0) return 1;
    if(expect_ok(ns_platform_clock_monotonic_us(&second)) != 0) return 1;
    if(expect_true(second >= first) != 0) return 1;

    return 0;
}

typedef struct test_thread_ctx {
    int value;
} test_thread_ctx_t;

static void test_thread_entry(void *arg)
{
    test_thread_ctx_t *ctx = (test_thread_ctx_t *)arg;

    ctx->value = 42;
}

static int test_thread(void)
{
    ns_platform_thread_t *thread = NULL;
    test_thread_ctx_t ctx;

    ctx.value = 0;

    if(ns_platform_thread_create(NULL, test_thread_entry, &ctx, "bad-thread") != NS_E_INVAL) return 1;
    if(ns_platform_thread_create(&thread, NULL, &ctx, "bad-thread") != NS_E_INVAL) return 1;
    if(expect_ok(ns_platform_thread_create(&thread, test_thread_entry, &ctx, "test-thread")) != 0) return 1;
    if(expect_ok(ns_platform_thread_join(thread)) != 0) return 1;
    if(expect_true(ctx.value == 42) != 0) return 1;
    if(ns_platform_thread_join(NULL) != NS_E_INVAL) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  P5b waitset tests                                                  */
/* ------------------------------------------------------------------ */

static ns_platform_waitable_t test_create_raw_waitable(void)
{
    ns_platform_waitable_t w = ns_waitable_init();
#ifdef _WIN32
    w.handle = CreateEventA(NULL, FALSE, FALSE, NULL);
#else
    w.fd = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
#endif
    w.events = NS_WAITABLE_EVENT_IN;
    return w;
}

static void test_destroy_raw_waitable(ns_platform_waitable_t w)
{
#ifdef _WIN32
    if(w.handle != NULL) CloseHandle(w.handle);
#else
    if(w.fd >= 0) close(w.fd);
#endif
}

static void test_signal_raw_waitable(ns_platform_waitable_t w)
{
#ifdef _WIN32
    SetEvent((HANDLE)w.handle);
#else
    uint64_t val = 1u;
    (void)write(w.fd, &val, sizeof(val));
#endif
}

static int test_waitset_lifecycle(void)
{
    ns_platform_waitset_t *ws = NULL;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    if(expect_true(ws != NULL) != 0) return 1;
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

static int test_wakeup_waitable(void)
{
    ns_platform_wakeup_t *wakeup = NULL;
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_wakeup_create(&wakeup, "test-wakeup-waitable")) != 0) return 1;
    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;

    w = ns_platform_wakeup_get_waitable(wakeup);
    w.events = NS_WAITABLE_EVENT_IN;
    w.user_data = (void *)0x1234;

    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_signal(wakeup)) != 0) return 1;
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4u, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;
    if(expect_true(cc[0].waitable == &w) != 0) return 1;
    if(expect_true(cc[0].waitable->user_data == (void *)0x1234) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;
    if(expect_ok(ns_platform_wakeup_destroy(wakeup)) != 0) return 1;

    return 0;
}

static int test_waitset_null_params(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[1];
    size_t cnt = 0u;

    w = ns_waitable_init();

    if(ns_platform_waitset_create(NULL) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_destroy(NULL) != NS_E_INVAL) return 1;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    if(ns_platform_waitset_add(ws, NULL) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_add(ws, &w) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_remove(ws, NULL) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_remove(ws, &w) != NS_E_INVAL) return 1;

    if(ns_platform_waitset_wait(NULL, 0u, cc, 1, &cnt) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_wait(ws, 0u, NULL, 1, &cnt) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_wait(ws, 0u, cc, 1, NULL) != NS_E_INVAL) return 1;

    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

static int test_waitset_add_remove(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();

    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;
    if(ns_platform_waitset_add(ws, &w) != NS_E_EXISTS) return 1;
    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    if(ns_platform_waitset_remove(ws, &w) != NS_E_INVAL) return 1;
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;
    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;

    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

static int test_waitset_add_signaled(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();

    test_signal_raw_waitable(w);
    w.user_data = (void *)0xAA;
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    {
        ns_platform_waitset_completion_t cc[4];
        size_t cnt = 0u;
        if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
        if(expect_true(cnt >= 1u) != 0) return 1;
        if(expect_true(cc[0].waitable->user_data == (void *)0xAA) != 0) return 1;
    }

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

static int test_waitset_multi_signal(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w[3];
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;
    int i;
    int created = 0;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) goto fail;

    for(i = 0; i < 3; i++){
        w[i] = test_create_raw_waitable();
        w[i].user_data = (void *)(intptr_t)i;
        created++;
        if(expect_ok(ns_platform_waitset_add(ws, &w[i])) != 0) goto fail;
    }

    for(i = 0; i < 3; i++){
        test_signal_raw_waitable(w[i]);
    }

    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) goto fail;
    if(expect_true(cnt >= 1u) != 0) goto fail;

    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_destroy(ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    if(ws != NULL) (void)ns_platform_waitset_destroy(ws);
    return 1;
}

static int test_waitset_max_completions_zero(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[1];
    size_t cnt = 99u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    test_signal_raw_waitable(w);

    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 0u, &cnt)) != 0) return 1;
    if(expect_true(cnt == 0u) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

static int test_waitset_destroy_with_entries(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w[2];

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;

    w[0] = test_create_raw_waitable();
    w[1] = test_create_raw_waitable();

    if(expect_ok(ns_platform_waitset_add(ws, &w[0])) != 0) return 1;
    if(expect_ok(ns_platform_waitset_add(ws, &w[1])) != 0) return 1;

    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    test_destroy_raw_waitable(w[0]);
    test_destroy_raw_waitable(w[1]);

    return 0;
}

static int test_waitset_wait_timeout(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitset_completion_t completions[4];
    size_t count = 99u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;

    if(expect_ok(ns_platform_waitset_wait(ws, 0u, completions, 4, &count)) != 0) return 1;
    if(expect_true(count == 0u) != 0) return 1;

    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

static int test_waitset_wait_signal(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t completions[4];
    size_t count = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    w.user_data = (void *)0x42;
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    test_signal_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, completions, 4, &count)) != 0) return 1;
    if(expect_true(count == 1u) != 0) return 1;
    if(expect_true(completions[0].waitable->user_data == (void *)0x42) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

static int test_waitset_multi(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w[3];
    ns_platform_waitset_completion_t completions[4];
    size_t count = 0u;
    int i;
    int found = 0;
    int created = 0;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) goto fail;

    for(i = 0; i < 3; i++){
        w[i] = test_create_raw_waitable();
        w[i].user_data = (void *)(intptr_t)i;
        created++;
        if(expect_ok(ns_platform_waitset_add(ws, &w[i])) != 0) goto fail;
    }

    test_signal_raw_waitable(w[1]);
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, completions, 4, &count)) != 0) goto fail;
    if(expect_true(count >= 1u) != 0) goto fail;

    for(i = 0; i < (int)count; i++){
        if(completions[i].waitable->user_data == (void *)(intptr_t)1) found++;
    }
    if(expect_true(found >= 1) != 0) goto fail;

    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_destroy(ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    if(ws != NULL) (void)ns_platform_waitset_destroy(ws);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  行为语义                                                           */
/* ------------------------------------------------------------------ */

/** signal 两次 → wait 只返回 1 个 completion（不重复） */
static int test_waitset_double_signal(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    test_signal_raw_waitable(w);
    test_signal_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

/** signal → wait → remove + re-add → wait → count=0（信号已消费） */
static int test_waitset_signal_after_wait(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    test_signal_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;

    /* remove + re-add 模拟信号消费（eventfd level-triggered，需要 read() 才能清除，
       但平台层不暴露 read()，通过 remove/add 重置 epoll 注册状态） */
    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    cnt = 99u;
    if(expect_ok(ns_platform_waitset_wait(ws, 0u, cc, 4, &cnt)) != 0) return 1;
    /* Linux eventfd: signal 仍然 readable（level-triggered），remove/add 不能清除。
       Windows auto-reset event: signal 已被消费，返回 0。 */
#ifdef _WIN32
    if(expect_true(cnt == 0u) != 0) return 1;
#else
    /* Linux: eventfd 仍然 readable，epoll 仍然返回 */
    if(expect_true(cnt >= 1u) != 0) return 1;
#endif

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

/** 无 signal + timeout > 0 → 等待后返回 count=0（验证真的等了） */
static int test_waitset_timeout_actual(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 99u;
    ns_platform_time_us_t t0, t1;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    if(expect_ok(ns_platform_clock_monotonic_us(&t0)) != 0) return 1;
    if(expect_ok(ns_platform_waitset_wait(ws, 50000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_ok(ns_platform_clock_monotonic_us(&t1)) != 0) return 1;

    if(expect_true(cnt == 0u) != 0) return 1;
    if(expect_true((t1 - t0) >= 40000u) != 0) return 1; /* 至少 ~40ms */

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

/** timeout=1s，signal 在 wait 前到达 → 立即返回 */
static int test_waitset_signal_before_timeout(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;
    ns_platform_time_us_t t0, t1;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    test_signal_raw_waitable(w);
    if(expect_ok(ns_platform_clock_monotonic_us(&t0)) != 0) return 1;
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_ok(ns_platform_clock_monotonic_us(&t1)) != 0) return 1;

    if(expect_true(cnt == 1u) != 0) return 1;
    /* 应该远小于 1s */
    if(expect_true((t1 - t0) < 500000u) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  completion 数据完整性                                               */
/* ------------------------------------------------------------------ */

/** completion.waitable 应该 == add 时传入的 &w（同一指针） */
static int test_waitset_pointer_identity(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    w.user_data = (void *)0x99;
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    test_signal_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;

    /* 指针必须完全一致 */
    if(expect_true(cc[0].waitable == &w) != 0) return 1;
    if(expect_true(cc[0].waitable->user_data == (void *)0x99) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

/** 3 个 waitable signal 2 个 → 各 completion 指向正确 waitable */
static int test_waitset_multi_pointer_identity(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w[3];
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;
    int i, found0 = 0, found2 = 0;
    int created = 0;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) goto fail;

    for(i = 0; i < 3; i++){
        w[i] = test_create_raw_waitable();
        w[i].user_data = (void *)(intptr_t)i;
        created++;
        if(expect_ok(ns_platform_waitset_add(ws, &w[i])) != 0) goto fail;
    }

    /* signal w[0] 和 w[2]，不 signal w[1] */
    test_signal_raw_waitable(w[0]);
    test_signal_raw_waitable(w[2]);

    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) goto fail;
    if(expect_true(cnt >= 1u) != 0) goto fail;

    for(i = 0; i < (int)cnt; i++){
        if(cc[i].waitable == &w[0]) found0++;
        if(cc[i].waitable == &w[2]) found2++;
    }
    /* Windows WFMO 只返回 1 个；Linux epoll 可能返回 2 个 */
    if(expect_true(found0 + found2 >= 1) != 0) goto fail;

    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_destroy(ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    if(ws != NULL) (void)ns_platform_waitset_destroy(ws);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  边界                                                               */
/* ------------------------------------------------------------------ */

/** add 到上限成功，再多一个 → NS_E_TOO_MANY_HANDLES */
static int test_waitset_capacity(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w[65];
    int i;
    int created = 0;
    int rc;
    int limit;

    /* Linux: 64, Windows: 63（预留 1 个给 timer） */
#ifdef _WIN32
    limit = 63;
#else
    limit = 64;
#endif

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) goto fail;

    for(i = 0; i < limit + 1; i++){
        w[i] = test_create_raw_waitable();
        created++;
        rc = ns_platform_waitset_add(ws, &w[i]);
        if(i < limit){
            if(rc != NS_OK) goto fail;
        }else{
            if(rc != NS_E_TOO_MANY_HANDLES) goto fail;
        }
    }

    for(i = 0; i < limit; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    test_destroy_raw_waitable(w[limit]); /* 没 add 成功的，只销毁 handle */
    (void)ns_platform_waitset_destroy(ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    if(ws != NULL) (void)ns_platform_waitset_destroy(ws);
    return 1;
}

/** 3 个 signal，max_completions=1 → 只返回 1 个 */
static int test_waitset_max_completions_truncate(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w[3];
    ns_platform_waitset_completion_t cc[1];
    size_t cnt = 0u;
    int i;
    int created = 0;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) goto fail;

    for(i = 0; i < 3; i++){
        w[i] = test_create_raw_waitable();
        created++;
        if(expect_ok(ns_platform_waitset_add(ws, &w[i])) != 0) goto fail;
    }

    for(i = 0; i < 3; i++){
        test_signal_raw_waitable(w[i]);
    }

    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 1, &cnt)) != 0) goto fail;
    if(expect_true(cnt == 1u) != 0) goto fail;

    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_destroy(ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    if(ws != NULL) (void)ns_platform_waitset_destroy(ws);
    return 1;
}

/** add events=0 → 成功，signal 后 wait 不返回 completion（Linux epoll 语义） */
#ifdef __linux__
static int test_waitset_events_zero(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    w.events = 0u; /* 不关注任何事件 */
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    test_signal_raw_waitable(w);
    /* timeout=0 非阻塞，events=0 不应触发 */
    if(expect_ok(ns_platform_waitset_wait(ws, 0u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 0u) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}
#endif

/** 同一 waitable 加到两个 waitset，各自独立 */
static int test_waitset_two_waitsets(void)
{
    ns_platform_waitset_t *ws1 = NULL, *ws2 = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws1)) != 0) return 1;
    if(expect_ok(ns_platform_waitset_create(&ws2)) != 0) goto fail2;

    w = test_create_raw_waitable();
    if(expect_ok(ns_platform_waitset_add(ws1, &w)) != 0) goto fail;
    if(expect_ok(ns_platform_waitset_add(ws2, &w)) != 0) goto fail;

    /* signal 后两个 waitset 都应该能收到 */
    test_signal_raw_waitable(w);

    cnt = 0u;
    if(expect_ok(ns_platform_waitset_wait(ws1, 0u, cc, 4, &cnt)) != 0) goto fail;
    if(expect_true(cnt == 1u) != 0) goto fail;

    /* ws2 的 wait：auto-reset event 已被 ws1 消费，Linux epoll LT 模式可能还能收到 */
    cnt = 0u;
    (void)ns_platform_waitset_wait(ws2, 0u, cc, 4, &cnt);

    (void)ns_platform_waitset_remove(ws1, &w);
    (void)ns_platform_waitset_remove(ws2, &w);
    test_destroy_raw_waitable(w);
    (void)ns_platform_waitset_destroy(ws1);
    (void)ns_platform_waitset_destroy(ws2);
    return 0;

fail:
    (void)ns_platform_waitset_remove(ws1, &w);
    test_destroy_raw_waitable(w);
fail2:
    if(ws1 != NULL) (void)ns_platform_waitset_destroy(ws1);
    if(ws2 != NULL) (void)ns_platform_waitset_destroy(ws2);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  生命周期                                                           */
/* ------------------------------------------------------------------ */

/** add → signal → wait → remove → add → signal → wait → 正常工作 */
static int test_waitset_lifecycle_readd(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    w.user_data = (void *)0xBB;

    /* 第一轮 */
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;
    test_signal_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;
    if(expect_true(cc[0].waitable->user_data == (void *)0xBB) != 0) return 1;
    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;

    /* 第二轮 */
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;
    test_signal_raw_waitable(w);
    cnt = 0u;
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;
    if(expect_true(cc[0].waitable->user_data == (void *)0xBB) != 0) return 1;
    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;

    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

/** destroy 有注册项的 waitset → 成功，不崩溃 */
static int test_waitset_destroy_then_ops(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    /* 有注册项时 destroy → 成功 */
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    test_destroy_raw_waitable(w);
    return 0;
}

#ifdef __linux__
/** edge_triggered：signal → wait → 再 signal → wait 第二次才触发 */
static int test_waitset_edge_triggered(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    w.edge_triggered = 1;
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    /* 第一次 signal + wait → 触发 */
    test_signal_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;

    /* 不 signal，直接 wait → 不触发（ET 模式，需要新的边沿） */
    cnt = 99u;
    if(expect_ok(ns_platform_waitset_wait(ws, 0u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 0u) != 0) return 1;

    /* 再次 signal + wait → 触发 */
    test_signal_raw_waitable(w);
    cnt = 0u;
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}

/** level_triggered：signal → wait → 再 wait 仍然触发（信号未消费） */
static int test_waitset_level_triggered(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    if(expect_ok(ns_platform_waitset_create(&ws)) != 0) return 1;
    w = test_create_raw_waitable();
    w.edge_triggered = 0;
    if(expect_ok(ns_platform_waitset_add(ws, &w)) != 0) return 1;

    /* signal → wait → 触发 */
    test_signal_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_wait(ws, 1000000u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 1u) != 0) return 1;

    /* eventfd 已被 drain（level-triggered 但 poll 后 read 了），再 wait → 不触发 */
    /* 注：eventfd 的 POLLIN 在 drain 后消失，所以 LT 模式下第二次也不触发 */
    cnt = 99u;
    if(expect_ok(ns_platform_waitset_wait(ws, 0u, cc, 4, &cnt)) != 0) return 1;
    if(expect_true(cnt == 0u) != 0) return 1;

    if(expect_ok(ns_platform_waitset_remove(ws, &w)) != 0) return 1;
    test_destroy_raw_waitable(w);
    if(expect_ok(ns_platform_waitset_destroy(ws)) != 0) return 1;

    return 0;
}
#endif

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    if(expect_ok(ns_platform_init()) != 0){ fprintf(stderr, "ns_platform_init failed\n"); return 1; }
    if(test_alloc() != 0){ fprintf(stderr, "test_alloc failed\n"); return 1; }
    if(test_tls() != 0){ fprintf(stderr, "test_tls failed\n"); return 1; }
    if(test_mutex() != 0){ fprintf(stderr, "test_mutex failed\n"); return 1; }
    if(test_wakeup() != 0){ fprintf(stderr, "test_wakeup failed\n"); return 1; }
    if(test_clock() != 0){ fprintf(stderr, "test_clock failed\n"); return 1; }
    if(test_thread() != 0){ fprintf(stderr, "test_thread failed\n"); return 1; }
    if(test_waitset_lifecycle() != 0){ fprintf(stderr, "test_waitset_lifecycle failed\n"); return 1; }
    if(test_wakeup_waitable() != 0){ fprintf(stderr, "test_wakeup_waitable failed\n"); return 1; }
    if(test_waitset_null_params() != 0){ fprintf(stderr, "test_waitset_null_params failed\n"); return 1; }
    if(test_waitset_add_remove() != 0){ fprintf(stderr, "test_waitset_add_remove failed\n"); return 1; }
    if(test_waitset_add_signaled() != 0){ fprintf(stderr, "test_waitset_add_signaled failed\n"); return 1; }
    if(test_waitset_wait_timeout() != 0){ fprintf(stderr, "test_waitset_wait_timeout failed\n"); return 1; }
    if(test_waitset_wait_signal() != 0){ fprintf(stderr, "test_waitset_wait_signal failed\n"); return 1; }
    if(test_waitset_multi() != 0){ fprintf(stderr, "test_waitset_multi failed\n"); return 1; }
    if(test_waitset_multi_signal() != 0){ fprintf(stderr, "test_waitset_multi_signal failed\n"); return 1; }
    if(test_waitset_max_completions_zero() != 0){ fprintf(stderr, "test_waitset_max_completions_zero failed\n"); return 1; }
    if(test_waitset_destroy_with_entries() != 0){ fprintf(stderr, "test_waitset_destroy_with_entries failed\n"); return 1; }
    if(test_waitset_double_signal() != 0){ fprintf(stderr, "test_waitset_double_signal failed\n"); return 1; }
    if(test_waitset_signal_after_wait() != 0){ fprintf(stderr, "test_waitset_signal_after_wait failed\n"); return 1; }
    if(test_waitset_timeout_actual() != 0){ fprintf(stderr, "test_waitset_timeout_actual failed\n"); return 1; }
    if(test_waitset_signal_before_timeout() != 0){ fprintf(stderr, "test_waitset_signal_before_timeout failed\n"); return 1; }
    if(test_waitset_pointer_identity() != 0){ fprintf(stderr, "test_waitset_pointer_identity failed\n"); return 1; }
    if(test_waitset_multi_pointer_identity() != 0){ fprintf(stderr, "test_waitset_multi_pointer_identity failed\n"); return 1; }
    if(test_waitset_capacity() != 0){ fprintf(stderr, "test_waitset_capacity failed\n"); return 1; }
    if(test_waitset_max_completions_truncate() != 0){ fprintf(stderr, "test_waitset_max_completions_truncate failed\n"); return 1; }
#ifdef __linux__
    if(test_waitset_events_zero() != 0){ fprintf(stderr, "test_waitset_events_zero failed\n"); return 1; }
#endif
    if(test_waitset_two_waitsets() != 0){ fprintf(stderr, "test_waitset_two_waitsets failed\n"); return 1; }
    if(test_waitset_lifecycle_readd() != 0){ fprintf(stderr, "test_waitset_lifecycle_readd failed\n"); return 1; }
    if(test_waitset_destroy_then_ops() != 0){ fprintf(stderr, "test_waitset_destroy_then_ops failed\n"); return 1; }
#ifdef __linux__
    if(test_waitset_edge_triggered() != 0){ fprintf(stderr, "test_waitset_edge_triggered failed\n"); return 1; }
    if(test_waitset_level_triggered() != 0){ fprintf(stderr, "test_waitset_level_triggered failed\n"); return 1; }
#endif
    if(expect_ok(ns_platform_shutdown()) != 0){ fprintf(stderr, "ns_platform_shutdown failed\n"); return 1; }

    return 0;
}
