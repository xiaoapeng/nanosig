/**
 * @file test_broker_lifecycle.c
 * @brief Event broker lifecycle, add/remove, and invalid-path tests.
 * @date 2026-06-28
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>

#include <nanosig/nanosig.h>
#include "test_macros.h"
#include "test_helpers.h"
#include "test_thread.h"
#include "nanosig/internal/ns_broker.h"
#if !defined(_WIN32)
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#endif

static void test_yield(void)
{
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

/* RAW_TO_HANDLE 直接读取 waitable 的平台句柄；句柄是平台头里的标量别名，无需分支。 */
#define RAW_TO_HANDLE(raw) ((raw).primitive)

/* ------------------------------------------------------------------ */
/*  Shared test scaffolding                                            */
/* ------------------------------------------------------------------ */

/* No-op handler for tests that only exercise lifecycle bookkeeping. */
static void noop_watcher_handler(ns_watcher_t *watcher, uint32_t triggered_events)
{
    (void)watcher;
    (void)triggered_events;
}

typedef struct broker_test_ctx {
    atomic_int slot_called;
    ns_loop_t *loop;
    atomic_uint triggered_events;
    ns_watcher_t watcher;
    ns_signal_t signal;
} broker_test_ctx_t;

static test_thread_t g_broker_thread;

/* Handler runs on the broker thread. It carries the readiness back to the
 * user loop by emitting a user-owned no-payload signal (情况二范式). */
static void broker_watcher_handler(ns_watcher_t *w, uint32_t triggered_events)
{
    broker_test_ctx_t *ctx = NS_CONTAINER_OF(w, broker_test_ctx_t, watcher);

    ns_atomic_store_explicit(&ctx->triggered_events, triggered_events, ns_memory_order_release);
    (void)ns_signal_emit(ctx->signal, NS_NO_PAYLOAD);
}

/* Slot runs on the user loop thread. */
static void broker_loop_slot(void *user_data, const ns_no_payload_t *payload)
{
    broker_test_ctx_t *ctx = (broker_test_ctx_t *)user_data;

    (void)payload;
    ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
    (void)ns_loop_quit(ctx->loop);
}

static int broker_worker_entry(void *arg)
{
    broker_test_ctx_t *ctx = (broker_test_ctx_t *)arg;
    int rc;

    rc = ns_loop_init(&ctx->loop, NULL);
    if(rc != NS_OK){
        test_thread_signal_failed(&g_broker_thread, rc);
        return rc;
    }

    test_thread_signal_ready(&g_broker_thread);
    rc = ns_loop_run(ctx->loop);
    (void)ns_loop_deinit(ctx->loop);
    ctx->loop = NULL;
    return rc;
}

static int broker_wait_until_slot_called(broker_test_ctx_t *ctx)
{
    int i;

    for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&ctx->slot_called, ns_memory_order_acquire) != 0) return NS_OK;
        test_yield();
    }

    return NS_E_INVAL;
}

/* ------------------------------------------------------------------ */
/*  Test: ns_broker() presence tracks ns_init/ns_shutdown lifecycle     */
/* ------------------------------------------------------------------ */

static int test_broker_lifecycle(void)
{
    EXPECT_OK(ns_broker() == NULL);
    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_broker() != NULL);
    EXPECT_OK(ns_shutdown() == NS_OK);
    EXPECT_OK(ns_broker() == NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: ns_watcher_* / ns_broker_add/remove reject invalid arguments  */
/* ------------------------------------------------------------------ */

static int test_watcher_invalid_paths(void)
{
    ns_watcher_t watcher = {0};
    ns_watcher_t zero_watcher = {0};
    ns_platform_waitable_t raw;
    uint32_t invalid_event = NS_WAITABLE_EVENT_ERR << 1;

    /* AC-4: 未初始化（含 {0}）的 watcher deinit 返回 NS_E_INVAL。 */
    EXPECT_OK(ns_watcher_deinit(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_watcher_deinit(&zero_watcher) == NS_E_INVAL);
    /* Before ns_init() the broker runtime is down, so a valid handle must
     * yield NS_E_SHUTDOWN. Use a real platform waitable: on Windows an fd of
     * 0 is an invalid HANDLE and would be rejected as NS_E_INVAL first. */
    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_E_SHUTDOWN);
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_E_INVAL);

    EXPECT_OK(ns_init() == NS_OK);
    { ns_waitable_handle_t h = 0;
      /* AC-1: handler == NULL → NS_E_INVAL（先于 watcher NULL / runtime 校验）。 */
      EXPECT_OK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, NULL) == NS_E_INVAL); }
    { ns_waitable_handle_t h = 0; EXPECT_OK(ns_watcher_init(NULL, h, NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_E_INVAL); }
    /* 无效句柄：POSIX 用负 fd，Windows 用 NULL（平台头定义各自的谓词）。 */
#if defined(_WIN32)
    { ns_waitable_handle_t h = NULL; EXPECT_OK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_E_INVAL); }
