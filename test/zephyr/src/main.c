/**
 * @file main.c
 * @brief nanosig Zephyr `k_poll` backend contract tests (ztest).
 * @date 2026-10-11
 *
 * 覆盖 platform/zephyr/port.c 的平台契约：
 *
 * - event 生命周期（init → add → deinit busy → remove → deinit）与句柄有效性
 *   （含拒绝 `ns_waitable_init` 的 `(void *)~0` 无效哨兵）。
 * - waitset add/remove 错误语义、IN-only 限制、无效句柄、容量溢出。
 * - wait：电平触发（未 drain 重复触发）、drain 后不再触发、超时（断言等待
 *   ≥ timeout，验证微秒→tick 向上取整不忙等）、max_completions 截断。
 * - remove 只摘除 slot、不 reset 底层 `k_poll_signal`；移除后不再出现在 completions。
 * - `edge_triggered` 忽略（仍按电平触发）。
 * - 跨线程 signal→wait（`k_sem` 显式握手，不依赖调度时序），drain 只在 poller 线程。
 * - wakeup / mutex / thread / clock / broker 端到端。
 *
 * 全部断言在 ztest 线程执行；`k_poll_signal_reset` 只在 poller 线程：broker 用例在
 * handler 内、跨线程用例在独立 poller 线程内，符合 Zephyr 的“reset 只在 poller 线程”约束。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <nanosig/nanosig.h>
#include <nanosig/nanosig_broker.h>
#include <nanosig/nanosig_port.h>

ZTEST_SUITE(nanosig_zephyr, NULL, NULL, NULL, NULL, NULL);

/* ------------------------------------------------------------------ */
/*  跨 TU 平台 ABI 探针                                                 */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_abi_probe)
{
    /* 引用库导出符号：平台选择错配会在链接期失败。 */
    zassert_equal(NS_PLATFORM_ABI_PROBE, 0, "platform ABI probe mismatch");
}

/* ------------------------------------------------------------------ */
/*  event 生命周期                                                      */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_event_lifecycle)
{
    ns_platform_event_t ev;
    ns_platform_waitset_t ws;

    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    zassert_true(ns_waitable_handle_is_valid((ev.waitable.primitive)),
                 "event handle must be valid");

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_ok(ns_platform_waitset_add(&ws, &ev.waitable), "add event");

    /* 仍注册时 deinit 必须报 NS_E_BUSY。 */
    zassert_equal(ns_platform_event_deinit(&ev), NS_E_BUSY,
                  "deinit while registered must be NS_E_BUSY");

    /* waitset 仍有成员时 destroy 必须报 NS_E_EXISTS。 */
    zassert_equal(ns_platform_waitset_deinit(&ws), NS_E_EXISTS,
                  "destroy with members must be NS_E_EXISTS");

    zassert_ok(ns_platform_waitset_remove(&ws, &ev.waitable), "remove event");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  add/remove 错误语义与 IN-only                                       */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_add_remove_errors)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;
    ns_platform_waitable_t w;

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;

    zassert_ok(ns_platform_waitset_add(&ws, &ev.waitable), "first add");
    zassert_equal(ns_platform_waitset_add(&ws, &ev.waitable), NS_E_EXISTS,
                  "duplicate add must be NS_E_EXISTS");
    zassert_ok(ns_platform_waitset_remove(&ws, &ev.waitable), "remove");
    zassert_equal(ns_platform_waitset_remove(&ws, &ev.waitable), NS_E_INVAL,
                  "remove of unregistered must be NS_E_INVAL");

    /* NULL 参数。 */
    zassert_equal(ns_platform_waitset_add(NULL, &w), NS_E_INVAL, "add NULL waitset");
    zassert_equal(ns_platform_waitset_add(&ws, NULL), NS_E_INVAL, "add NULL waitable");
    zassert_equal(ns_platform_waitset_remove(NULL, &w), NS_E_INVAL, "remove NULL waitset");
    zassert_equal(ns_platform_waitset_remove(&ws, NULL), NS_E_INVAL, "remove NULL waitable");

    /* IN-only：OUT / ERR 组合被拒。 */
    ns_waitable_init(&w);
    w.primitive = ev.waitable.primitive;
    w.events = NS_WAITABLE_EVENT_OUT;
    zassert_equal(ns_platform_waitset_add(&ws, &w), NS_E_INVAL, "OUT-only must be rejected");
    w.events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_ERR;
    zassert_equal(ns_platform_waitset_add(&ws, &w), NS_E_INVAL, "IN|ERR must be rejected");
    w.events = 0u;
    zassert_equal(ns_platform_waitset_add(&ws, &w), NS_E_INVAL, "no events must be rejected");

    /* 无效句柄：`ns_waitable_init` 的 `(void *)~0` 哨兵必须被拒，不得进入 waitset
       （否则 wait 会把哨兵交给 k_poll 解引用）。 */
    ns_waitable_init(&w);
    w.events = NS_WAITABLE_EVENT_IN;
    zassert_equal(ns_platform_waitset_add(&ws, &w), NS_E_INVAL,
                  "invalid sentinel handle must be rejected by waitset_add");
    zassert_true(w.registered_waitset == NULL, "rejected waitable must stay unregistered");

    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

