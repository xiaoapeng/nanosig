/**
 * @file test_freertos_waitset.c
 * @brief FreeRTOS queue-set backend contract tests (host POSIX harness).
 * @date 2026-10-04
 *
 * 覆盖 platform/freertos/port.c 的平台契约：event 生命周期、waitset
 * add/remove/wait、IN-only、容量校验、移除时保留其他挂起就绪、加入时清 stale
 * token、数据队列成员 remove 清空、edge_triggered 忽略、非空成员 add 拒绝、drain 幂等、
 * max_completions 截断、wakeup/mutex/thread/clock。所有断言在 FreeRTOS 任务中执行，
 * 结束后用
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
    ns_platform_waitset_t ws;

    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    CHECK(ns_waitable_handle_is_valid((ev.waitable.primitive)));

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_OK);

    /* deinit while still registered must report NS_E_BUSY */
    CHECK(ns_platform_event_deinit(&ev) == NS_E_BUSY);

    CHECK(ns_platform_waitset_remove(&ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  waitset add/remove 错误语义与 IN-only                               */
/* ------------------------------------------------------------------ */

static void test_add_remove_errors(void)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;
    ns_platform_waitable_t w;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;

    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_E_EXISTS);
    CHECK(ns_platform_waitset_remove(&ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &ev.waitable) == NS_E_INVAL);

    /* null 参数 */
    CHECK(ns_platform_waitset_add(NULL, &w) == NS_E_INVAL);
    CHECK(ns_platform_waitset_add(&ws, NULL) == NS_E_INVAL);

    /* IN-only：OUT / ERR 组合被拒 */
    ns_waitable_init(&w);
    w.primitive = ev.waitable.primitive;
    w.events = NS_WAITABLE_EVENT_OUT;
    CHECK(ns_platform_waitset_add(&ws, &w) == NS_E_INVAL);
    w.events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_ERR;
    CHECK(ns_platform_waitset_add(&ws, &w) == NS_E_INVAL);

    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  add：成员非空时拒绝（queue set 要求成员加入时必须为空）               */
/* ------------------------------------------------------------------ */

static void test_add_rejects_nonempty_member(void)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;

    /* 先 signal 使成员非空 → add 必须拒绝，且不登记 waitset。 */
    CHECK(ns_platform_event_signal(&ev) == NS_OK);
    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_E_INVAL);
    CHECK(ev.waitable.registered_waitset == NULL);

    /* drain 清空成员后 add 成功。 */
    CHECK(ns_platform_event_drain(&ev) == NS_OK);
    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &ev.waitable) == NS_OK);

    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  add：edge_triggered 被忽略（后端无原生电平/边沿模式）                 */
/* ------------------------------------------------------------------ */

static void test_add_ignores_edge_triggered(void)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[2];
    size_t count = 0u;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;

    /* edge_triggered=1：被接受且行为不变（FreeRTOS 就绪本就是 token/边沿语义）。 */
    ev.waitable.edge_triggered = 1;
    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_event_signal(&ev) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 2u, &count) == NS_OK);
    CHECK(count == 1u);
    CHECK(ns_platform_event_drain(&ev) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &ev.waitable) == NS_OK);

    /* edge_triggered=0：同样被接受，结果一致——证明该字段不影响注册/触发。 */
    ev.waitable.edge_triggered = 0;
    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_event_signal(&ev) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 2u, &count) == NS_OK);
    CHECK(count == 1u);
    CHECK(ns_platform_event_drain(&ev) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &ev.waitable) == NS_OK);

    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  wait：信号触发、非阻塞无重复、超时                                    */
/* ------------------------------------------------------------------ */