#else
    { ns_waitable_handle_t h = -1; EXPECT_OK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_E_INVAL); }
#endif
    /* 有效句柄 + 非法 events 位 → NS_E_INVAL；须用真实句柄，否则会先因句柄无效返回。 */
    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); EXPECT_OK(ns_watcher_init(&watcher, h, invalid_event, 0, noop_watcher_handler) == NS_E_INVAL); }
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_E_INVAL);
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_broker_add(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_broker_remove(NULL) == NS_E_INVAL);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_OK);
    /* 二次 init 不做判定：重复 init 属调用方错误（未定义行为），不会返回
       NS_E_EXISTS，而是重新初始化。 */
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(raw);

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: basic broker add/remove transitions                           */
/* ------------------------------------------------------------------ */

static int test_broker_add_remove(void)
{
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_OK);

    EXPECT_OK(ns_broker_add(&watcher) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher) == NS_E_EXISTS);
    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    EXPECT_OK(ns_broker_remove(&watcher) == NS_E_INVAL);

    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: a single watcher event reaches a consuming loop               */
/* ------------------------------------------------------------------ */

static int test_watcher_event_reaches_loop(void)
{
    broker_test_ctx_t ctx = {0};
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int signal_ok = 0;
    int watcher_initialized = 0;
    int connected = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&ctx.slot_called, 0);
    ns_atomic_init(&ctx.triggered_events, 0u);
    ctx.loop = NULL;
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    EXPECT_OK(ns_signal_init(&ctx.signal, ns_no_payload_t) == NS_OK);
    signal_ok = 1;

    test_thread_init(&g_broker_thread, broker_worker_entry, &ctx);
    rc = test_thread_start(&g_broker_thread);
    if(rc != 0) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;

    rc = ns_signal_connect_typed(ctx.signal, broker_loop_slot, ns_no_payload_t, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&ctx.watcher, h, NS_WAITABLE_EVENT_IN, 1, broker_watcher_handler); }
    if(rc != NS_OK) goto fail;
    watcher_initialized = 1;

    rc = ns_broker_add(&ctx.watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    test_signal_raw_waitable(raw);
    rc = broker_wait_until_slot_called(&ctx);
    if(rc != NS_OK) goto fail;

    test_thread_join(&g_broker_thread);
    worker_started = 0;
    EXPECT_OK(g_broker_thread.rc == 0);
    EXPECT_EQ(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire), 1);
    EXPECT_OK((ns_atomic_load_explicit(&ctx.triggered_events, ns_memory_order_acquire) & NS_WAITABLE_EVENT_IN) != 0u);

    EXPECT_OK(ns_broker_remove(&ctx.watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&ctx.watcher) == NS_OK);
    watcher_initialized = 0;
    EXPECT_OK(ns_signal_deinit(&ctx.signal) == NS_OK);
    signal_ok = 0;
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&ctx.watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_initialized) (void)ns_watcher_deinit(&ctx.watcher);
    if(signal_ok) (void)ns_signal_deinit(&ctx.signal);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(ctx.loop != NULL) (void)ns_loop_quit(ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: ns_shutdown removes any residual watcher                     */
/* ------------------------------------------------------------------ */

static int test_shutdown_removes_residual_watcher(void)
{
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher) == NS_OK);

    EXPECT_OK(ns_shutdown() == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(raw);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: broker_remove does not retract an already-enqueued emit       */
/*  The handler emits a user-owned signal; connecting it to both a       */
/*  witness loop and a test loop shows the previously-enqueued emit is   */
/*  still delivered after ns_broker_remove.                              */
/* ------------------------------------------------------------------ */

static int g_broker_remove_retract_test = 0;

static void slot_broker_retract_testee(void *user_data, const ns_no_payload_t *payload)
{
    (void)user_data;
    (void)payload;
    g_broker_remove_retract_test++;
}

typedef struct retract_ctx {
    ns_watcher_t watcher;
    ns_signal_t signal;
} retract_ctx_t;

static void retract_watcher_handler(ns_watcher_t *w, uint32_t triggered_events)
{
    retract_ctx_t *c = NS_CONTAINER_OF(w, retract_ctx_t, watcher);

    (void)triggered_events;
    (void)ns_signal_emit(c->signal, NS_NO_PAYLOAD);
}

static int test_broker_remove_does_not_retract_enqueued(void)
{
    broker_test_ctx_t witness = {0};
    retract_ctx_t testee = {0};
    ns_platform_waitable_t raw;
    ns_connection_t conn_witness;
    ns_connection_t conn_test;
    ns_loop_t *test_loop = NULL;
    int rc;

    g_broker_remove_retract_test = 0;
    ns_atomic_init(&witness.slot_called, 0);
    ns_atomic_init(&witness.triggered_events, 0u);
    witness.loop = NULL;
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    EXPECT_OK(ns_signal_init(&testee.signal, ns_no_payload_t) == NS_OK);

    test_thread_init(&g_broker_thread, broker_worker_entry, &witness);
    rc = test_thread_start(&g_broker_thread);
    EXPECT_OK(rc == 0);
    EXPECT_OK(test_thread_wait_ready(&g_broker_thread) == 0);

    EXPECT_OK(ns_loop_init(&test_loop, NULL) == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&testee.watcher, h, NS_WAITABLE_EVENT_IN, 1, retract_watcher_handler); }
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect_typed(testee.signal, broker_loop_slot, ns_no_payload_t,
                                 witness.loop, &witness, &conn_witness);
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect_typed(testee.signal, slot_broker_retract_testee, ns_no_payload_t,
                                 test_loop, NULL, &conn_test);
    EXPECT_OK(rc == NS_OK);

    rc = ns_broker_add(&testee.watcher);
    EXPECT_OK(rc == NS_OK);

    test_signal_raw_waitable(raw);

    EXPECT_OK(broker_wait_until_slot_called(&witness) == NS_OK);

    test_thread_join(&g_broker_thread);

    EXPECT_OK(ns_broker_remove(&testee.watcher) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn_witness) == NS_OK);

    test_signal_raw_waitable(raw);

    EXPECT_OK(ns_loop_quit(test_loop) == NS_OK);
    rc = ns_loop_run(test_loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_broker_remove_retract_test, 1);

    EXPECT_OK(ns_signal_disconnect(&conn_test) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&testee.watcher) == NS_OK);
    EXPECT_OK(ns_signal_deinit(&testee.signal) == NS_OK);
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_loop_deinit(test_loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: broker error continue                                          */
/*  Injected waitset_wait failure is consumed and the broker thread      */
/*  keeps running — subsequent ns_shutdown must still succeed.          */
/* ------------------------------------------------------------------ */

static int test_broker_error_continue(void)
{
    g_ns_test_waitset_wait_result = NS_E_INVAL;

    EXPECT_OK(ns_init() == NS_OK);

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: ns_watcher_deinit before ns_broker_remove returns NS_E_EXISTS */
/* ------------------------------------------------------------------ */

static int test_watcher_deinit_before_remove(void)
{
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher) == NS_OK);

    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_E_EXISTS);
    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);

    EXPECT_OK(ns_shutdown() == NS_OK);
    test_destroy_raw_waitable(raw);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: ns_broker_add with invalid fd fails (or skips if init fails)  */