ZTEST(nanosig_zephyr, test_event_invalid_handle_rejected)
{
    ns_platform_event_t ev;

    /* 仅 ns_waitable_init、未 event_init：句柄为 (void *)~0 哨兵。 */
    ns_waitable_init(&ev.waitable);
    ev.signal_handle = (void *)(uintptr_t)~0;

    zassert_equal(ns_platform_event_signal(&ev), NS_E_INVAL,
                  "signal on sentinel handle must be NS_E_INVAL");
    zassert_equal(ns_platform_event_drain(&ev), NS_E_INVAL,
                  "drain on sentinel handle must be NS_E_INVAL");
    /* deinit 对无效句柄是安全 no-op，不释放哨兵。 */
    zassert_ok(ns_platform_event_deinit(&ev), "deinit of invalid event is a no-op");
}

/* ------------------------------------------------------------------ */
/*  wait：电平触发、drain、超时                                          */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_wait_level_triggered_and_timeout)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[4];
    ns_platform_time_us_t t0 = 0u;
    ns_platform_time_us_t t1 = 0u;
    ns_platform_time_us_t one_tick_us = (ns_platform_time_us_t)(1000000u / CONFIG_SYS_CLOCK_TICKS_PER_SEC);
    size_t count = 0u;

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_ok(ns_platform_waitset_add(&ws, &ev.waitable), "add");

    /* 未 signal：有限超时返回 0 个完成，且等待不短于 timeout（向上取整、无忙等）。 */
    zassert_ok(ns_platform_clock_monotonic_us(&t0), "clock read");
    count = 99u;
    zassert_ok(ns_platform_waitset_wait(&ws, 10000u, c, 4u, &count), "wait timeout");
    zassert_ok(ns_platform_clock_monotonic_us(&t1), "clock read");
    zassert_true(count == 0u, "no completion before signal");
    zassert_true((t1 - t0) >= 10000u,
                 "timeout wait must last at least timeout (ceil, no busy-wait)");

    /* 亚 tick 超时：1us 向上取整到 1 tick，不得立即返回（floor 会退化成忙等）。 */
    zassert_ok(ns_platform_clock_monotonic_us(&t0), "clock read");
    count = 99u;
    zassert_ok(ns_platform_waitset_wait(&ws, 1u, c, 4u, &count), "wait sub-tick timeout");
    zassert_ok(ns_platform_clock_monotonic_us(&t1), "clock read");
    zassert_true(count == 0u, "sub-tick timeout yields no completion");
    zassert_true((t1 - t0) >= one_tick_us,
                 "sub-tick timeout must round up to at least one tick");

    /* signal 后恰好一个完成，waitable 与 events 正确。 */
    zassert_ok(ns_platform_event_signal(&ev), "signal");
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count), "wait signaled");
    zassert_true(count == 1u, "exactly one completion after signal");
    zassert_true(c[0].waitable == &ev.waitable, "completion waitable mismatch");
    zassert_true((c[0].triggered_events & NS_WAITABLE_EVENT_IN) != 0u,
                 "completion must report IN");

    /* `k_poll_signal` 电平触发：未 drain 时再次 wait 仍触发。 */
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count), "wait again");
    zassert_true(count == 1u, "level-triggered signal stays ready until drained");

    /* drain 幂等；drain 后不再触发。 */
    zassert_ok(ns_platform_event_drain(&ev), "drain");
    zassert_ok(ns_platform_event_drain(&ev), "drain is idempotent");
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count), "wait after drain");
    zassert_true(count == 0u, "no completion after drain");

    zassert_ok(ns_platform_waitset_remove(&ws, &ev.waitable), "remove");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  空 waitset：无限等待被封顶（不永久阻塞）                              */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_wait_empty_infinite_is_capped)
{
    ns_platform_waitset_t ws;
    ns_platform_waitset_completion_t c[4];
    ns_platform_time_us_t t0 = 0u;
    ns_platform_time_us_t t1 = 0u;
    ns_platform_time_us_t elapsed;
    ns_platform_time_us_t lower;
    size_t count = 99u;

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");

    /* 非阻塞：立即返回 0 个完成。 */
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count), "empty non-blocking wait");
    zassert_true(count == 0u, "empty waitset yields no completion");

    /* 空 waitset + 无限超时：必须返回，且等待接近封顶值。 */
    zassert_ok(ns_platform_clock_monotonic_us(&t0), "clock read");
    count = 99u;
    zassert_ok(ns_platform_waitset_wait(&ws, NS_PLATFORM_WAIT_INFINITE_US, c, 4u, &count),
               "empty infinite wait must not block forever");
    zassert_ok(ns_platform_clock_monotonic_us(&t1), "clock read");
    zassert_true(count == 0u, "capped empty wait yields no completion");

    elapsed = t1 - t0;
    lower = (NS_PLATFORM_WAIT_EMPTY_MAX_US > 1000000u)
        ? (NS_PLATFORM_WAIT_EMPTY_MAX_US - 1000000u)
        : (NS_PLATFORM_WAIT_EMPTY_MAX_US / 2u);
    zassert_true(elapsed >= lower, "empty infinite wait must wait ~cap");
    zassert_true(elapsed <= (NS_PLATFORM_WAIT_EMPTY_MAX_US + 1000000u),
                 "empty infinite wait must not exceed cap");

    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  wait：max_completions 截断                                          */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_wait_truncates_completions)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev[3];
    ns_platform_waitset_completion_t c[3];
    size_t count = 0u;
    int i;

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");
    for(i = 0; i < 3; i++){
        zassert_ok(ns_platform_event_init(&ev[i], "ev"), "event init %d", i);
        ev[i].waitable.events = NS_WAITABLE_EVENT_IN;
        zassert_ok(ns_platform_waitset_add(&ws, &ev[i].waitable), "add %d", i);
        zassert_ok(ns_platform_event_signal(&ev[i]), "signal %d", i);
    }

    /* max_completions == 0：立即返回，不等待、不上报。 */
    count = 99u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 0u, &count), "wait max 0");
    zassert_true(count == 0u, "max_completions==0 must not report completions");

    /* 3 个就绪、max_completions == 2：截断为 2。 */
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 2u, &count), "wait truncate");
    zassert_true(count == 2u, "completions must be truncated to max_completions");

    /* 电平保持：未上报的成员在下一轮被上报。 */
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 3u, &count), "wait remainder");
    zassert_true(count == 1u, "remaining signaled event must be reported next round");

    /* 全部 drain 后不再上报。 */
    for(i = 0; i < 3; i++){
        zassert_ok(ns_platform_event_drain(&ev[i]), "drain %d", i);
    }
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 3u, &count), "wait after drain");
    zassert_true(count == 0u, "no completion after drain");

    for(i = 0; i < 3; i++){
        zassert_ok(ns_platform_waitset_remove(&ws, &ev[i].waitable), "remove %d", i);
        zassert_ok(ns_platform_event_deinit(&ev[i]), "deinit %d", i);
    }
    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  remove 只摘除 slot，不 reset 底层信号                                */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_remove_does_not_reset)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0u;

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_ok(ns_platform_waitset_add(&ws, &ev.waitable), "add");
    zassert_ok(ns_platform_event_signal(&ev), "signal");

    /* remove 后不 re-add：pending 的 waitable 不再出现在 completions（AC-5/AC-7）。 */
    zassert_ok(ns_platform_waitset_remove(&ws, &ev.waitable), "remove");
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count), "wait after remove without re-add");
    zassert_true(count == 0u, "removed waitable must not appear in completions");

    /* remove 不 reset：信号仍为 signaled，re-add 后立即再触发。 */
    zassert_ok(ns_platform_waitset_add(&ws, &ev.waitable), "re-add");

    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count), "wait after re-add");
    zassert_true(count == 1u, "remove must not reset; re-add triggers immediately");

    zassert_ok(ns_platform_event_drain(&ev), "drain");
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 4u, &count), "wait after drain");
    zassert_true(count == 0u, "drained and no longer ready");

    zassert_ok(ns_platform_waitset_remove(&ws, &ev.waitable), "remove");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  edge_triggered 忽略                                                 */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_edge_triggered_ignored)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[2];
    size_t count = 0u;

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    ev.waitable.edge_triggered = 1; /* 请求边沿触发；后端忽略，按电平处理 */
    zassert_ok(ns_platform_waitset_add(&ws, &ev.waitable), "add with edge_triggered=1");

    zassert_ok(ns_platform_event_signal(&ev), "signal");
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 2u, &count), "wait");
    zassert_true(count == 1u, "edge_triggered is ignored; event still fires");

    zassert_ok(ns_platform_event_drain(&ev), "drain");
    zassert_ok(ns_platform_waitset_remove(&ws, &ev.waitable), "remove");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  容量溢出                                                            */