static void test_wait_signal_and_timeout(void)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_OK);

    /* 空 set 超时 */
    count = 99u;
    CHECK(ns_platform_waitset_wait(&ws, 10000u, c, 4u, &count) == NS_OK);
    CHECK(count == 0u);

    /* signal 后恰好一个 completion */
    CHECK(ns_platform_event_signal(&ev) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 1u);
    if(count == 1u){
        CHECK(c[0].waitable == &ev.waitable);
        CHECK((c[0].triggered_events & NS_WAITABLE_EVENT_IN) != 0u);
    }

    /* select 已取走 set token 且未消费成员：不再重复触发 */
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 0u);

    CHECK(ns_platform_event_drain(&ev) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  空 waitset：无限等待被封顶（不永久阻塞）                              */
/* ------------------------------------------------------------------ */

static void test_wait_empty_infinite_capped(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitset_completion_t c[4];
    ns_platform_time_us_t t0 = 0u;
    ns_platform_time_us_t t1 = 0u;
    ns_platform_time_us_t elapsed;
    size_t count = 99u;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);

    /* 非阻塞：立即返回 0 个完成。 */
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 0u);

    /* 有限超时：睡眠该时长后返回 0 个完成。 */
    CHECK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    CHECK(ns_platform_waitset_wait(&ws, 10000u, c, 4u, &count) == NS_OK);
    CHECK(ns_platform_clock_monotonic_us(&t1) == NS_OK);
    CHECK(count == 0u);
    CHECK((t1 - t0) >= 5000u);

    /* 空 waitset + 无限超时：必须返回，且等待接近封顶值。 */
    CHECK(ns_platform_clock_monotonic_us(&t0) == NS_OK);
    count = 99u;
    CHECK(ns_platform_waitset_wait(&ws, NS_PLATFORM_WAIT_INFINITE_US, c, 4u, &count) == NS_OK);
    CHECK(ns_platform_clock_monotonic_us(&t1) == NS_OK);
    CHECK(count == 0u);

    elapsed = t1 - t0;
    CHECK(elapsed >= (ns_platform_time_us_t)((NS_PLATFORM_WAIT_EMPTY_MAX_US > 1000000u)
              ? (NS_PLATFORM_WAIT_EMPTY_MAX_US - 1000000u)
              : (NS_PLATFORM_WAIT_EMPTY_MAX_US / 2u)));
    CHECK(elapsed <= (ns_platform_time_us_t)(NS_PLATFORM_WAIT_EMPTY_MAX_US + 1000000u));

    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  drain 幂等；两次 drain 之间 wait 不重复触发                          */
/* ------------------------------------------------------------------ */

static void test_drain_idempotent(void)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[2];
    size_t count;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&ev, "ev") == NS_OK);
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(&ws, &ev.waitable) == NS_OK);

    CHECK(ns_platform_event_signal(&ev) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 2u, &count) == NS_OK);
    CHECK(count == 1u);

    /* drain 幂等：连续两次 drain 均成功，其间 wait 不再重复触发。 */
    CHECK(ns_platform_event_drain(&ev) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 2u, &count) == NS_OK);
    CHECK(count == 0u);
    CHECK(ns_platform_event_drain(&ev) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 2u, &count) == NS_OK);
    CHECK(count == 0u);

    CHECK(ns_platform_waitset_remove(&ws, &ev.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&ev) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  wait：max_completions 截断，剩余 token 留待下次 wait                 */
/* ------------------------------------------------------------------ */

static void test_wait_truncates_completions(void)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t e1;
    ns_platform_event_t e2;
    ns_platform_waitset_completion_t c[1];
    const ns_platform_waitable_t *first;
    size_t count;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&e1, "e1") == NS_OK);
    CHECK(ns_platform_event_init(&e2, "e2") == NS_OK);
    e1.waitable.events = NS_WAITABLE_EVENT_IN;
    e2.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(&ws, &e1.waitable) == NS_OK);
    CHECK(ns_platform_waitset_add(&ws, &e2.waitable) == NS_OK);

    /* 两个成员都就绪：set 中有两个 token。 */
    CHECK(ns_platform_event_signal(&e1) == NS_OK);
    CHECK(ns_platform_event_signal(&e2) == NS_OK);

    /* max_completions=1：首次只取 1 个。 */
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 1u, &count) == NS_OK);
    CHECK(count == 1u);
    first = c[0].waitable;

    /* 剩余 token 留待下次：第二次取到另一个成员。 */
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 1u, &count) == NS_OK);
    CHECK(count == 1u);
    CHECK(c[0].waitable != first);

    CHECK(ns_platform_event_drain(&e1) == NS_OK);
    CHECK(ns_platform_event_drain(&e2) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &e1.waitable) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &e2.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&e1) == NS_OK);
    CHECK(ns_platform_event_deinit(&e2) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  remove 不丢失其他成员的挂起就绪，并消化被移除成员的残留 token          */
/* ------------------------------------------------------------------ */

static void test_remove_preserves_other_pending(void)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t e1;
    ns_platform_event_t e2;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&e1, "e1") == NS_OK);
    CHECK(ns_platform_event_init(&e2, "e2") == NS_OK);
    e1.waitable.events = NS_WAITABLE_EVENT_IN;
    e2.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(&ws, &e1.waitable) == NS_OK);
    CHECK(ns_platform_waitset_add(&ws, &e2.waitable) == NS_OK);

    /* 两者都就绪：set 中有两个 token */
    CHECK(ns_platform_event_signal(&e1) == NS_OK);
    CHECK(ns_platform_event_signal(&e2) == NS_OK);

    /* 移除 e2 不得丢掉 e1 的挂起就绪 */
    CHECK(ns_platform_waitset_remove(&ws, &e2.waitable) == NS_OK);

    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 1u);
    if(count >= 1u) CHECK(c[0].waitable == &e1.waitable);

    CHECK(ns_platform_event_drain(&e1) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &e1.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&e1) == NS_OK);
    CHECK(ns_platform_event_deinit(&e2) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  add 时 flush 残留 token：被移除成员的 stale token 不产生假完成        */
/* ------------------------------------------------------------------ */

static void test_add_flush_no_spurious(void)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t e1;
    ns_platform_event_t e2;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);
    CHECK(ns_platform_event_init(&e1, "e1") == NS_OK);
    CHECK(ns_platform_event_init(&e2, "e2") == NS_OK);
    e1.waitable.events = NS_WAITABLE_EVENT_IN;
    e2.waitable.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(&ws, &e1.waitable) == NS_OK);

    /* e1 已就绪后移除：set 中残留 e1 的 token */
    CHECK(ns_platform_event_signal(&e1) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &e1.waitable) == NS_OK);

    /* add e2 时清理 stale token，wait 不得产生 e1 的假完成 */
    CHECK(ns_platform_waitset_add(&ws, &e2.waitable) == NS_OK);
    count = 0u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 0u);

    CHECK(ns_platform_waitset_remove(&ws, &e2.waitable) == NS_OK);
    CHECK(ns_platform_event_deinit(&e1) == NS_OK);
    CHECK(ns_platform_event_deinit(&e2) == NS_OK);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  remove 数据队列成员：排空（丢弃未取走消息）——FreeRTOS 特有语义        */
