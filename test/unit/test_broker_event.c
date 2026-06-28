/**
 * @file test_broker_event.c
 * @brief Edge/level-triggered and OUT/ERR watcher event delivery tests.
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
#include <sys/socket.h>

#include <nanosig/nanosig.h>
#include "test_macros.h"
#include "test_helpers.h"
#include "test_thread.h"
#if !defined(_WIN32)
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
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
/*  Test: edge_triggered vs level_triggered event delivery semantics    */
/*                                                                    */
/*  edge (EPOLLET): fires once on the not-ready to ready transition.    */
/*    Even if the eventfd is not consumed, no spurious re-fire.        */
/*  level (default epoll): fires every time waitset_wait sees the      */
/*    fd as ready. The slot drains the eventfd to stop the loop.       */
/*                                                                    */
/*  Strategy: signal the eventfd, the slot quits the loop on first     */
/*  fire and drains the eventfd, then check the fire count after join: */
/*    - edge: count == 1 (exactly one fire)                           */
/*    - level: count >= 1 (may have re-fired before quit took effect) */
/* ------------------------------------------------------------------ */

static atomic_int g_edge_level_fire_count;
static int g_edge_level_drain_fd = -1;

/* Slot quits the loop on first fire and drains the eventfd to stop the
 * broker from re-firing. For level-triggered watchers, epoll keeps reporting
 * the ready eventfd until it is consumed; for edge-triggered, draining is
 * harmless because edge only fires once per transition. */
static void slot_edge_level(void *user_data, const void *payload)
{
    broker_test_ctx_t *ctx = (broker_test_ctx_t *)user_data;
    (void)payload;
    int count = ns_atomic_fetch_add_explicit(&g_edge_level_fire_count, 1, ns_memory_order_release);
    if(count == 0){
        if(g_edge_level_drain_fd >= 0){
            uint64_t dummy;
            ssize_t n;
            do {
                n = read(g_edge_level_drain_fd, &dummy, sizeof(dummy));
            } while(n < 0 && errno == EINTR);
            (void)n;
        }
        ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
        (void)ns_loop_quit(ctx->loop);
    }
}

static int test_edge_level_impl(int edge_triggered)
{
    broker_test_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ctx.triggered_events = 0u;
    ns_atomic_init(&g_edge_level_fire_count, 0);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    test_thread_init(&g_broker_thread, broker_worker_entry, &ctx);
    rc = test_thread_start(&g_broker_thread);
    if(rc != 0) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, edge_triggered, NULL); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_signal_connect(&watcher.signal, slot_edge_level, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

#if !defined(_WIN32)
    g_edge_level_drain_fd = raw.primitive.fd;
#endif

    /* Yield so the broker thread processes the add notification and settles
     * back into epoll_wait before we signal; otherwise edge-triggered epoll
     * may miss the transition because the broker hasn't re-entered wait. */
    {
        int i;
        for(i = 0; i < 100000; ++i) test_yield();
    }

    test_signal_raw_waitable(raw);

    if(broker_wait_until_slot_called(&ctx) != NS_OK) goto fail;

    test_thread_join(&g_broker_thread);
    worker_started = 0;
    EXPECT_OK(g_broker_thread.rc == 0);

    if(edge_triggered){
        /* Edge-triggered: fires exactly once on the not-ready to ready
         * transition. No spurious re-fire even though the eventfd is not
         * consumed before the slot runs. */
        EXPECT_EQ(ns_atomic_load_explicit(&g_edge_level_fire_count, ns_memory_order_acquire), 1);
    } else {
        /* Level-triggered: fires at least once. May re-fire because epoll
         * keeps reporting the ready eventfd across waitset_wait calls
         * until the slot drains it. */
        EXPECT_OK(ns_atomic_load_explicit(&g_edge_level_fire_count, ns_memory_order_acquire) >= 1);
    }

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    g_edge_level_drain_fd = -1;
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    g_edge_level_drain_fd = -1;
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(ctx.loop != NULL) (void)ns_loop_quit(ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

static int test_broker_edge_triggered_no_refire(void)
{
    return test_edge_level_impl(1);
}

static int test_broker_level_triggered_may_refire(void)
{
    return test_edge_level_impl(0);
}

/* ------------------------------------------------------------------ */
/*  Test: NS_WAITABLE_EVENT_OUT and NS_WAITABLE_EVENT_ERR flags are    */
/*  accepted during watcher init and produce a valid watcher.          */
/* ------------------------------------------------------------------ */

static int test_watcher_event_out_err(void)
{
    ns_watcher_t watcher_out, watcher_err;
    ns_platform_waitable_t raw;
    int rc;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher_out, h, NS_WAITABLE_EVENT_OUT, 0, NULL); }
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_out) == NS_OK);

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher_err, h, NS_WAITABLE_EVENT_ERR, 0, NULL); }
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_err) == NS_OK);

    {
        ns_watcher_t watcher_all;
        { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher_all, h,
                                NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR, 0, NULL); }
        EXPECT_OK(rc == NS_OK);
        EXPECT_OK(ns_watcher_deinit(&watcher_all) == NS_OK);
    }

    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: OUT event delivery via socketpair                            */
