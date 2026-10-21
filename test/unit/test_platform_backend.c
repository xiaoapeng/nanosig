/**
 * @file test_platform_backend.c
 * @brief Platform backend runtime smoke test.
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if defined(__linux__)
#include <unistd.h>  /* close()：构造已关闭 eventfd 的 drain 用例 */
#endif

#include <nanosig/nanosig_port.h>

#include "test_helpers.h"
#include "test_macros.h"

/* ------------------------------------------------------------------ */
/*  basic tests                                                          */
/* ------------------------------------------------------------------ */

static int test_alloc(void)
{
    void *ptr = ns_platform_alloc(16u);

    if(ptr == NULL) return 1;

    memset(ptr, 0, 16u);
    ns_platform_free(ptr);
    return 0;
}

static int test_mutex(void)
{
    ns_platform_mutex_t mutex;

    EXPECT_OK(ns_platform_mutex_init(&mutex, "test-mutex") == NS_OK);
    EXPECT_OK(ns_platform_mutex_lock(&mutex) == NS_OK);
    EXPECT_OK(ns_platform_mutex_unlock(&mutex) == NS_OK);
    EXPECT_OK(ns_platform_mutex_deinit(&mutex) == NS_OK);

    return 0;
}

static int test_wakeup(void)
{
    ns_platform_wakeup_t wakeup;
    ns_platform_wait_result_t wait_result = NS_PLATFORM_WAIT_SIGNALED;

    EXPECT_OK(ns_platform_wakeup_init(&wakeup, "test-wakeup") == NS_OK);
    EXPECT_OK(ns_platform_wakeup_wait(&wakeup, 0u, &wait_result) == NS_OK);
    EXPECT_OK(wait_result == NS_PLATFORM_WAIT_TIMEOUT);
    EXPECT_OK(ns_platform_wakeup_signal(&wakeup) == NS_OK);
    EXPECT_OK(ns_platform_wakeup_wait(&wakeup, NS_PLATFORM_WAIT_INFINITE_US, &wait_result) == NS_OK);
    EXPECT_OK(wait_result == NS_PLATFORM_WAIT_SIGNALED);
    EXPECT_OK(ns_platform_wakeup_deinit(&wakeup) == NS_OK);

    return 0;
}

/* Linux eventfd 的 drain 在非可恢复 errno 下必须先消费计数再报错，否则调用方
   （broker 的 wakeup 等待）重试时 poll 会立即再次返回 POLLIN，形成零等待热自旋。
   用已关闭的 eventfd 构造 EBADF：poll 得 POLLNVAL、read 得 EBADF。 */
static int test_wakeup_drain_closed_fd(void)
{
#if defined(__linux__)
    ns_platform_wakeup_t wakeup;
    ns_platform_wait_result_t wait_result = NS_PLATFORM_WAIT_SIGNALED;

    EXPECT_OK(ns_platform_wakeup_init(&wakeup, "drain-closed-fd") == NS_OK);
    EXPECT_OK(ns_platform_wakeup_signal(&wakeup) == NS_OK);
    EXPECT_OK(close(wakeup) == 0);

    /* 要求：立即返回错误，不得挂死 */
    EXPECT_OK(ns_platform_wakeup_wait(&wakeup, NS_PLATFORM_WAIT_INFINITE_US, &wait_result)
        == NS_E_INVAL);
#endif
    return 0;
}

static int test_clock(void)
{
    ns_platform_time_us_t first = 0u;
    ns_platform_time_us_t second = 0u;

    EXPECT_OK(ns_platform_clock_monotonic_us(&first) == NS_OK);
    EXPECT_OK(ns_platform_clock_monotonic_us(&second) == NS_OK);
    EXPECT_OK(second >= first);

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
    ns_platform_thread_t thread;
    test_thread_ctx_t ctx;

    ctx.value = 0;

    if(ns_platform_thread_init(NULL, test_thread_entry, &ctx, "bad-thread", 0u) != NS_E_INVAL) return 1;
    if(ns_platform_thread_init(&thread, NULL, &ctx, "bad-thread", 0u) != NS_E_INVAL) return 1;
    EXPECT_OK(ns_platform_thread_init(&thread, test_thread_entry, &ctx, "test-thread", 0u) == NS_OK);
    EXPECT_OK(ns_platform_thread_join(&thread) == NS_OK);
    EXPECT_OK(ctx.value == 42);
    if(ns_platform_thread_join(NULL) != NS_E_INVAL) return 1;

    return 0;
}