/* ------------------------------------------------------------------ */

static void test_remove_clears_data_queue(void)
{
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w;
    QueueHandle_t q;
    ns_platform_waitset_completion_t c[4];
    uint32_t val;
    size_t count = 0u;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);

    q = xQueueCreate(4u, sizeof(uint32_t));
    CHECK(q != NULL);
    ns_waitable_init(&w);
    w.primitive = (void *)q;
    w.events = NS_WAITABLE_EVENT_IN;

    /* 加入时成员必须为空；刚创建的队列满足。 */
    CHECK(ns_platform_waitset_add(&ws, &w) == NS_OK);

    val = 0x11u; CHECK(xQueueSend(q, &val, 0) == pdPASS);
    val = 0x22u; CHECK(xQueueSend(q, &val, 0) == pdPASS);
    val = 0x33u; CHECK(xQueueSend(q, &val, 0) == pdPASS);
    CHECK(uxQueueMessagesWaiting(q) == 3u);

    /* remove 为满足 xQueueRemoveFromSet“成员必须为空”而排空成员：数据队列的 3 条
       未取走消息被丢弃（见 nanosig_port.h `ns_platform_waitset_remove` @note）。 */
    CHECK(ns_platform_waitset_remove(&ws, &w) == NS_OK);
    CHECK(uxQueueMessagesWaiting(q) == 0u);
    CHECK(xQueueReceive(q, &val, 0) == pdFAIL);

    /* 被移除成员的残留 token 不得产生假完成。 */
    count = 99u;
    CHECK(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count) == NS_OK);
    CHECK(count == 0u);

    /* 已清空，成员重新为空，可再次注册。 */
    CHECK(ns_platform_waitset_add(&ws, &w) == NS_OK);
    CHECK(ns_platform_waitset_remove(&ws, &w) == NS_OK);

    vQueueDelete(q);
    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
}

/* ------------------------------------------------------------------ */
/*  容量校验：Σ成员容量 + max(成员容量) ≤ CAP                            */
/* ------------------------------------------------------------------ */