/*  A socketpair fd is writable by default. Register a watcher with    */
/*  NS_WAITABLE_EVENT_OUT on one end — the broker fires the watcher's  */
/*  signal with triggered_events containing EVENT_OUT.                 */
/* ------------------------------------------------------------------ */

static atomic_int g_out_event_slot_called;
static uint32_t g_out_event_triggered;

static void slot_out_event(void *user_data, const void *payload)
{
    const ns_watcher_event_t *ev = (const ns_watcher_event_t *)payload;
    (void)user_data;
    g_out_event_triggered = ev->triggered_events;
    ns_atomic_store_explicit(&g_out_event_slot_called, 1, ns_memory_order_release);
}

static int test_watcher_out_event_delivery(void)
{
#if defined(_WIN32)
    /* Windows: skip — socketpair not available */
    return 0;
#else
    broker_test_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int sv[2] = {-1, -1};
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ctx.triggered_events = 0u;
    ns_atomic_init(&g_out_event_slot_called, 0);
    g_out_event_triggered = 0u;
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    test_thread_init(&g_broker_thread, broker_worker_entry, &ctx);
    rc = test_thread_start(&g_broker_thread);
    if(rc != 0) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) goto fail;

    raw.primitive.fd = sv[0];
    raw.events = NS_WAITABLE_EVENT_OUT;

    { ns_waitable_handle_t h = {.fd = sv[0]}; rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_OUT, 0, NULL); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_signal_connect(&watcher.signal, slot_out_event, ctx.loop, NULL, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    /* Yield so the broker settles into epoll_wait */
    {
        int i;
        for(i = 0; i < 100000; ++i) test_yield();
    }

    /* Wait for OUT event to fire (socketpair is writable by default) */
    {
        int i;
        for(i = 0; i < 1000000; ++i){
            if(ns_atomic_load_explicit(&g_out_event_slot_called, ns_memory_order_acquire) != 0) break;
            test_yield();
        }
    }
    EXPECT_OK(ns_atomic_load_explicit(&g_out_event_slot_called, ns_memory_order_acquire) != 0);

    EXPECT_OK((g_out_event_triggered & NS_WAITABLE_EVENT_OUT) != 0u);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    (void)close(sv[0]);
    (void)close(sv[1]);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(sv[0] >= 0) (void)close(sv[0]);
    if(sv[1] >= 0) (void)close(sv[1]);
    if(worker_started){
        if(ctx.loop != NULL) (void)ns_loop_quit(ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
#endif
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    if(test_broker_edge_triggered_no_refire() != 0) return 1;
    if(test_broker_level_triggered_may_refire() != 0) return 1;
    if(test_watcher_event_out_err() != 0) return 1;
    if(test_watcher_out_event_delivery() != 0) return 1;

    return 0;
}