/* 栈大小参数：0 走默认、显式值生效、过小被拒且不创建线程。 */
static int test_thread_stack_size(void)
{
    ns_platform_thread_t thread;
    test_thread_ctx_t ctx;
    size_t stack_min = 0u;
    size_t stack_max = 0u;

    /* 上下限查询契约：min >= 1，max >= min。 */
    EXPECT_OK(ns_platform_thread_stack_range(&stack_min, &stack_max) == NS_OK);
    EXPECT_OK(stack_min >= 1u);
    EXPECT_OK(stack_max >= stack_min);

    /* stack_size == 0：走后端默认路径。 */
    ctx.value = 0;
    EXPECT_OK(ns_platform_thread_init(&thread, test_thread_entry, &ctx, "thread-default", 0u) == NS_OK);
    EXPECT_OK(ns_platform_thread_join(&thread) == NS_OK);
    EXPECT_OK(ctx.value == 42);

    /* 显式栈大小：合法值创建成功、线程跑完。 */
    ctx.value = 0;
    EXPECT_OK(ns_platform_thread_init(&thread, test_thread_entry, &ctx, "thread-sized",
                                      stack_min) == NS_OK);
    EXPECT_OK(ns_platform_thread_join(&thread) == NS_OK);
    EXPECT_OK(ctx.value == 42);

    /* 过小：必须显式拒绝，且不得创建线程（入口副作用计数保持 0）。 */
    ctx.value = 0;
    EXPECT_OK(ns_platform_thread_init(&thread, test_thread_entry, &ctx, "thread-too-small",
                                      stack_min - 1u) == NS_E_INVAL);
    EXPECT_OK(ctx.value == 0);

    /* init 失败后误调 join：必须返回错误（不得 UB，也不得永久阻塞）。 */
    EXPECT_OK(ns_platform_thread_join(&thread) == NS_E_INVAL);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  waitset tests                                                        */
/* ------------------------------------------------------------------ */

/* test_create_raw_waitable, test_destroy_raw_waitable, test_signal_raw_waitable
   are provided by test_helpers.h */

static int test_waitset_lifecycle(void)
{
    ns_platform_waitset_t ws;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

typedef struct test_event_ctx {
    ns_platform_event_t *event;
} test_event_ctx_t;

static void test_event_signal_entry(void *arg)
{
    test_event_ctx_t *ctx = (test_event_ctx_t *)arg;

    (void)ns_platform_event_signal(ctx->event);
}

static int test_event_backend(void)
{
    ns_platform_event_t event;
    ns_platform_waitset_t ws;
    ns_platform_waitset_completion_t cc[4];
    ns_platform_thread_t thread;
    test_event_ctx_t ctx;
    size_t cnt = 0u;

    EXPECT_OK(ns_platform_event_init(&event, "test-event") == NS_OK);

    /* drain 幂等（无信号时同样返回成功） */
    EXPECT_OK(ns_platform_event_drain(&event) == NS_OK);
    EXPECT_OK(ns_platform_event_drain(&event) == NS_OK);

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    event.waitable.events = NS_WAITABLE_EVENT_IN;
    event.waitable.user_data = (void *)0x1234;

    EXPECT_OK(ns_platform_waitset_add(&ws, &event.waitable) == NS_OK);

    /* 强约束：未 remove 就 deinit → NS_E_BUSY */
    EXPECT_OK(ns_platform_event_deinit(&event) == NS_E_BUSY);

    /* 跨线程 signal → wait 触发，user_data 原样返回 */
    ctx.event = &event;
    EXPECT_OK(ns_platform_thread_init(&thread, test_event_signal_entry, &ctx, "test-event-signal", 0u) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4u, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);
    EXPECT_OK(cc[0].waitable == &event.waitable);
    EXPECT_OK(cc[0].waitable->user_data == (void *)0x1234);
    EXPECT_OK(ns_platform_thread_join(&thread) == NS_OK);

    /* 多次 signal 合并；单次 drain 后电平触发不再重复报告（防 busy-loop） */
    EXPECT_OK(ns_platform_event_signal(&event) == NS_OK);
    EXPECT_OK(ns_platform_event_signal(&event) == NS_OK);
    EXPECT_OK(ns_platform_event_signal(&event) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4u, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);
    EXPECT_OK(ns_platform_event_drain(&event) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 0u, cc, 4u, &cnt) == NS_OK);
    EXPECT_OK(cnt == 0u);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &event.waitable) == NS_OK);
    EXPECT_OK(ns_platform_event_deinit(&event) == NS_OK);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