static void test_capacity_overflow(void)
{
    /* 边界随构建期配置变化：CAP-1 个二值成员可容纳，第 CAP 个溢出。 */
    enum { CAP = NANOSIG_FREERTOS_WAITSET_CAP };
    ns_platform_waitset_t ws;
    ns_platform_waitable_t w[CAP];
    SemaphoreHandle_t sem[CAP];
    QueueHandle_t q;
    ns_platform_waitable_t qw;
    int i;

    CHECK(ns_platform_waitset_init(&ws) == NS_OK);

    for(i = 0; i < CAP; i++){
        sem[i] = xSemaphoreCreateBinary();
        CHECK(sem[i] != NULL);
        ns_waitable_init(&w[i]);
        w[i].primitive = (void *)sem[i];
        w[i].events = NS_WAITABLE_EVENT_IN;
    }

    /* CAP-1 个二值成员可容纳：sum=CAP-1, max=1 -> CAP <= CAP */
    for(i = 0; i < (CAP - 1); i++){
        CHECK(ns_platform_waitset_add(&ws, &w[i]) == NS_OK);
    }
    /* 第 CAP 个溢出：sum=CAP, max=1 -> CAP+1 > CAP */
    CHECK(ns_platform_waitset_add(&ws, &w[CAP - 1]) == NS_E_TOO_MANY_HANDLES);

    for(i = 0; i < (CAP - 1); i++){
        CHECK(ns_platform_waitset_remove(&ws, &w[i]) == NS_OK);
    }

    /* 单个大队列成员即溢出：cap=CAP -> CAP + CAP > CAP */
    q = xQueueCreate((UBaseType_t)CAP, sizeof(uint32_t));
    CHECK(q != NULL);
    ns_waitable_init(&qw);
    qw.primitive = (void *)q;
    qw.events = NS_WAITABLE_EVENT_IN;
    CHECK(ns_platform_waitset_add(&ws, &qw) == NS_E_TOO_MANY_HANDLES);

    CHECK(ns_platform_waitset_deinit(&ws) == NS_OK);
    vQueueDelete(q);
    for(i = 0; i < CAP; i++) vSemaphoreDelete(sem[i]);
}

/* ------------------------------------------------------------------ */
/*  wakeup / mutex / thread / clock                                    */
/* ------------------------------------------------------------------ */

static void test_wakeup(void)
{
    ns_platform_wakeup_t wk;
    ns_platform_wait_result_t r = NS_PLATFORM_WAIT_TIMEOUT;

    CHECK(ns_platform_wakeup_init(&wk, "wk") == NS_OK);
    CHECK(ns_platform_wakeup_signal(&wk) == NS_OK);
    CHECK(ns_platform_wakeup_wait(&wk, 100000u, &r) == NS_OK);
    CHECK(r == NS_PLATFORM_WAIT_SIGNALED);
    CHECK(ns_platform_wakeup_wait(&wk, 10000u, &r) == NS_OK);
    CHECK(r == NS_PLATFORM_WAIT_TIMEOUT);
    CHECK(ns_platform_wakeup_deinit(&wk) == NS_OK);
}

static void test_mutex(void)
{
    ns_platform_mutex_t m;

    CHECK(ns_platform_mutex_init(&m, "m") == NS_OK);
    CHECK(ns_platform_mutex_lock(&m) == NS_OK);
    CHECK(ns_platform_mutex_unlock(&m) == NS_OK);
    CHECK(ns_platform_mutex_deinit(&m) == NS_OK);
}

static volatile int g_thread_ran;

static void thread_proc(void *arg)
{
    (void)arg;
    g_thread_ran = 1;
}

static void test_thread(void)
{
    ns_platform_thread_t t;

    g_thread_ran = 0;
    CHECK(ns_platform_thread_init(&t, thread_proc, NULL, "t") == NS_OK);
    CHECK(ns_platform_thread_join(&t) == NS_OK);
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
    test_add_rejects_nonempty_member();
    test_add_ignores_edge_triggered();
    test_wait_signal_and_timeout();
    test_wait_empty_infinite_capped();
    test_drain_idempotent();
    test_wait_truncates_completions();
    test_remove_preserves_other_pending();
    test_add_flush_no_spurious();
    test_remove_clears_data_queue();
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