/* ------------------------------------------------------------------ */

static ns_platform_event_t s_cap_ev[CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP];

ZTEST(nanosig_zephyr, test_waitset_capacity_overflow)
{
    ns_platform_waitset_t ws;
    ns_platform_event_t extra;
    int i;

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");

    for(i = 0; i < CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP; i++){
        zassert_ok(ns_platform_event_init(&s_cap_ev[i], "cap"), "init %d", i);
        s_cap_ev[i].waitable.events = NS_WAITABLE_EVENT_IN;
        zassert_ok(ns_platform_waitset_add(&ws, &s_cap_ev[i].waitable), "add %d", i);
    }

    zassert_ok(ns_platform_event_init(&extra, "extra"), "init extra");
    extra.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_equal(ns_platform_waitset_add(&ws, &extra.waitable),
                  NS_E_TOO_MANY_HANDLES, "overflow must report NS_E_TOO_MANY_HANDLES");
    zassert_ok(ns_platform_event_deinit(&extra), "deinit extra");

    for(i = 0; i < CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP; i++){
        zassert_ok(ns_platform_waitset_remove(&ws, &s_cap_ev[i].waitable), "remove %d", i);
        zassert_ok(ns_platform_event_deinit(&s_cap_ev[i]), "deinit %d", i);
    }

    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  wakeup / mutex / thread / clock                                     */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_wakeup)
{
    ns_platform_wakeup_t wk;
    ns_platform_wait_result_t r = NS_PLATFORM_WAIT_TIMEOUT;

    zassert_ok(ns_platform_wakeup_init(&wk, "wk"), "wakeup create");
    zassert_ok(ns_platform_wakeup_signal(&wk), "wakeup signal");
    zassert_ok(ns_platform_wakeup_wait(&wk, 100000u, &r), "wakeup wait");
    zassert_equal(r, NS_PLATFORM_WAIT_SIGNALED, "first wait should be signaled");
    zassert_ok(ns_platform_wakeup_wait(&wk, 10000u, &r), "wakeup wait timeout");
    zassert_equal(r, NS_PLATFORM_WAIT_TIMEOUT, "second wait should time out");
    zassert_ok(ns_platform_wakeup_deinit(&wk), "wakeup destroy");
}

