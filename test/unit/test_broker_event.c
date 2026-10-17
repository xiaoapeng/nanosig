/**
 * @file test_broker_event.c
 * @brief Edge/level-triggered and OUT/ERR watcher event delivery tests.
 * @date 2026-06-28
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>
#include <stdint.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>

#include <nanosig/nanosig.h>
#include "test_macros.h"
#include "test_helpers.h"
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

/* RAW_TO_HANDLE 直接读取 waitable 的平台句柄；句柄是平台头里的标量别名，无需分支。 */
#define RAW_TO_HANDLE(raw) ((raw).primitive)

/* ------------------------------------------------------------------ */
/*  Test: edge_triggered vs level_triggered event delivery semantics    */
/*                                                                    */
/*  edge (EPOLLET): fires once on the not-ready to ready transition.    */
/*    Even if the eventfd is not consumed, no spurious re-fire.        */
/*  level (default epoll): the handler drains the eventfd on first     */
/*    fire, so the broker stops re-reporting it.                       */
/*                                                                    */
/*  Strategy: signal the eventfd, wait until the handler runs and      */
/*  drains it, then check the fire count:                              */
/*    - edge: count == 1 (exactly one fire)                           */
/*    - level: count >= 1                                             */
/* ------------------------------------------------------------------ */

static atomic_int g_edge_level_fire_count;
static atomic_int g_edge_level_handler_called;

/* Handler runs on the broker thread. On first fire it drains the eventfd to
 * stop the broker from re-reporting a level-triggered source. */
static void handler_edge_level(ns_watcher_t *w, uint32_t triggered_events)
{
    int count = ns_atomic_fetch_add_explicit(&g_edge_level_fire_count, 1, ns_memory_order_release);

    (void)triggered_events;
    if(count == 0){
#if !defined(_WIN32)
        ns_waitable_handle_t h = ns_watcher_handle(w);
        uint64_t dummy;
        ssize_t n;
        do {
            n = read(h, &dummy, sizeof(dummy));
        } while(n < 0 && errno == EINTR);
        (void)n;
#else
        (void)w;
#endif
        ns_atomic_store_explicit(&g_edge_level_handler_called, 1, ns_memory_order_release);
    }
}

static int test_edge_level_impl(int edge_triggered)
{
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;
    int watcher_ok = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&g_edge_level_fire_count, 0);
    ns_atomic_init(&g_edge_level_handler_called, 0);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, edge_triggered, handler_edge_level); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    /* Yield so the broker thread processes the add notification and settles
     * back into epoll_wait before we signal; otherwise edge-triggered epoll
     * may miss the transition because the broker hasn't re-entered wait. */
    {
        int i;
        for(i = 0; i < 100000; ++i) test_yield();
    }

    test_signal_raw_waitable(raw);

    {
        int i;
        for(i = 0; i < 1000000; ++i){
            if(ns_atomic_load_explicit(&g_edge_level_handler_called, ns_memory_order_acquire) != 0) break;
            test_yield();
        }
    }

    EXPECT_OK(ns_atomic_load_explicit(&g_edge_level_handler_called, ns_memory_order_acquire) != 0);
    if(edge_triggered){
        /* Edge-triggered: fires exactly once on the not-ready to ready
         * transition. No spurious re-fire even though the eventfd is not
         * consumed before the handler drains it. */
        EXPECT_EQ(ns_atomic_load_explicit(&g_edge_level_fire_count, ns_memory_order_acquire), 1);
    } else {
        /* Level-triggered: fires at least once; the handler drains the
         * eventfd so no tight re-fire loop occurs. */
        EXPECT_OK(ns_atomic_load_explicit(&g_edge_level_fire_count, ns_memory_order_acquire) >= 1);
    }

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
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

static void handler_event_idle(ns_watcher_t *w, uint32_t triggered_events)
{
    (void)w;
    (void)triggered_events;
}

static int test_watcher_event_out_err(void)
{
    ns_watcher_t watcher_out = {0}, watcher_err = {0};
    ns_platform_waitable_t raw;
    int rc;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher_out, h, NS_WAITABLE_EVENT_OUT, 0, handler_event_idle); }
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_out) == NS_OK);

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher_err, h, NS_WAITABLE_EVENT_ERR, 0, handler_event_idle); }
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_err) == NS_OK);

    {
        ns_watcher_t watcher_all = {0};
        { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher_all, h,
                                NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR, 0, handler_event_idle); }
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
/*  NS_WAITABLE_EVENT_OUT on one end — the broker calls the handler    */
/*  with triggered_events containing EVENT_OUT.                        */
/* ------------------------------------------------------------------ */

static atomic_int g_out_event_handler_called;
static atomic_uint g_out_event_triggered;

static void handler_out_event(ns_watcher_t *w, uint32_t triggered_events)
{
    (void)w;
    ns_atomic_store_explicit(&g_out_event_triggered, triggered_events, ns_memory_order_release);
    ns_atomic_store_explicit(&g_out_event_handler_called, 1, ns_memory_order_release);
}

static int test_watcher_out_event_delivery(void)
{
#if defined(_WIN32)
    /* Windows: skip — socketpair not available */
    return 0;
#else
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;
    int sv[2] = {-1, -1};
    int watcher_ok = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&g_out_event_handler_called, 0);
    ns_atomic_store_explicit(&g_out_event_triggered, 0u, ns_memory_order_relaxed);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) goto fail;

    raw.primitive = sv[0];
    raw.events = NS_WAITABLE_EVENT_OUT;

    { ns_waitable_handle_t h = sv[0]; rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_OUT, 0, handler_out_event); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

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
            if(ns_atomic_load_explicit(&g_out_event_handler_called, ns_memory_order_acquire) != 0) break;
            test_yield();
        }
    }
    EXPECT_OK(ns_atomic_load_explicit(&g_out_event_handler_called, ns_memory_order_acquire) != 0);

    EXPECT_OK((ns_atomic_load_explicit(&g_out_event_triggered, ns_memory_order_acquire) & NS_WAITABLE_EVENT_OUT) != 0u);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    (void)close(sv[0]);
    (void)close(sv[1]);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(sv[0] >= 0) (void)close(sv[0]);
    if(sv[1] >= 0) (void)close(sv[1]);
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