/* ------------------------------------------------------------------ */

static int test_broker_add_invalid_fd(void)
{
    ns_watcher_t watcher = {0};

    EXPECT_OK(ns_init() == NS_OK);

    /* POSIX：负 fd 能通过 ns_watcher_init，但 ns_broker_add 注册时应失败。
       Windows：唯一无效句柄是 NULL，会在 ns_watcher_init 阶段被拒绝；
       Windows waitset 只拒绝 NULL 句柄，故按平台语义断言。 */
#if defined(_WIN32)
    { ns_waitable_handle_t h = NULL;
    EXPECT_OK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_E_INVAL); }
#else
    { ns_waitable_handle_t h = -1;
    if(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, noop_watcher_handler) == NS_OK){
        EXPECT_OK(ns_broker_add(&watcher) != NS_OK);
        (void)ns_broker_remove(&watcher);
        EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    } }
#endif

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    if(test_broker_lifecycle() != 0) return 1;
    if(test_watcher_invalid_paths() != 0) return 1;
    if(test_broker_add_remove() != 0) return 1;
    if(test_watcher_event_reaches_loop() != 0) return 1;
    if(test_shutdown_removes_residual_watcher() != 0) return 1;
    if(test_broker_remove_does_not_retract_enqueued() != 0) return 1;
    if(test_broker_error_continue() != 0) return 1;
    if(test_watcher_deinit_before_remove() != 0) return 1;
    if(test_broker_add_invalid_fd() != 0) return 1;

    return 0;
}