ZTEST(nanosig_zephyr, test_mutex)
{
    ns_platform_mutex_t m;

    zassert_ok(ns_platform_mutex_init(&m, "m"), "mutex create");
    zassert_ok(ns_platform_mutex_lock(&m), "mutex lock");
    zassert_ok(ns_platform_mutex_unlock(&m), "mutex unlock");
    zassert_ok(ns_platform_mutex_deinit(&m), "mutex destroy");
}

static volatile int g_thread_ran;

static void test_thread_proc(void *arg)
{
    (void)arg;
    g_thread_ran = 1;
}

ZTEST(nanosig_zephyr, test_thread_create_join)
{
    /* k_thread + 内嵌内核栈内联在结构体中：放静态存储，避免占用 ZTEST 线程栈。 */
    static ns_platform_thread_t t;

    g_thread_ran = 0;
    zassert_ok(ns_platform_thread_init(&t, test_thread_proc, NULL, "t"), "thread create");
    zassert_ok(ns_platform_thread_join(&t), "thread join");
    zassert_true(g_thread_ran == 1, "thread entry must have run");
}

ZTEST(nanosig_zephyr, test_clock_monotonic)
{
    ns_platform_time_us_t t0 = 0u;
    ns_platform_time_us_t t1 = 0u;

    zassert_ok(ns_platform_clock_monotonic_us(&t0), "clock read");
    k_msleep(5);
    zassert_ok(ns_platform_clock_monotonic_us(&t1), "clock read");
    zassert_true(t1 >= t0, "monotonic clock must not go backwards");
}

