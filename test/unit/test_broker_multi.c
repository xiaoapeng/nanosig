/**
 * @file test_broker_multi.c
 * @brief Event broker multi-watcher and mixed-watcher tests.
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

/* RAW_TO_HANDLE delegates to NS_WAITABLE_GET in nanosig_port.h; no platform branching needed. */
#define RAW_TO_HANDLE(raw) NS_WAITABLE_GET(&(raw))

/* ------------------------------------------------------------------ */
/*  Test context: shared between test function and worker thread       */
/* ------------------------------------------------------------------ */

typedef struct broker_test_ctx {
    atomic_int slot_called;
    ns_loop_t *loop;
    uint32_t triggered_events;
} broker_test_ctx_t;

static test_thread_t g_broker_thread;

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
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Test: multiple watchers registered simultaneously                  */
/*  Two watchers on separate waitables; both added/removed without     */
/*  error; duplicate add returns E_EXISTS; both can be removed.        */
/* ------------------------------------------------------------------ */

static int test_broker_multi_watcher(void)
{
    ns_watcher_t watcher_a, watcher_b;
    ns_platform_waitable_t raw_a, raw_b;

    EXPECT_OK(ns_init() == NS_OK);

    raw_a = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw_a));
    raw_b = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw_b));

    EXPECT_OK(ns_watcher_init(&watcher_a, RAW_TO_HANDLE(raw_a), NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK);
    EXPECT_OK(ns_watcher_init(&watcher_b, RAW_TO_HANDLE(raw_b), NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher_a) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher_b) == NS_OK);

    EXPECT_OK(ns_broker_add(&watcher_a) == NS_E_EXISTS);
    EXPECT_OK(ns_broker_add(&watcher_b) == NS_E_EXISTS);

    EXPECT_OK(ns_broker_remove(&watcher_a) == NS_OK);
    EXPECT_OK(ns_broker_remove(&watcher_b) == NS_OK);

    EXPECT_OK(ns_broker_remove(&watcher_a) == NS_E_INVAL);
    EXPECT_OK(ns_broker_remove(&watcher_b) == NS_E_INVAL);

    EXPECT_OK(ns_broker_add(&watcher_a) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher_b) == NS_OK);
    EXPECT_OK(ns_broker_remove(&watcher_a) == NS_OK);
    EXPECT_OK(ns_broker_remove(&watcher_b) == NS_OK);

    EXPECT_OK(ns_watcher_deinit(&watcher_a) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_b) == NS_OK);
    test_destroy_raw_waitable(raw_a);
    test_destroy_raw_waitable(raw_b);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: mixed watchers — some with consume_fn, some without           */
/*  Both types coexist in the same broker; both deliver events.         */
/* ------------------------------------------------------------------ */

static atomic_int g_mixed_slot_a_called;
static atomic_int g_mixed_slot_b_called;

static int mixed_consume_fn(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);
#if !defined(_WIN32)
    { uint64_t dummy; ssize_t n; do { n = read(h.fd, &dummy, sizeof(dummy)); } while(n < 0 && errno == EINTR); (void)n; }
#endif
    (void)h;
    return 1;
}

static void mixed_slot_a(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ns_atomic_store_explicit(&g_mixed_slot_a_called, 1, ns_memory_order_release);
}

static void mixed_slot_b(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ns_atomic_store_explicit(&g_mixed_slot_b_called, 1, ns_memory_order_release);
}

