/**
 * @file main.c
 * @brief nanosig Zephyr `k_poll` backend contract tests (ztest).
 * @date 2026-10-11
 *
 * 覆盖 platform/zephyr/port.c 的平台契约：
 *
 * - event 生命周期（init → add → deinit busy → remove → deinit）与句柄有效性。
 * - waitset add/remove 错误语义、IN-only 限制、容量溢出。
 * - wait：电平触发（未 drain 重复触发）、drain 后不再触发、超时。
 * - remove 只摘除 slot、不 reset 底层 `k_poll_signal`。
 * - `edge_triggered` 忽略（仍按电平触发）。
 * - wakeup / mutex / thread / clock / broker 端到端。
 *
 * 全部断言在 ztest 线程执行；`k_poll_signal_reset` 只在 broker（poller）线程的
 * consume_fn 内调用，符合 Zephyr 的“reset 只在 poller 线程”约束。
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
    ns_platform_waitset_t *ws = NULL;

    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    zassert_true(ns_waitable_handle_is_valid(NS_WAITABLE_GET(&ev.waitable)),
                 "event handle must be valid");

    zassert_ok(ns_platform_waitset_create(&ws), "waitset create");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_ok(ns_platform_waitset_add(ws, &ev.waitable), "add event");

    /* 仍注册时 deinit 必须报 NS_E_BUSY。 */
    zassert_equal(ns_platform_event_deinit(&ev), NS_E_BUSY,
                  "deinit while registered must be NS_E_BUSY");

    /* waitset 仍有成员时 destroy 必须报 NS_E_EXISTS。 */
    zassert_equal(ns_platform_waitset_destroy(ws), NS_E_EXISTS,
                  "destroy with members must be NS_E_EXISTS");

    zassert_ok(ns_platform_waitset_remove(ws, &ev.waitable), "remove event");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_destroy(ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  add/remove 错误语义与 IN-only                                       */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_add_remove_errors)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t ev;
    ns_platform_waitable_t w;

    zassert_ok(ns_platform_waitset_create(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;

    zassert_ok(ns_platform_waitset_add(ws, &ev.waitable), "first add");
    zassert_equal(ns_platform_waitset_add(ws, &ev.waitable), NS_E_EXISTS,
                  "duplicate add must be NS_E_EXISTS");
    zassert_ok(ns_platform_waitset_remove(ws, &ev.waitable), "remove");
    zassert_equal(ns_platform_waitset_remove(ws, &ev.waitable), NS_E_INVAL,
                  "remove of unregistered must be NS_E_INVAL");

    /* NULL 参数。 */
    zassert_equal(ns_platform_waitset_add(NULL, &w), NS_E_INVAL, "add NULL waitset");
    zassert_equal(ns_platform_waitset_add(ws, NULL), NS_E_INVAL, "add NULL waitable");
    zassert_equal(ns_platform_waitset_remove(NULL, &w), NS_E_INVAL, "remove NULL waitset");
    zassert_equal(ns_platform_waitset_remove(ws, NULL), NS_E_INVAL, "remove NULL waitable");

    /* IN-only：OUT / ERR 组合被拒。 */
    ns_waitable_init(&w);
    w.primitive.handle = ev.waitable.primitive.handle;
    w.events = NS_WAITABLE_EVENT_OUT;
    zassert_equal(ns_platform_waitset_add(ws, &w), NS_E_INVAL, "OUT-only must be rejected");
    w.events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_ERR;
    zassert_equal(ns_platform_waitset_add(ws, &w), NS_E_INVAL, "IN|ERR must be rejected");
    w.events = 0u;
    zassert_equal(ns_platform_waitset_add(ws, &w), NS_E_INVAL, "no events must be rejected");

    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_destroy(ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  wait：电平触发、drain、超时                                          */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_wait_level_triggered_and_timeout)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0u;

    zassert_ok(ns_platform_waitset_create(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_ok(ns_platform_waitset_add(ws, &ev.waitable), "add");

    /* 未 signal：有限超时返回 0 个完成。 */
    count = 99u;
    zassert_ok(ns_platform_waitset_wait(ws, 10000u, c, 4u, &count), "wait timeout");
    zassert_true(count == 0u, "no completion before signal");

    /* signal 后恰好一个完成，waitable 与 events 正确。 */
    zassert_ok(ns_platform_event_signal(&ev), "signal");
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(ws, 0u, c, 4u, &count), "wait signaled");
    zassert_true(count == 1u, "exactly one completion after signal");
    zassert_true(c[0].waitable == &ev.waitable, "completion waitable mismatch");
    zassert_true((c[0].triggered_events & NS_WAITABLE_EVENT_IN) != 0u,
                 "completion must report IN");

    /* `k_poll_signal` 电平触发：未 drain 时再次 wait 仍触发。 */
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(ws, 0u, c, 4u, &count), "wait again");
    zassert_true(count == 1u, "level-triggered signal stays ready until drained");

    /* drain 幂等；drain 后不再触发。 */
    zassert_ok(ns_platform_event_drain(&ev), "drain");
    zassert_ok(ns_platform_event_drain(&ev), "drain is idempotent");
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(ws, 0u, c, 4u, &count), "wait after drain");
    zassert_true(count == 0u, "no completion after drain");

    zassert_ok(ns_platform_waitset_remove(ws, &ev.waitable), "remove");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_destroy(ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  remove 只摘除 slot，不 reset 底层信号                                */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_remove_does_not_reset)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[4];
    size_t count = 0u;

    zassert_ok(ns_platform_waitset_create(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_ok(ns_platform_waitset_add(ws, &ev.waitable), "add");
    zassert_ok(ns_platform_event_signal(&ev), "signal");

    /* remove 不 reset：信号仍为 signaled。 */
    zassert_ok(ns_platform_waitset_remove(ws, &ev.waitable), "remove");
    zassert_ok(ns_platform_waitset_add(ws, &ev.waitable), "re-add");

    count = 0u;
    zassert_ok(ns_platform_waitset_wait(ws, 0u, c, 4u, &count), "wait after re-add");
    zassert_true(count == 1u, "remove must not reset; re-add triggers immediately");

    zassert_ok(ns_platform_event_drain(&ev), "drain");
    count = 0u;
    zassert_ok(ns_platform_waitset_wait(ws, 0u, c, 4u, &count), "wait after drain");
    zassert_true(count == 0u, "drained and no longer ready");

    zassert_ok(ns_platform_waitset_remove(ws, &ev.waitable), "remove");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_destroy(ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  edge_triggered 忽略                                                 */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_edge_triggered_ignored)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t ev;
    ns_platform_waitset_completion_t c[2];
    size_t count = 0u;

    zassert_ok(ns_platform_waitset_create(&ws), "waitset create");
    zassert_ok(ns_platform_event_init(&ev, "ev"), "event init");
    ev.waitable.events = NS_WAITABLE_EVENT_IN;
    ev.waitable.edge_triggered = 1; /* 请求边沿触发；后端忽略，按电平处理 */
    zassert_ok(ns_platform_waitset_add(ws, &ev.waitable), "add with edge_triggered=1");

    zassert_ok(ns_platform_event_signal(&ev), "signal");
    zassert_ok(ns_platform_waitset_wait(ws, 0u, c, 2u, &count), "wait");
    zassert_true(count == 1u, "edge_triggered is ignored; event still fires");

    zassert_ok(ns_platform_event_drain(&ev), "drain");
    zassert_ok(ns_platform_waitset_remove(ws, &ev.waitable), "remove");
    zassert_ok(ns_platform_event_deinit(&ev), "event deinit");
    zassert_ok(ns_platform_waitset_destroy(ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  容量溢出                                                            */
/* ------------------------------------------------------------------ */

static ns_platform_event_t s_cap_ev[CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP];

ZTEST(nanosig_zephyr, test_waitset_capacity_overflow)
{
    ns_platform_waitset_t *ws = NULL;
    ns_platform_event_t extra;
    int i;

    zassert_ok(ns_platform_waitset_create(&ws), "waitset create");

    for(i = 0; i < CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP; i++){
        zassert_ok(ns_platform_event_init(&s_cap_ev[i], "cap"), "init %d", i);
        s_cap_ev[i].waitable.events = NS_WAITABLE_EVENT_IN;
        zassert_ok(ns_platform_waitset_add(ws, &s_cap_ev[i].waitable), "add %d", i);
    }

    zassert_ok(ns_platform_event_init(&extra, "extra"), "init extra");
    extra.waitable.events = NS_WAITABLE_EVENT_IN;
    zassert_equal(ns_platform_waitset_add(ws, &extra.waitable),
                  NS_E_TOO_MANY_HANDLES, "overflow must report NS_E_TOO_MANY_HANDLES");
    zassert_ok(ns_platform_event_deinit(&extra), "deinit extra");

    for(i = 0; i < CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP; i++){
        zassert_ok(ns_platform_waitset_remove(ws, &s_cap_ev[i].waitable), "remove %d", i);
        zassert_ok(ns_platform_event_deinit(&s_cap_ev[i]), "deinit %d", i);
    }

    zassert_ok(ns_platform_waitset_destroy(ws), "waitset destroy");
}

/* ------------------------------------------------------------------ */
/*  wakeup / mutex / thread / clock                                     */
/* ------------------------------------------------------------------ */

ZTEST(nanosig_zephyr, test_wakeup)
{
    ns_platform_wakeup_t *wk = NULL;
    ns_platform_wait_result_t r = NS_PLATFORM_WAIT_TIMEOUT;

    zassert_ok(ns_platform_wakeup_create(&wk, "wk"), "wakeup create");
    zassert_ok(ns_platform_wakeup_signal(wk), "wakeup signal");
    zassert_ok(ns_platform_wakeup_wait(wk, 100000u, &r), "wakeup wait");
    zassert_equal(r, NS_PLATFORM_WAIT_SIGNALED, "first wait should be signaled");
    zassert_ok(ns_platform_wakeup_wait(wk, 10000u, &r), "wakeup wait timeout");
    zassert_equal(r, NS_PLATFORM_WAIT_TIMEOUT, "second wait should time out");
    zassert_ok(ns_platform_wakeup_destroy(wk), "wakeup destroy");
}

ZTEST(nanosig_zephyr, test_mutex)
{
    ns_platform_mutex_t *m = NULL;

    zassert_ok(ns_platform_mutex_create(&m, "m"), "mutex create");
    zassert_ok(ns_platform_mutex_lock(m), "mutex lock");
    zassert_ok(ns_platform_mutex_unlock(m), "mutex unlock");
    zassert_ok(ns_platform_mutex_destroy(m), "mutex destroy");
}

static volatile int g_thread_ran;

static void test_thread_proc(void *arg)
{
    (void)arg;
    g_thread_ran = 1;
}

ZTEST(nanosig_zephyr, test_thread_create_join)
{
    ns_platform_thread_t *t = NULL;

    g_thread_ran = 0;
    zassert_ok(ns_platform_thread_create(&t, test_thread_proc, NULL, "t"), "thread create");
    zassert_ok(ns_platform_thread_join(t), "thread join");
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
/*  broker 端到端                                                       */
/* ------------------------------------------------------------------ */

static volatile int g_consumed;

/* consume_fn 在 broker（poller）线程运行：这是唯一允许 reset k_poll_signal 的线程。 */
static int broker_consume(ns_watcher_t *watcher)
{
    ns_waitable_handle_t h = ns_watcher_handle(watcher);
    struct k_poll_signal *sig = (struct k_poll_signal *)h.handle;

    k_poll_signal_reset(sig);
    g_consumed = 1;
    return 1;
}

ZTEST(nanosig_zephyr, test_broker_roundtrip)
{
    ns_platform_event_t ev;
    ns_watcher_t watcher;
    int i;

    zassert_ok(ns_init(), "ns_init");
    zassert_ok(ns_platform_event_init(&ev, "broker-ev"), "event init");
    g_consumed = 0;

    zassert_ok(ns_watcher_init(&watcher, NS_WAITABLE_GET(&ev.waitable),
                               NS_WAITABLE_EVENT_IN, 0, broker_consume),
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