/* ------------------------------------------------------------------ */
/*  跨线程 signal→wait，drain 只在 poller 线程（计划 Step 4 / R2）        */
/* ------------------------------------------------------------------ */

K_SEM_DEFINE(s_xthread_ready, 0, 1);
K_SEM_DEFINE(s_xthread_done, 0, 1);

static ns_platform_waitset_t     *s_xthread_ws;
static ns_platform_event_t       *s_xthread_ev;
static ns_platform_wakeup_t      *s_xthread_wk;
static ns_platform_wait_result_t  s_xthread_result;
static volatile int               s_xthread_observed;

/* poller 线程：唯一允许执行 k_poll_signal_reset（event_drain）的线程。 */
static void xthread_event_poller(void *arg)
{
    ns_platform_waitset_completion_t c[2];
    int i;

    (void)arg;

    k_sem_give(&s_xthread_ready);

    /* 电平触发：signal 先到或后到都能被观测；循环容忍握手窗口。 */
    for(i = 0; (i < 100) && (s_xthread_observed == 0); i++){
        size_t count = 0u;

        if(ns_platform_waitset_wait(s_xthread_ws, 10000u, c, 2u, &count) != NS_OK) break;

        if((count == 1u) && (c[0].waitable == &s_xthread_ev->waitable)){
            s_xthread_observed = 1;
            (void)ns_platform_event_drain(s_xthread_ev); /* reset 只在 poller 线程 */
        }
    }

    k_sem_give(&s_xthread_done);
}

static void xthread_wakeup_poller(void *arg)
{
    (void)arg;

    k_sem_give(&s_xthread_ready);
    (void)ns_platform_wakeup_wait(s_xthread_wk, 1000000u, &s_xthread_result);
    k_sem_give(&s_xthread_done);
}