static int test_event_null_params(void)
{
    ns_platform_event_t event;

    if(ns_platform_event_init(NULL, "test-event-null") != NS_E_INVAL) return 1;
    if(ns_platform_event_signal(NULL) != NS_E_INVAL) return 1;
    if(ns_platform_event_drain(NULL) != NS_E_INVAL) return 1;
    if(ns_platform_event_deinit(NULL) != NS_E_INVAL) return 1;

    EXPECT_OK(ns_platform_event_init(&event, "test-event-double-deinit") == NS_OK);
    EXPECT_OK(ns_platform_event_deinit(&event) == NS_OK);
    EXPECT_OK(ns_platform_event_deinit(&event) == NS_OK);

    return 0;
}

static int test_waitset_null_params(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[1];
    size_t cnt = 0u;

    ns_waitable_init(&w);

    if(ns_platform_waitset_init(NULL) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_deinit(NULL) != NS_E_INVAL) return 1;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    if(ns_platform_waitset_add(&ws, NULL) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_add(&ws, &w) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_remove(&ws, NULL) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_remove(&ws, &w) != NS_E_INVAL) return 1;

    if(ns_platform_waitset_wait(NULL, 0u, cc, 1, &cnt) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_wait(&ws, 0u, NULL, 1, &cnt) != NS_E_INVAL) return 1;
    if(ns_platform_waitset_wait(&ws, 0u, cc, 1, NULL) != NS_E_INVAL) return 1;

    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

static int test_waitset_add_remove(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();

    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);
    if(ns_platform_waitset_add(&ws, &w) != NS_E_EXISTS) return 1;
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    if(ns_platform_waitset_remove(&ws, &w) != NS_E_INVAL) return 1;
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);

    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

static int test_waitset_add_signaled(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();

    test_signal_raw_waitable(w);
    w.user_data = (void *)0xAA;
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    {
        ns_platform_waitset_completion_t cc[4];
        size_t cnt = 0u;
        EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
        EXPECT_OK(cnt >= 1u);
        EXPECT_OK(cc[0].waitable->user_data == (void *)0xAA);
    }

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

static int test_waitset_multi_signal(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w[3];
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;
    int i;
    int created = 0;

    if(ns_platform_waitset_init(&ws) != NS_OK) return 1;

    for(i = 0; i < 3; i++){
        w[i] = test_create_raw_waitable();
        w[i].user_data = (void *)(intptr_t)i;
        created++;
        if(ns_platform_waitset_add(&ws, &w[i]) != NS_OK) goto fail;
    }

    for(i = 0; i < 3; i++){
        test_signal_raw_waitable(w[i]);
    }

    if(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) != NS_OK) goto fail;
    if(cnt < 1u) goto fail;

    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 1;
}

static int test_waitset_max_completions_zero(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[1];
    size_t cnt = 99u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    test_signal_raw_waitable(w);

    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 0u, &cnt) == NS_OK);
    EXPECT_OK(cnt == 0u);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** max_completions == 0 → 立即返回，不做实际等待（跨后端统一契约） */
