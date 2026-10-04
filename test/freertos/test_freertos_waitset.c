/**
 * @file test_freertos_waitset.c
 * @brief FreeRTOS queue-set backend contract tests (host POSIX harness).
 * @date 2026-10-04
 *
 * 覆盖 platform/freertos/port.c 的平台契约：event 生命周期、waitset
 * add/remove/wait、IN-only、容量校验、移除时保留其他挂起就绪、加入时清 stale
 * token、wakeup/mutex/thread/clock。所有断言在 FreeRTOS 任务中执行，结束后用
 * `_Exit` 以失败计数作为退出码。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include <nanosig/nanosig_port.h>

static int g_failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if(!(cond)){                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
            fflush(stderr);                                                \
            g_failures++;                                                  \
        }                                                                  \
    } while(0)

/* ------------------------------------------------------------------ */
/*  event                                                              */
/* ------------------------------------------------------------------ */

static void test_event_lifecycle(void)
{
    ns_platform_event_t ev;
    ns_platform_waitset_t *ws = NULL;

    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    CHECK(ns_waitable_handle_is_valid(NS_WAITABLE_GET(&ev.waitable)));

    CHECK(ns_platform_waitset_create(&ws) == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(ws, &ev.waitable) == NS_OK);

    /* deinit while still registered must report NS_E_BUSY */
    CHECK(ns_platform_event_deinit(&ev) == NS_E_BUSY);

    CHECK(ns_platform_waitset_remove(ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_destroy(ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  waitset add/remove 错误语义与 IN-only                               */
/* ------------------------------------------------------------------ */

static void test_add_remove_errors(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t ev;
    ns_platform_waitable_t w;

    CHECK(ns_platform_waitset_create(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;

    CHECK(ns_platform_waitset_add(ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_waitset_add(ws, &ev.waitable) == NS_E_EXISTS);
    CHECK(ns_platform_waitset_remove(ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_waitset_remove(ws, &ev.waitable) == NS_E_INVAL);

    /* null 参数 */
    CHECK(ns_platform_waitset_add(NULL, &w) == NS_E_INVAL);
    CHECK(ns_platform_waitset_add(ws, NULL) == NS_E_INVAL);

    /* IN-only：OUT / ERR 组合被拒 */
    ns_waitable_init(&w);
    w.primitive.handle = ev.waitable.primitive.handle;
    w.events = NS_WAITABLE_EVENT_OUT;
    CHECK(ns_platform_waitset_add(ws, &w) == NS_E_INVAL);
    w.events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_ERR;
    CHECK(ns_platform_waitset_add(ws, &w) == NS_E_INVAL);

    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_destroy(ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  wait：信号触发、非阻塞无重复、超时                                    */
/* ------------------------------------------------------------------ */

static void test_wait_signal_and_timeout(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0;

    CHECK(ns_platform_waitset_create(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(ws, &ev.waitable) == NS_OK);

    /* 空 set 超时 */
    count = 99u;
    CHECK(ns_platform_waitset_wait(ws, 10000u, c, 4u, &count) == NS_OK);
    CHECK(count == 0u);

    /* signal 后恰好一个 completion */
    CHECK(ns_platform_event_signal(&ev) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 1u);
    if(count == 1u){
        CHECK(c[0].waitable == &ev.waitable);
        CHECK((c[0].triggered_events & NS_WAITABLE_EVENT_IN) != 0u);
    }

    /* select 已取走 set token 且未消费成员：不再重复触发 */
    count = 0u;
    CHECK(ns_platform_waitset_wait(ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 0u);

    CHECK(ns_platform_event_drain(&ev) == NS_OK);
    CHECK(ns_platform_waitset_remove(ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_destroy(ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  remove 不丢失其他成员的挂起就绪，并消化被移除成员的残留 token          */
/* ------------------------------------------------------------------ */

static void test_remove_preserves_other_pending(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t e1;
    ns_platform_event_t e2;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0;

    CHECK(ns_platform_waitset_create(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&e1, "e1") == NS_OK);
    CHECK(ns_platform_event_init(&e2, "e2") == NS_OK);
    e1.waitable.events = NS_WAITABLE_EVENT_IN;
    e2.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(ws, &e1.waitable) == NS_OK);
    CHECK(ns_platform_waitset_add(ws, &e2.waitable) == NS_OK);

    /* 两者都就绪：set 中有两个 token */
    CHECK(ns_platform_event_signal(&e1) == NS_OK);
    CHECK(ns_platform_event_signal(&e2) == NS_OK);

    /* 移除 e2 不得丢掉 e1 的挂起就绪 */
    CHECK(ns_platform_waitset_remove(ws, &e2.waitable) == NS_OK);

    count = 0u;
    CHECK(ns_platform_waitset_wait(ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 1u);
    if(count >= 1u) CHECK(c[0].waitable == &e1.waitable);

    CHECK(ns_platform_event_drain(&e1) == NS_OK);
    CHECK(ns_platform_waitset_remove(ws, &e1.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&e1) == NS_OK);
    CHECK(ns_platform_event_deinit(&e2) == NS_OK);
    CHECK(ns_platform_waitset_destroy(ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  add 时 flush 残留 token：被移除成员的 stale token 不产生假完成        */
/* ------------------------------------------------------------------ */

static void test_add_flush_no_spurious(void)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t e1;
    ns_platform_event_t e2;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0;

    CHECK(ns_platform_waitset_create(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&e1, "e1") == NS_OK);
    CHECK(ns_platform_event_init(&e2, "e2") == NS_OK);
    e1.waitable.events = NS_WAITABLE_EVENT_IN;
    e2.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(ws, &e1.waitable) == NS_OK);

    /* e1 已就绪后移除：set 中残留 e1 的 token */
    CHECK(ns_platform_event_signal(&e1) == NS_OK);
    CHECK(ns_platform_waitset_remove(ws, &e1.waitable) == NS_OK);

    /* add e2 时清理 stale token，wait 不得产生 e1 的假完成 */
    CHECK(ns_platform_waitset_add(ws, &e2.waitable) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 0u);

    CHECK(ns_platform_waitset_remove(ws, &e2.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&e1) == NS_OK);
    CHECK(ns_platform_event_deinit(&e2) == NS_OK);
    CHECK(ns_platform_waitset_destroy(ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  容量校验：Σ成员容量 + max(成员容量) ≤ CAP                            */
/* ------------------------------------------------------------------ */

static void test_capacity_overflow(void)
{
    /* 边界随构建期配置变化：CAP-1 个二值成员可容纳，第 CAP 个溢出。 */
    enum { CAP = NANOSIG_FREERTOS_WAITSET_CAP };
    ns_platform_waitset_t *ws = NULL;
    ns_platform_waitable_t w[CAP];
    SemaphoreHandle_t sem[CAP];
    QueueHandle_t q;
    ns_platform_waitable_t qw;
    int i;

    CHECK(ns_platform_waitset_create(&ws) == NS_OK);

    for(i = 0; i < CAP; i++){
        sem[i] = xSemaphoreCreateBinary();
        CHECK(sem[i] != NULL);
        ns_waitable_init(&w[i]);
        w[i].primitive.handle = (void *)sem[i];
        w[i].events = NS_WAITABLE_EVENT_IN;
    }

    /* CAP-1 个二值成员可容纳：sum=CAP-1, max=1 -> CAP <= CAP */
    for(i = 0; i < (CAP - 1); i++){
        CHECK(ns_platform_waitset_add(ws, &w[i]) == NS_OK);
    }
    /* 第 CAP 个溢出：sum=CAP, max=1 -> CAP+1 > CAP */
    CHECK(ns_platform_waitset_add(ws, &w[CAP - 1]) == NS_E_TOO_MANY_HANDLES);

    for(i = 0; i < (CAP - 1); i++){
        CHECK(ns_platform_waitset_remove(ws, &w[i]) == NS_OK);
    }

    /* 单个大队列成员即溢出：cap=CAP -> CAP + CAP > CAP */
    q = xQueueCreate((UBaseType_t)CAP, sizeof(uint32_t));
    CHECK(q != NULL);
    ns_waitable_init(&qw);
    qw.primitive.handle = (void *)q;
    qw.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(ws, &qw) == NS_E_TOO_MANY_HANDLES);

    CHECK(ns_platform_waitset_destroy(ws) == NS_OK);
    vQueueDelete(q);
    for(i = 0; i < CAP; i++) vSemaphoreDelete(sem[i]);
}

/* ------------------------------------------------------------------ */
/*  wakeup / mutex / thread / clock                                    */
/* ------------------------------------------------------------------ */

static void test_wakeup(void)
{
    ns_platform_wakeup_t *wk = NULL;
    ns_platform_wait_result_t r = NS_PLATFORM_WAIT_TIMEOUT;

    CHECK(ns_platform_wakeup_create(&wk, "wk") == NS_OK);
    CHECK(ns_platform_wakeup_signal(wk) == NS_OK);
    CHECK(ns_platform_wakeup_wait(wk, 100000u, &r) == NS_OK);
    CHECK(r == NS_PLATFORM_WAIT_SIGNALED);
    CHECK(ns_platform_wakeup_wait(wk, 10000u, &r) == NS_OK);
    CHECK(r == NS_PLATFORM_WAIT_TIMEOUT);
    CHECK(ns_platform_wakeup_destroy(wk) == NS_OK);
}

static void test_mutex(void)
{
    ns_platform_mutex_t *m = NULL;

    CHECK(ns_platform_mutex_create(&m, "m") == NS_OK);
    CHECK(ns_platform_mutex_lock(m) == NS_OK);
    CHECK(ns_platform_mutex_unlock(m) == NS_OK);
    CHECK(ns_platform_mutex_destroy(m) == NS_OK);
}

static volatile int g_thread_ran;

static void thread_proc(void *arg)
{
    (void)arg;
    g_thread_ran = 1;
}

static void test_thread(void)
{
    ns_platform_thread_t *t = NULL;

    g_thread_ran = 0;
    CHECK(ns_platform_thread_create(&t, thread_proc, NULL, "t") == NS_OK);
    CHECK(ns_platform_thread_join(t) == NS_OK);
    CHECK(g_thread_ran == 1);
}

static void test_clock(void)
{
    ns_platform_time_us_t t0 = 0u;
    ns_platform_time_us_t t1 = 0u;

    CHECK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    vTaskDelay(pdMS_TO_TICKS(5));
    CHECK(ns_platform_clock_monotonic_us(&t1) == NS_OK);
    CHECK(t1 >= t0);
}

/* ------------------------------------------------------------------ */
/*  runner                                                             */
/* ------------------------------------------------------------------ */

static void runner_task(void *arg)
{
    (void)arg;

    test_event_lifecycle();
    test_add_remove_errors();
    test_wait_signal_and_timeout();
    test_remove_preserves_other_pending();
    test_add_flush_no_spurious();
    test_capacity_overflow();
    test_wakeup();
    test_mutex();
    test_thread();
    test_clock();

    fflush(NULL);
    _Exit(g_failures != 0 ? 1 : 0);
}

int main(void)
{
    /* 跨 TU 平台 ABI 探针：消费者必须与库选到同一平台分支，否则链接期失败。 */
    if(NS_PLATFORM_ABI_PROBE != 0){
        fprintf(stderr, "platform ABI probe mismatch\n");
        return 2;
    }

    if(xTaskCreate(runner_task, "runner", 4096, NULL, 2, NULL) != pdPASS){
        fprintf(stderr, "failed to create runner task\n");
        return 2;
    }

    vTaskStartScheduler();

    return 3; /* 不可达：runner 用 _Exit 结束进程 */
}