ZTEST(nanosig_zephyr, test_event_cross_thread_signal_drain_in_poller)
{
    /* k_thread + 内嵌内核栈：放静态存储，避免占用 ZTEST 线程栈。 */
    static ns_platform_thread_t t;
    ns_platform_event_t ev;
    ns_platform_waitset_t ws;
    ns_platform_waitset_completion_t c[2];
    size_t count = 0u;

    k_sem_reset(&s_xthread_ready);
    k_sem_reset(&s_xthread_done);
    s_xthread_observed = 0;

    zassert_ok(ns_platform_waitset_init(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_ok(ns_platform_waitset_add(&ws, &ev.waitable), "add");

    s_xthread_ws = &ws;
    s_xthread_ev = &ev;

    zassert_ok(ns_platform_thread_init(&t, xthread_event_poller, NULL, "poller"),
               "thread create");

    /* 显式握手：poller 已就绪后再从本线程跨线程 signal，不依赖调度时序。 */
    zassert_ok(k_sem_take(&s_xthread_ready, K_FOREVER), "poller ready handshake");
    zassert_ok(ns_platform_event_signal(&ev), "cross-thread signal");
    zassert_ok(k_sem_take(&s_xthread_done, K_FOREVER), "poller done handshake");

    zassert_true(s_xthread_observed == 1,
                 "cross-thread signal must be observed by poller");
    zassert_ok(ns_platform_thread_join(&t), "thread join");

    /* drain 已在 poller 线程执行：主线程再 wait 不应触发。 */
    zassert_ok(ns_platform_waitset_wait(&ws, 0u, c, 2u, &count), "wait after poller drain");
    zassert_true(count == 0u, "poller drained the signal");

    zassert_ok(ns_platform_waitset_remove(&ws, &ev.waitable), "remove");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_deinit(&ws), "waitset destroy");
}

ZTEST(nanosig_zephyr, test_wakeup_cross_thread_signal_wait)
{
    /* k_thread + 内嵌内核栈：放静态存储，避免占用 ZTEST 线程栈。 */
    static ns_platform_thread_t t;
    ns_platform_wakeup_t wk;

    k_sem_reset(&s_xthread_ready);
    k_sem_reset(&s_xthread_done);

    zassert_ok(ns_platform_wakeup_init(&wk, "wk"), "wakeup create");
    s_xthread_wk = &wk;
    s_xthread_result = NS_PLATFORM_WAIT_TIMEOUT;

    zassert_ok(ns_platform_thread_init(&t, xthread_wakeup_poller, NULL, "wk-poller"),
               "thread create");

    zassert_ok(k_sem_take(&s_xthread_ready, K_FOREVER), "poller ready handshake");
    zassert_ok(ns_platform_wakeup_signal(&wk), "cross-thread wakeup signal");
    zassert_ok(k_sem_take(&s_xthread_done, K_FOREVER), "poller done handshake");

    zassert_ok(ns_platform_thread_join(&t), "thread join");
    zassert_equal(s_xthread_result, NS_PLATFORM_WAIT_SIGNALED,
                  "cross-thread wakeup must be observed");
    zassert_ok(ns_platform_wakeup_deinit(&wk), "wakeup destroy");
}

/* ------------------------------------------------------------------ */
/*  broker 端到端                                                       */
/* ------------------------------------------------------------------ */

static volatile int g_consumed;

/* handler 在 broker（poller）线程运行：这是唯一允许 reset k_poll_signal 的线程。 */
static void broker_handler(ns_watcher_t *watcher, uint32_t triggered_events)
{
    ns_waitable_handle_t h = ns_watcher_handle(watcher);
    struct k_poll_signal *sig = (struct k_poll_signal *)h;

    (void)triggered_events;
    k_poll_signal_reset(sig);
    g_consumed = 1;
}

ZTEST(nanosig_zephyr, test_broker_roundtrip)
{
    ns_platform_event_t ev;
    ns_watcher_t watcher;
    int i;

    zassert_ok(ns_init(), "ns_init");
    zassert_ok(ns_platform_event_init(&ev, "broker-ev"), "event init");
    g_consumed = 0;

    zassert_ok(ns_watcher_init(&watcher, (ev.waitable.primitive),
                               NS_WAITABLE_EVENT_IN, 0, broker_handler),
               "watcher init");
    zassert_ok(ns_broker_add(&watcher), "broker add");

    zassert_ok(ns_platform_event_signal(&ev), "event signal");

    for(i = 0; (i < 2000) && (g_consumed == 0); i++){
        k_msleep(1);
    }
    zassert_true(g_consumed != 0, "broker must consume the event");

    zassert_ok(ns_broker_remove(&watcher), "broker remove");
    zassert_ok(ns_watcher_deinit(&watcher), "watcher deinit");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_shutdown(), "ns_shutdown");
}