static int test_mixed_watchers_with_and_without_consume(void)
{
    broker_test_ctx_t ctx;
    ns_watcher_t watcher_a, watcher_b;
    ns_connection_t conn_a, conn_b;
    ns_platform_waitable_t raw_a, raw_b;
    int worker_started = 0;
    int wa_ok = 0, wb_ok = 0;
    int ca = 0, cb = 0;
    int aa = 0, ab = 0;
    int rc;

    ns_atomic_init(&g_mixed_slot_a_called, 0);
    ns_atomic_init(&g_mixed_slot_b_called, 0);
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ctx.triggered_events = 0u;
    ns_waitable_init(&raw_a);
    ns_waitable_init(&raw_b);

    EXPECT_OK(ns_init() == NS_OK);

    test_thread_init(&g_broker_thread, broker_worker_entry, &ctx);
    rc = test_thread_start(&g_broker_thread);
    if(rc != 0) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;

    raw_a = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw_a)) goto fail;
    raw_b = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw_b)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw_a);
      rc = ns_watcher_init(&watcher_a, h, NS_WAITABLE_EVENT_IN, 1, mixed_consume_fn); }
    if(rc != NS_OK) goto fail;
    wa_ok = 1;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw_b);
      rc = ns_watcher_init(&watcher_b, h, NS_WAITABLE_EVENT_IN, 1, NULL); }
    if(rc != NS_OK) goto fail;
    wb_ok = 1;

    rc = ns_signal_connect(&watcher_a.signal, mixed_slot_a, ctx.loop, NULL, &conn_a);
    if(rc != NS_OK) goto fail;
    ca = 1;

    rc = ns_signal_connect(&watcher_b.signal, mixed_slot_b, ctx.loop, NULL, &conn_b);
    if(rc != NS_OK) goto fail;
    cb = 1;

    rc = ns_broker_add(&watcher_a);
    if(rc != NS_OK) goto fail;
    aa = 1;

    rc = ns_broker_add(&watcher_b);
    if(rc != NS_OK) goto fail;
    ab = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw_a);
    test_signal_raw_waitable(raw_b);

    { int i; for(i = 0; i < 1000000; ++i){
        int a = ns_atomic_load_explicit(&g_mixed_slot_a_called, ns_memory_order_acquire);
        int b = ns_atomic_load_explicit(&g_mixed_slot_b_called, ns_memory_order_acquire);
        if(a && b) break;
        test_yield();
    }}

    EXPECT_OK(ns_atomic_load_explicit(&g_mixed_slot_a_called, ns_memory_order_acquire) != 0);
    EXPECT_OK(ns_atomic_load_explicit(&g_mixed_slot_b_called, ns_memory_order_acquire) != 0);

    if(ab) (void)ns_broker_remove(&watcher_b);
    ab = 0;
    if(aa) (void)ns_broker_remove(&watcher_a);
    aa = 0;
    if(cb) (void)ns_signal_disconnect(&conn_b);
    cb = 0;
    if(ca) (void)ns_signal_disconnect(&conn_a);
    ca = 0;
    if(wb_ok) (void)ns_watcher_deinit(&watcher_b);
    wb_ok = 0;
    if(wa_ok) (void)ns_watcher_deinit(&watcher_a);
    wa_ok = 0;
    test_destroy_raw_waitable(raw_b);
    test_destroy_raw_waitable(raw_a);
    if(worker_started){ (void)ns_loop_quit(ctx.loop); test_thread_join(&g_broker_thread); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(ab) (void)ns_broker_remove(&watcher_b);
    if(aa) (void)ns_broker_remove(&watcher_a);
    if(cb) (void)ns_signal_disconnect(&conn_b);
    if(ca) (void)ns_signal_disconnect(&conn_a);
    if(wb_ok) (void)ns_watcher_deinit(&watcher_b);
    if(wa_ok) (void)ns_watcher_deinit(&watcher_a);
    if(test_raw_waitable_is_valid(raw_b)) test_destroy_raw_waitable(raw_b);
    if(test_raw_waitable_is_valid(raw_a)) test_destroy_raw_waitable(raw_a);
    if(worker_started){
        if(ctx.loop != NULL) (void)ns_loop_quit(ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    if(test_broker_multi_watcher() != 0) return 1;
    if(test_mixed_watchers_with_and_without_consume() != 0) return 1;

    return 0;
}