static int test_waitset_max_completions_zero_no_wait(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[1];
    ns_platform_time_us_t t0, t1;
    size_t cnt = 99u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);

    w = test_create_raw_waitable();
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    /* 未触发的非空 waitset + 有限超时：容量 0 时立即返回，不睡满超时。 */
    EXPECT_OK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 200000u, cc, 0u, &cnt) == NS_OK);
    EXPECT_OK(ns_platform_clock_monotonic_us(&t1) == NS_OK);
    EXPECT_OK(cnt == 0u);
    EXPECT_OK((t1 - t0) < 100000u);

    /* 空 waitset + 无限超时：容量 0 时同样立即返回，不睡满封顶值。 */
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);

    cnt = 99u;
    EXPECT_OK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(
        &ws, NS_PLATFORM_WAIT_INFINITE_US, cc, 0u, &cnt) == NS_OK);
    EXPECT_OK(ns_platform_clock_monotonic_us(&t1) == NS_OK);
    EXPECT_OK(cnt == 0u);
    EXPECT_OK((t1 - t0) < 100000u);

    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

static int test_waitset_destroy_with_entries(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w[2];

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);

    w[0] = test_create_raw_waitable();
    w[1] = test_create_raw_waitable();

    EXPECT_OK(ns_platform_waitset_add(&ws, &w[0]) == NS_OK);
    EXPECT_OK(ns_platform_waitset_add(&ws, &w[1]) == NS_OK);

    if(ns_platform_waitset_deinit(&ws) != NS_E_EXISTS) return 1;
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w[0]) == NS_OK);
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w[1]) == NS_OK);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    test_destroy_raw_waitable(w[0]);
    test_destroy_raw_waitable(w[1]);

    return 0;
}

static int test_waitset_wait_timeout(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitset_completion_t completions[4];
    size_t count = 99u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);

    EXPECT_OK(ns_platform_waitset_wait(&ws, 0u, completions, 4, &count) == NS_OK);
    EXPECT_OK(count == 0u);

    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** 空 waitset + 有限超时 → 睡眠该时长后返回 0 个 completion */
static int test_waitset_wait_empty_finite(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitset_completion_t completions[4];
    size_t count = 99u;
    ns_platform_time_us_t t0, t1;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);

    EXPECT_OK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 50000u, completions, 4, &count) == NS_OK);
    EXPECT_OK(ns_platform_clock_monotonic_us(&t1) == NS_OK);

    EXPECT_OK(count == 0u);
    EXPECT_OK((t1 - t0) >= 40000u);

    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** 空 waitset + 无限超时 → 被 NS_PLATFORM_WAIT_EMPTY_MAX_US 封顶，不永久阻塞 */
static int test_waitset_wait_empty_infinite_capped(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitset_completion_t completions[4];
    size_t count = 99u;
    ns_platform_time_us_t t0, t1;
    ns_platform_time_us_t elapsed;
    ns_platform_time_us_t lower;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);

    EXPECT_OK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(&ws, NS_PLATFORM_WAIT_INFINITE_US, completions, 4, &count) == NS_OK);
    EXPECT_OK(ns_platform_clock_monotonic_us(&t1) == NS_OK);

    EXPECT_OK(count == 0u);

    /* 空 waitset 不允许真无限阻塞：实测等待必须落在 [下界, MAX + 1s]。
       下界在大封顶值（> 1s）时收紧为 MAX - 1s，小封顶值时取 MAX / 2，
       兼顾"证明封顶生效"与 tick / 调度余量。 */
    elapsed = t1 - t0;
    lower = (NS_PLATFORM_WAIT_EMPTY_MAX_US > 1000000u)
        ? (NS_PLATFORM_WAIT_EMPTY_MAX_US - 1000000u)
        : (NS_PLATFORM_WAIT_EMPTY_MAX_US / 2u);
    EXPECT_OK(elapsed >= lower);
    EXPECT_OK(elapsed <= (NS_PLATFORM_WAIT_EMPTY_MAX_US + 1000000u));

    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

static int test_waitset_wait_signal(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t completions[4];
    size_t count = 0u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    w.user_data = (void *)0x42;
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    test_signal_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, completions, 4, &count) == NS_OK);
    EXPECT_OK(count == 1u);
    EXPECT_OK(completions[0].waitable->user_data == (void *)0x42);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

static int test_waitset_multi(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w[3];
    ns_platform_waitset_completion_t completions[4];
    size_t count = 0u;
    int i;
    int found = 0;
    int created = 0;

    if(ns_platform_waitset_init(&ws) != NS_OK) return 1;

    for(i = 0; i < 3; i++){
        w[i] = test_create_raw_waitable();
        w[i].user_data = (void *)(intptr_t)i;
        created++;
        if(ns_platform_waitset_add(&ws, &w[i]) != NS_OK) goto fail;
    }

    test_signal_raw_waitable(w[1]);
    if(ns_platform_waitset_wait(&ws, 1000000u, completions, 4, &count) != NS_OK) goto fail;
    if(count < 1u) goto fail;

    for(i = 0; i < (int)count; i++){
        if(completions[i].waitable->user_data == (void *)(intptr_t)1) found++;
    }
    if(found < 1) goto fail;

    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  行为语义                                                           */
/* ------------------------------------------------------------------ */

/** signal 两次 → wait 只返回 1 个 completion（不重复） */
static int test_waitset_double_signal(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    test_signal_raw_waitable(w);
    test_signal_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** signal → wait → remove + re-add → wait → count=0（信号已消费） */
static int test_waitset_signal_after_wait(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    test_signal_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);

    /* remove + re-add 只重置 waitset 注册，不消费底层 readiness。 */
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    cnt = 99u;
    EXPECT_OK(ns_platform_waitset_wait(&ws, 0u, cc, 4, &cnt) == NS_OK);
    /* POSIX fd/kqueue readiness 仍然可读，remove/add 不能清除。
       Windows auto-reset event: signal 已被消费，返回 0。 */
#ifdef _WIN32
    EXPECT_OK(cnt == 0u);
#else
    EXPECT_OK(cnt >= 1u);
#endif

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** 无 signal + timeout > 0 → 等待后返回 count=0（验证真的等了） */
static int test_waitset_timeout_actual(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 99u;
    ns_platform_time_us_t t0, t1;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    EXPECT_OK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 50000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(ns_platform_clock_monotonic_us(&t1) == NS_OK);

    EXPECT_OK(cnt == 0u);
    EXPECT_OK((t1 - t0) >= 40000u); /* 至少 ~40ms */

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** timeout=1s，signal 在 wait 前到达 → 立即返回 */
static int test_waitset_signal_before_timeout(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;
    ns_platform_time_us_t t0, t1;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    test_signal_raw_waitable(w);
    EXPECT_OK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(ns_platform_clock_monotonic_us(&t1) == NS_OK);

    EXPECT_OK(cnt == 1u);
    /* 应该远小于 1s */
    EXPECT_OK((t1 - t0) < 500000u);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  completion 数据完整性                                               */
/* ------------------------------------------------------------------ */

/** completion.waitable 应该 == add 时传入的 &w（同一指针） */
static int test_waitset_pointer_identity(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    w.user_data = (void *)0x99;
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    test_signal_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);

    /* 指针必须完全一致 */
    EXPECT_OK(cc[0].waitable == &w);
    EXPECT_OK(cc[0].waitable->user_data == (void *)0x99);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** 3 个 waitable signal 2 个 → 各 completion 指向正确 waitable */
static int test_waitset_multi_pointer_identity(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w[3];
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;
    int i, found0 = 0, found2 = 0;
    int created = 0;

    if(ns_platform_waitset_init(&ws) != NS_OK) return 1;

    for(i = 0; i < 3; i++){
        w[i] = test_create_raw_waitable();
        w[i].user_data = (void *)(intptr_t)i;
        created++;
        if(ns_platform_waitset_add(&ws, &w[i]) != NS_OK) goto fail;
    }

    /* signal w[0] 和 w[2]，不 signal w[1] */
    test_signal_raw_waitable(w[0]);
    test_signal_raw_waitable(w[2]);

    if(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) != NS_OK) goto fail;
    if(cnt < 1u) goto fail;

    for(i = 0; i < (int)cnt; i++){
        if(cc[i].waitable == &w[0]) found0++;
        if(cc[i].waitable == &w[2]) found2++;
    }
    /* Windows WFMO 只返回 1 个；POSIX waitsets 可能返回 2 个 */
    if(found0 + found2 < 1) goto fail;

    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  边界                                                               */
/* ------------------------------------------------------------------ */

/** add 到上限成功，再多一个 → NS_E_TOO_MANY_HANDLES */
static int test_waitset_capacity(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w[65];
    int i;
    int created = 0;
    int rc;
    int limit;

    /* POSIX: 64, Windows: 63（预留 1 个给 timer） */
#ifdef _WIN32
    limit = 63;
#else
    limit = 64;
#endif

    if(ns_platform_waitset_init(&ws) != NS_OK) return 1;

    for(i = 0; i < limit + 1; i++){
        w[i] = test_create_raw_waitable();
        created++;
        rc = ns_platform_waitset_add(&ws, &w[i]);
        if(i < limit){
            if(rc != NS_OK) goto fail;
        }else{
            if(rc != NS_E_TOO_MANY_HANDLES) goto fail;
        }
    }

    for(i = 0; i < limit; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    test_destroy_raw_waitable(w[limit]); /* 没 add 成功的，只销毁 handle */
    (void)ns_platform_waitset_deinit(&ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 1;
}

/** 3 个 signal，max_completions=1 → 只返回 1 个 */
static int test_waitset_max_completions_truncate(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w[3];
    ns_platform_waitset_completion_t cc[1];
    size_t cnt = 0u;
    int i;
    int created = 0;

    if(ns_platform_waitset_init(&ws) != NS_OK) return 1;

    for(i = 0; i < 3; i++){
        w[i] = test_create_raw_waitable();
        created++;
        if(ns_platform_waitset_add(&ws, &w[i]) != NS_OK) goto fail;
    }

    for(i = 0; i < 3; i++){
        test_signal_raw_waitable(w[i]);
    }

    if(ns_platform_waitset_wait(&ws, 1000000u, cc, 1, &cnt) != NS_OK) goto fail;
    if(cnt != 1u) goto fail;

    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 0;

fail:
    for(i = 0; i < created; i++){
        (void)ns_platform_waitset_remove(&ws, &w[i]);
        test_destroy_raw_waitable(w[i]);
    }
    (void)ns_platform_waitset_deinit(&ws);
    return 1;
}

/** add events=0 → 成功，signal 后 wait 不返回 completion */
#if defined(__linux__) || defined(__APPLE__)
static int test_waitset_events_zero(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    w.events = 0u; /* 不关注任何事件 */
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    test_signal_raw_waitable(w);
    /* timeout=0 非阻塞，events=0 不应触发 */
    EXPECT_OK(ns_platform_waitset_wait(&ws, 0u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 0u);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}
#endif

/** 同一 waitable 只能注册到一个 waitset */
static int test_waitset_two_waitsets(void)
{
    ns_platform_waitset_t ws1, ws2;
    ns_platform_waitable_t w;

    if(ns_platform_waitset_init(&ws1) != NS_OK) return 1;
    if(ns_platform_waitset_init(&ws2) != NS_OK) goto fail2;

    w = test_create_raw_waitable();
    if(ns_platform_waitset_add(&ws1, &w) != NS_OK) goto fail;
    if(ns_platform_waitset_add(&ws2, &w) != NS_E_EXISTS) goto fail;

    if(ns_platform_waitset_remove(&ws1, &w) != NS_OK) goto fail;
    if(ns_platform_waitset_add(&ws2, &w) != NS_OK) goto fail;
    if(ns_platform_waitset_remove(&ws2, &w) != NS_OK) goto fail;
    test_destroy_raw_waitable(w);
    (void)ns_platform_waitset_deinit(&ws1);
    (void)ns_platform_waitset_deinit(&ws2);
    return 0;

fail:
    (void)ns_platform_waitset_remove(&ws1, &w);
    (void)ns_platform_waitset_remove(&ws2, &w);
    test_destroy_raw_waitable(w);
    (void)ns_platform_waitset_deinit(&ws2);
fail2:
    (void)ns_platform_waitset_deinit(&ws1);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  生命周期                                                           */
/* ------------------------------------------------------------------ */

/** add → signal → wait → remove → add → signal → wait → 正常工作 */
static int test_waitset_lifecycle_readd(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    w.user_data = (void *)0xBB;

    /* 第一轮 */
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);
    test_signal_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);
    EXPECT_OK(cc[0].waitable->user_data == (void *)0xBB);
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);

    /* 第二轮 */
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);
    test_signal_raw_waitable(w);
    cnt = 0u;
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);
    EXPECT_OK(cc[0].waitable->user_data == (void *)0xBB);
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);

    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** destroy 有注册项的 waitset → 拒绝，避免 waitable 注册状态悬挂 */
static int test_waitset_destroy_then_ops(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    if(ns_platform_waitset_deinit(&ws) != NS_E_EXISTS) return 1;
    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    test_destroy_raw_waitable(w);
    return 0;
}

#ifdef __linux__
/** edge_triggered：signal → wait → 再 wait 不触发（ET，需要新边沿） → signal → wait 触发 */
static int test_waitset_edge_triggered(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    w.edge_triggered = 1;
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    /* 第一次 signal + wait → 触发 */
    test_signal_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);

    /* 不 signal，直接 wait → 不触发（ET 模式，需要新的边沿） */
    cnt = 99u;
    EXPECT_OK(ns_platform_waitset_wait(&ws, 0u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 0u);

    /* 再次 signal + wait → 触发 */
    test_signal_raw_waitable(w);
    cnt = 0u;
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}

/** level_triggered：signal → wait → 再 wait 仍触发（底层事件未消费） */
static int test_waitset_level_triggered(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t cc[4];
    size_t cnt = 0u;

    EXPECT_OK(ns_platform_waitset_init(&ws) == NS_OK);
    w = test_create_raw_waitable();
    w.edge_triggered = 0;
    EXPECT_OK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    /* signal → wait → 触发 */
    test_signal_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_wait(&ws, 1000000u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt == 1u);

    /* 再 wait → 仍触发（电平触发：eventfd 未 drain，epoll 仍报告 ready） */
    cnt = 0u;
    EXPECT_OK(ns_platform_waitset_wait(&ws, 0u, cc, 4, &cnt) == NS_OK);
    EXPECT_OK(cnt >= 1u);

    EXPECT_OK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    test_destroy_raw_waitable(w);
    EXPECT_OK(ns_platform_waitset_deinit(&ws) == NS_OK);

    return 0;
}
#endif

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    /* 跨 TU 平台 ABI 探针：引用当前平台分支的探针符号；若库与消费者选到不同
       平台分支（例如 PUBLIC 编译定义未传播），该符号未定义，链接期即失败。 */
    if(NS_PLATFORM_ABI_PROBE != 0){ fprintf(stderr, "platform ABI probe mismatch\n"); return 1; }

    EXPECT_OK(ns_platform_init() == NS_OK);
    if(test_alloc() != 0){ fprintf(stderr, "test_alloc failed\n"); return 1; }
    if(test_mutex() != 0){ fprintf(stderr, "test_mutex failed\n"); return 1; }
    if(test_wakeup() != 0){ fprintf(stderr, "test_wakeup failed\n"); return 1; }
    if(test_wakeup_drain_closed_fd() != 0){ fprintf(stderr, "test_wakeup_drain_closed_fd failed\n"); return 1; }
    if(test_clock() != 0){ fprintf(stderr, "test_clock failed\n"); return 1; }
    if(test_thread() != 0){ fprintf(stderr, "test_thread failed\n"); return 1; }
    if(test_thread_stack_size() != 0){ fprintf(stderr, "test_thread_stack_size failed\n"); return 1; }
    if(test_waitset_lifecycle() != 0){ fprintf(stderr, "test_waitset_lifecycle failed\n"); return 1; }
    if(test_event_backend() != 0){ fprintf(stderr, "test_event_backend failed\n"); return 1; }
    if(test_event_null_params() != 0){ fprintf(stderr, "test_event_null_params failed\n"); return 1; }
    if(test_waitset_null_params() != 0){ fprintf(stderr, "test_waitset_null_params failed\n"); return 1; }
    if(test_waitset_add_remove() != 0){ fprintf(stderr, "test_waitset_add_remove failed\n"); return 1; }
    if(test_waitset_add_signaled() != 0){ fprintf(stderr, "test_waitset_add_signaled failed\n"); return 1; }
    if(test_waitset_wait_timeout() != 0){ fprintf(stderr, "test_waitset_wait_timeout failed\n"); return 1; }
    if(test_waitset_wait_empty_finite() != 0){ fprintf(stderr, "test_waitset_wait_empty_finite failed\n"); return 1; }
    if(test_waitset_wait_empty_infinite_capped() != 0){ fprintf(stderr, "test_waitset_wait_empty_infinite_capped failed\n"); return 1; }
    if(test_waitset_wait_signal() != 0){ fprintf(stderr, "test_waitset_wait_signal failed\n"); return 1; }
    if(test_waitset_multi() != 0){ fprintf(stderr, "test_waitset_multi failed\n"); return 1; }
    if(test_waitset_multi_signal() != 0){ fprintf(stderr, "test_waitset_multi_signal failed\n"); return 1; }
    if(test_waitset_max_completions_zero() != 0){ fprintf(stderr, "test_waitset_max_completions_zero failed\n"); return 1; }
    if(test_waitset_max_completions_zero_no_wait() != 0){ fprintf(stderr, "test_waitset_max_completions_zero_no_wait failed\n"); return 1; }
    if(test_waitset_destroy_with_entries() != 0){ fprintf(stderr, "test_waitset_destroy_with_entries failed\n"); return 1; }
    if(test_waitset_double_signal() != 0){ fprintf(stderr, "test_waitset_double_signal failed\n"); return 1; }
    if(test_waitset_signal_after_wait() != 0){ fprintf(stderr, "test_waitset_signal_after_wait failed\n"); return 1; }
    if(test_waitset_timeout_actual() != 0){ fprintf(stderr, "test_waitset_timeout_actual failed\n"); return 1; }
    if(test_waitset_signal_before_timeout() != 0){ fprintf(stderr, "test_waitset_signal_before_timeout failed\n"); return 1; }
    if(test_waitset_pointer_identity() != 0){ fprintf(stderr, "test_waitset_pointer_identity failed\n"); return 1; }
    if(test_waitset_multi_pointer_identity() != 0){ fprintf(stderr, "test_waitset_multi_pointer_identity failed\n"); return 1; }
    if(test_waitset_capacity() != 0){ fprintf(stderr, "test_waitset_capacity failed\n"); return 1; }
    if(test_waitset_max_completions_truncate() != 0){ fprintf(stderr, "test_waitset_max_completions_truncate failed\n"); return 1; }
#if defined(__linux__) || defined(__APPLE__)
    if(test_waitset_events_zero() != 0){ fprintf(stderr, "test_waitset_events_zero failed\n"); return 1; }
#endif
    if(test_waitset_two_waitsets() != 0){ fprintf(stderr, "test_waitset_two_waitsets failed\n"); return 1; }
    if(test_waitset_lifecycle_readd() != 0){ fprintf(stderr, "test_waitset_lifecycle_readd failed\n"); return 1; }
    if(test_waitset_destroy_then_ops() != 0){ fprintf(stderr, "test_waitset_destroy_then_ops failed\n"); return 1; }
#ifdef __linux__
    if(test_waitset_edge_triggered() != 0){ fprintf(stderr, "test_waitset_edge_triggered failed\n"); return 1; }
    if(test_waitset_level_triggered() != 0){ fprintf(stderr, "test_waitset_level_triggered failed\n"); return 1; }
#endif
    EXPECT_OK(ns_platform_shutdown() == NS_OK);

    return 0;
}
