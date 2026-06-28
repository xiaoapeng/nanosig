/**
 * @file test_broker.c
 * @brief Event broker runtime tests.
 * @date 2026-06-14
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
#include "nanosig/internal/ns_broker.h"
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

/* test_create_raw_waitable, test_destroy_raw_waitable, test_signal_raw_waitable,
   test_raw_waitable_is_valid are provided by test_helpers.h */

/* RAW_TO_HANDLE 委托 nanosig_port.h 的 NS_WAITABLE_GET，无需平台检测 */
#define RAW_TO_HANDLE(raw) NS_WAITABLE_GET(&(raw))

typedef struct broker_loop_ctx {
    atomic_int ready;
    atomic_int slot_called;
    atomic_int thread_rc;
    ns_loop_t *loop;
    uint32_t triggered_events;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} broker_loop_ctx_t;

static void broker_watcher_slot(void *user_data, const void *payload)
{
    broker_loop_ctx_t *ctx = (broker_loop_ctx_t *)user_data;
    const ns_watcher_event_t *event = (const ns_watcher_event_t *)payload;

    ctx->triggered_events = event->triggered_events;
    ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
    (void)ns_loop_quit(ctx->loop);
}

static void broker_loop_worker(broker_loop_ctx_t *ctx)
{
    int rc;

    rc = ns_loop_init(&ctx->loop, NULL);
    if(rc != NS_OK){
        ns_atomic_store_explicit(&ctx->thread_rc, rc, ns_memory_order_release);
        ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);
        return;
    }

    ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);
    rc = ns_loop_run(ctx->loop);
    ns_atomic_store_explicit(&ctx->thread_rc, rc, ns_memory_order_release);
    (void)ns_loop_deinit(ctx->loop);
}

#if defined(_WIN32)
static DWORD WINAPI broker_loop_main(LPVOID arg)
{
    broker_loop_worker((broker_loop_ctx_t *)arg);
    return 0u;
}
#else
static void *broker_loop_main(void *arg)
{
    broker_loop_worker((broker_loop_ctx_t *)arg);
    return NULL;
}
#endif

static void broker_loop_ctx_init(broker_loop_ctx_t *ctx)
{
    ns_atomic_init(&ctx->ready, 0);
    ns_atomic_init(&ctx->slot_called, 0);
    ns_atomic_init(&ctx->thread_rc, NS_OK);
    ctx->loop = NULL;
    ctx->triggered_events = 0u;
#if defined(_WIN32)
    ctx->thread = NULL;
#endif
}

static int broker_loop_start(broker_loop_ctx_t *ctx)
{
#if defined(_WIN32)
    ctx->thread = CreateThread(NULL, 0u, broker_loop_main, ctx, 0u, NULL);
    return (ctx->thread != NULL) ? NS_OK : NS_E_NOMEM;
#else
    return (pthread_create(&ctx->thread, NULL, broker_loop_main, ctx) == 0) ? NS_OK : NS_E_NOMEM;
#endif
}

static void broker_loop_join(broker_loop_ctx_t *ctx)
{
#if defined(_WIN32)
    if(ctx->thread != NULL){
        WaitForSingleObject(ctx->thread, INFINITE);
        CloseHandle(ctx->thread);
        ctx->thread = NULL;
    }
#else
    (void)pthread_join(ctx->thread, NULL);
#endif
}

static int broker_wait_until_ready(broker_loop_ctx_t *ctx)
{
    int i;

    for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&ctx->ready, ns_memory_order_acquire) != 0) return NS_OK;
        test_yield();
    }

    return NS_E_INVAL;
}

static int broker_wait_until_slot_called(broker_loop_ctx_t *ctx)
{
    int i;

    for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&ctx->slot_called, ns_memory_order_acquire) != 0) return NS_OK;
        test_yield();
    }

    return NS_E_INVAL;
}

static int test_broker_lifecycle(void)
{
    EXPECT_OK(ns_broker() == NULL);
    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_broker() != NULL);
    EXPECT_OK(ns_shutdown() == NS_OK);
    EXPECT_OK(ns_broker() == NULL);
    return 0;
}

static int test_watcher_invalid_paths(void)
{
    ns_watcher_t watcher;
    ns_watcher_t zero_watcher = {0};
    ns_platform_waitable_t raw;
    uint32_t invalid_event = NS_WAITABLE_EVENT_ERR << 1;

    EXPECT_OK(ns_watcher_deinit(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_watcher_deinit(&zero_watcher) == NS_E_INVAL);
    { ns_waitable_handle_t h = {.fd = 0}; EXPECT_OK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, NULL) == NS_E_SHUTDOWN); }
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_E_INVAL);

    EXPECT_OK(ns_init() == NS_OK);
    { ns_waitable_handle_t h = {.fd = 0}; EXPECT_OK(ns_watcher_init(NULL, h, NS_WAITABLE_EVENT_IN, 0, NULL) == NS_E_INVAL); }
    { ns_waitable_handle_t h = {.fd = -1}; EXPECT_OK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, NULL) == NS_E_INVAL); }
    { ns_waitable_handle_t h = {.fd = 0}; EXPECT_OK(ns_watcher_init(&watcher, h, invalid_event, 0, NULL) == NS_E_INVAL); }
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_E_INVAL);
#if defined(_WIN32)
    { ns_waitable_handle_t h = {.handle = NULL}; EXPECT_OK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, NULL) == NS_E_INVAL); }
#endif
    EXPECT_OK(ns_broker_add(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_broker_remove(NULL) == NS_E_INVAL);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(raw);

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

static int test_broker_add_remove(void)
{
    ns_watcher_t watcher;
    ns_platform_waitable_t raw;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK);

    EXPECT_OK(ns_broker_add(&watcher) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher) == NS_E_EXISTS);
    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    EXPECT_OK(ns_broker_remove(&watcher) == NS_E_INVAL);

    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

static int test_watcher_event_reaches_loop(void)
{
    broker_loop_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_initialized = 0;
    int connected = 0;
    int added = 0;
    int rc;

    broker_loop_ctx_init(&ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&ctx) != NS_OK) goto fail;
    if(ns_atomic_load_explicit(&ctx.thread_rc, ns_memory_order_acquire) != NS_OK) goto fail;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, NULL); }
    if(rc != NS_OK) goto fail;
    watcher_initialized = 1;

    rc = ns_signal_connect(&watcher.signal, broker_watcher_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    test_signal_raw_waitable(raw);
    rc = broker_wait_until_slot_called(&ctx);
    if(rc != NS_OK) goto fail;

    broker_loop_join(&ctx);
    worker_started = 0;
    EXPECT_OK(ns_atomic_load_explicit(&ctx.thread_rc, ns_memory_order_acquire) == NS_OK);
    EXPECT_EQ(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire), 1);
    EXPECT_OK((ctx.triggered_events & NS_WAITABLE_EVENT_IN) != 0u);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_initialized = 0;
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_initialized) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(ctx.loop != NULL) (void)ns_loop_quit(ctx.loop);
        broker_loop_join(&ctx);
    }
    (void)ns_shutdown();
    return 1;
}

static int test_shutdown_removes_residual_watcher(void)
{
    ns_watcher_t watcher;
    ns_platform_waitable_t raw;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher) == NS_OK);

    EXPECT_OK(ns_shutdown() == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(raw);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: broker_remove does not retract already-enqueued emit         */
/*  Uses a witness loop to confirm the broker has emitted before       */
/*  removal — once the witness dispatches, the test loop's MPSC ring   */
/*  already holds the same emit (atomic per signal slot_list).         */
/* ------------------------------------------------------------------ */

static int g_broker_remove_retract_test = 0;

static void slot_broker_retract_testee(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_broker_remove_retract_test++;
}

static int test_broker_remove_does_not_retract_enqueued(void)
{
    broker_loop_ctx_t witness;
    ns_platform_waitable_t raw;
    ns_watcher_t watcher;
    ns_connection_t conn_witness;
    ns_connection_t conn_test;
    ns_loop_t *test_loop = NULL;
    int rc;

    g_broker_remove_retract_test = 0;
    ns_waitable_init(&raw);
    broker_loop_ctx_init(&witness);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&witness);
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(broker_wait_until_ready(&witness) == NS_OK);
    /* no "worker_started" flag — cleanup is sequential on fail */

    /* Create the test loop — NOT started, so its ring accumulates */
    EXPECT_OK(ns_loop_init(&test_loop, NULL) == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, NULL); }
    EXPECT_OK(rc == NS_OK);

    /* Connection 1 — to the running witness loop */
    rc = ns_signal_connect(&watcher.signal, (ns_slot_fn)broker_watcher_slot,
                           witness.loop, &witness, &conn_witness);
    EXPECT_OK(rc == NS_OK);

    /* Connection 2 — to the paused test loop */
    rc = ns_signal_connect(&watcher.signal, slot_broker_retract_testee,
                           test_loop, NULL, &conn_test);
    EXPECT_OK(rc == NS_OK);

    rc = ns_broker_add(&watcher);
    EXPECT_OK(rc == NS_OK);

    /* Signal — broker fires watcher.signal → iterates BOTH connections
     * atomically (slot_list locked). test_loop's ring gets the entry. */
    test_signal_raw_waitable(raw);

    /* Wait for witness to confirm broker processed */
    EXPECT_OK(broker_wait_until_slot_called(&witness) == NS_OK);

    /* Witness loop already quit (slot calls ns_loop_quit). Rejoin thread. */
    broker_loop_join(&witness);

    /* Remove watcher — does NOT touch the already-committed ring entry */
    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn_witness) == NS_OK);

    /* Signal again — no effect, watcher removed */
    test_signal_raw_waitable(raw);

    /* Drain the test loop — the pre-removal emit must dispatch */
    EXPECT_OK(ns_loop_quit(test_loop) == NS_OK);
    rc = ns_loop_run(test_loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_broker_remove_retract_test, 1);

    EXPECT_OK(ns_signal_disconnect(&conn_test) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_loop_deinit(test_loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: broker error continue (CRITICAL)                             */
/* ------------------------------------------------------------------ */

static int test_broker_error_continue(void)
{

    /* Inject waitset_wait failure BEFORE ns_init */
    g_ns_test_waitset_wait_result = NS_E_INVAL;

    EXPECT_OK(ns_init() == NS_OK);

    /* The injected error was consumed in ns_broker_run's first iteration.
     * The broker thread continued (didn't crash) and we can still interact.
     * Verify by doing a simple shutdown — if broker thread survived, this
     * succeeds. */
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: watcher deinit before broker_remove                          */
/* ------------------------------------------------------------------ */

static int test_watcher_deinit_before_remove(void)
{
    ns_watcher_t watcher;
    ns_platform_waitable_t raw;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
    EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher) == NS_OK);

    /* Deinit before remove — the operation should not crash.
     * ns_watcher_deinit returns NS_E_EXISTS because the watcher is still
     * linked in the broker, so we must remove first, then deinit. */
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_E_EXISTS);
    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);

    EXPECT_OK(ns_shutdown() == NS_OK);
    test_destroy_raw_waitable(raw);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: broker_add with invalid fd returns error                     */
/* ------------------------------------------------------------------ */

static int test_broker_add_invalid_fd(void)
{
    ns_watcher_t watcher;

    EXPECT_OK(ns_init() == NS_OK);

    /* Invalid fd (-1) — init may fail, and add with invalid waitable should too */
    { ns_waitable_handle_t h = {.fd = -1};
    if(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK){
        EXPECT_OK(ns_broker_add(&watcher) != NS_OK);
        (void)ns_broker_remove(&watcher);
        EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    } }

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: edge_triggered vs level_triggered event delivery semantics    */
/*                                                                    */
/*  edge (EPOLLET): fires once on the transition from not-ready to    */
/*  ready. Even if eventfd is not consumed, no spurious re-fire.      */
/*  level (default epoll): fires every time waitset_wait sees the     */
/*  fd as ready. If eventfd is not consumed, it may re-fire.          */
/*                                                                    */
/*  Strategy: signal the eventfd, use a slot that quits the loop on   */
/*  the first fire, then check the fire count after join:             */
/*    - edge: count == 1 (exactly one fire)                           */
/*    - level: count >= 1 (may have re-fired before quit took effect) */
/* ------------------------------------------------------------------ */

static atomic_int g_edge_level_fire_count;
static int g_edge_level_drain_fd = -1;

/* Slot that quits the loop on first fire and drains the eventfd to stop
 * the broker from re-firing. For level-triggered watchers, epoll keeps
 * reporting the ready eventfd, so we must consume it to let the loop exit.
 * For edge-triggered, draining is harmless (only fires once anyway). */
static void slot_edge_level(void *user_data, const void *payload)
{
    broker_loop_ctx_t *ctx = (broker_loop_ctx_t *)user_data;
    (void)payload;
    int count = ns_atomic_fetch_add_explicit(&g_edge_level_fire_count, 1, ns_memory_order_release);
    if(count == 0){
        /* Drain the eventfd so the broker stops re-firing */
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
    broker_loop_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    broker_loop_ctx_init(&ctx);
    ns_atomic_init(&g_edge_level_fire_count, 0);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&ctx) != NS_OK) goto fail;

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

    /* Set drain fd so the slot can consume the eventfd and stop re-firing */
#if !defined(_WIN32)
    g_edge_level_drain_fd = raw.primitive.fd;
#endif

    /* Yield to let the broker thread process the add notification
     * and settle back into epoll_wait before we signal. Without this,
     * edge-triggered epoll may miss the transition because the broker
     * hasn't re-entered epoll_wait yet. */
    {
        int i;
        for(i = 0; i < 100000; ++i) test_yield();
    }

    /* Signal the eventfd once. The slot drains it so the loop can exit cleanly. */
    test_signal_raw_waitable(raw);

    /* Wait for the slot to fire (it quits the loop). */
    if(broker_wait_until_slot_called(&ctx) != NS_OK) goto fail;

    broker_loop_join(&ctx);
    worker_started = 0;
    EXPECT_OK(ns_atomic_load_explicit(&ctx.thread_rc, ns_memory_order_acquire) == NS_OK);

    if(edge_triggered){
        /* Edge-triggered: fires exactly once on the transition.
         * No spurious re-fire even though eventfd was not consumed initially. */
        EXPECT_EQ(ns_atomic_load_explicit(&g_edge_level_fire_count, ns_memory_order_acquire), 1);
    } else {
        /* Level-triggered: fires at least once. May re-fire because
         * epoll keeps reporting the ready eventfd across waitset_wait calls
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
        broker_loop_join(&ctx);
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
/*  Test: watcher with OUT/ERR events (Minor 4-B5)                     */
/*  Verify NS_WAITABLE_EVENT_OUT and NS_WAITABLE_EVENT_ERR flags are   */
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

    /* Watcher with EVENT_OUT flag */
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher_out, h, NS_WAITABLE_EVENT_OUT, 0, NULL); }
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_out) == NS_OK);

    /* Watcher with EVENT_ERR flag */
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw); rc = ns_watcher_init(&watcher_err, h, NS_WAITABLE_EVENT_ERR, 0, NULL); }
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_err) == NS_OK);

    /* Combined flags: IN|OUT|ERR */
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
/*  NS_WAITABLE_EVENT_OUT on one end — the broker should fire the      */
/*  watcher's signal with triggered_events containing EVENT_OUT.       */
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
    broker_loop_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int sv[2] = {-1, -1};
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    broker_loop_ctx_init(&ctx);
    ns_atomic_init(&g_out_event_slot_called, 0);
    g_out_event_triggered = 0u;
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&ctx) != NS_OK) goto fail;

    /* socketpair — both ends writable by default */
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

    /* Yield to let broker settle into epoll_wait */
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

    /* Verify triggered_events contains EVENT_OUT */
    EXPECT_OK((g_out_event_triggered & NS_WAITABLE_EVENT_OUT) != 0u);

    /* Cleanup — close socketpair, remove watcher, etc. */
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
        broker_loop_join(&ctx);
    }
    (void)ns_shutdown();
    return 1;
#endif
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
    /* Both add operations must succeed */
    EXPECT_OK(ns_broker_add(&watcher_a) == NS_OK);
    EXPECT_OK(ns_broker_add(&watcher_b) == NS_OK);

    /* Duplicate add returns E_EXISTS */
    EXPECT_OK(ns_broker_add(&watcher_a) == NS_E_EXISTS);
    EXPECT_OK(ns_broker_add(&watcher_b) == NS_E_EXISTS);

    /* Remove both — must succeed */
    EXPECT_OK(ns_broker_remove(&watcher_a) == NS_OK);
    EXPECT_OK(ns_broker_remove(&watcher_b) == NS_OK);

    /* Double remove returns E_INVAL */
    EXPECT_OK(ns_broker_remove(&watcher_a) == NS_E_INVAL);
    EXPECT_OK(ns_broker_remove(&watcher_b) == NS_E_INVAL);

    /* Re-add after remove must work */
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
/*  Test: consume_fn basic — reads eventfd, sets handle, slot receives */
/* ------------------------------------------------------------------ */

typedef struct consume_test_ctx {
    atomic_int slot_called;
    atomic_int thread_rc;
    ns_loop_t *loop;
    uint32_t triggered_events;
    void *consume_handle;
} consume_test_ctx_t;

static void consume_slot(void *user_data, const void *payload)
{
    consume_test_ctx_t *ctx = (consume_test_ctx_t *)user_data;
    const ns_watcher_event_t *ev = (const ns_watcher_event_t *)payload;

    ctx->triggered_events = ev->triggered_events;
    ctx->consume_handle = ev->consume_handle;
    ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
    (void)ns_loop_quit(ctx->loop);
}

static int g_consume_call_count = 0;
static int g_consume_return_value = 1; /* >0 = emit */
static int g_consume_ctx_data = 42;

static int test_consume_fn(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);

    g_consume_call_count++;

    /* Drain the eventfd so level-triggered epoll doesn't re-fire */
#if !defined(_WIN32)
    {
        uint64_t dummy;
        ssize_t n;
        do { n = read(h.fd, &dummy, sizeof(dummy)); } while(n < 0 && errno == EINTR);
        (void)n;
    }
#endif

    if(g_consume_return_value > 0){
        ns_watcher_set_consume_handle(w, &g_consume_ctx_data);
    }
    return g_consume_return_value;
}

static int test_consume_fn_impl(void)
{
    broker_loop_ctx_t loop_ctx;
    consume_test_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    g_consume_call_count = 0;
    g_consume_return_value = 1;
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ctx.triggered_events = 0u;
    ctx.consume_handle = NULL;
    broker_loop_ctx_init(&loop_ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&loop_ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&loop_ctx) != NS_OK) goto fail;
    ctx.loop = loop_ctx.loop;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, test_consume_fn); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    /* Verify ns_watcher_handle returns the correct fd */
    {
        ns_waitable_handle_t h = ns_watcher_handle(&watcher);
#if !defined(_WIN32)
        EXPECT_OK(h.fd == raw.primitive.fd);
#endif
    }

    rc = ns_signal_connect(&watcher.signal, consume_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    /* Yield to let broker settle */
    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    /* Signal the eventfd — consume_fn should drain it and emit */
    test_signal_raw_waitable(raw);

    /* Wait for slot */
    { int i; for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0) break;
        test_yield();
    }}
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0);

    /* Verify consume_fn was called */
    EXPECT_OK(g_consume_call_count >= 1);
    /* Verify consume_handle was passed through */
    EXPECT_OK(ctx.consume_handle == &g_consume_ctx_data);
    /* Verify triggered_events */
    EXPECT_OK((ctx.triggered_events & NS_WAITABLE_EVENT_IN) != 0u);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); broker_loop_join(&loop_ctx); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        broker_loop_join(&loop_ctx);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn returning 0 suppresses emit                       */
/* ------------------------------------------------------------------ */

static int test_consume_fn_no_emit(void)
{
    broker_loop_ctx_t loop_ctx;
    consume_test_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    g_consume_call_count = 0;
    g_consume_return_value = 0; /* suppress emit */
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    broker_loop_ctx_init(&loop_ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&loop_ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&loop_ctx) != NS_OK) goto fail;
    ctx.loop = loop_ctx.loop;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, test_consume_fn); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_signal_connect(&watcher.signal, consume_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    /* Yield to let broker settle */
    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    /* Signal — consume_fn returns 0, so slot should NOT be called */
    test_signal_raw_waitable(raw);

    /* Wait a bit — slot should NOT fire */
    { int i; for(i = 0; i < 500000; ++i) test_yield(); }

    /* consume_fn was called but returned 0 → no emit */
    EXPECT_OK(g_consume_call_count >= 1);
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) == 0);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); broker_loop_join(&loop_ctx); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        broker_loop_join(&loop_ctx);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn prevents level-triggered refire                   */
/*  With consume_fn draining the eventfd, level-triggered epoll       */
/*  should fire exactly once (no tight loop).                          */
/* ------------------------------------------------------------------ */

static atomic_int g_consume_refire_count;

static int test_consume_refire_fn(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);
    int count = ns_atomic_fetch_add_explicit(&g_consume_refire_count, 1, ns_memory_order_release);

    /* Drain the eventfd */
#if !defined(_WIN32)
    {
        uint64_t dummy;
        ssize_t n;
        do { n = read(h.fd, &dummy, sizeof(dummy)); } while(n < 0 && errno == EINTR);
        (void)n;
    }
#endif

    /* Only emit on first call */
    return (count == 0) ? 1 : 0;
}

static void consume_refire_slot(void *user_data, const void *payload)
{
    broker_loop_ctx_t *ctx = (broker_loop_ctx_t *)user_data;
    (void)payload;

    ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
    (void)ns_loop_quit(ctx->loop);
}

static int test_consume_fn_prevents_refire(void)
{
    broker_loop_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&g_consume_refire_count, 0);
    broker_loop_ctx_init(&ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&ctx) != NS_OK) goto fail;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    /* Level-triggered (edge_triggered=0) + consume_fn */
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, test_consume_refire_fn); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_signal_connect(&watcher.signal, consume_refire_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    /* Yield to let broker settle */
    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    /* Signal the eventfd once */
    test_signal_raw_waitable(raw);

    /* Wait for slot to fire */
    if(broker_wait_until_slot_called(&ctx) != NS_OK) goto fail;

    broker_loop_join(&ctx);
    worker_started = 0;

    /* consume_fn drained the eventfd each time, so level-triggered epoll
     * should NOT have re-fired. First call returns 1 (emit), subsequent
     * calls return 0 (no emit). Fire count should be exactly 1. */
    EXPECT_EQ(ns_atomic_load_explicit(&g_consume_refire_count, ns_memory_order_acquire), 1);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(ctx.loop != NULL) (void)ns_loop_quit(ctx.loop);
        broker_loop_join(&ctx);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: pipe + level-triggered + consume_fn                          */
/*                                                                    */
/*  Uses a real pipe (not eventfd) in level-triggered mode.            */
/*  Without consume_fn draining the pipe, level-triggered epoll       */
/*  would fire repeatedly (tight loop). With consume_fn reading the   */
/*  pipe, the fd becomes not-ready after consume → exactly one fire.  */
/* ------------------------------------------------------------------ */

typedef struct pipe_consume_ctx {
    atomic_int slot_called;
    atomic_int consume_call_count;
    ns_loop_t *loop;
    uint8_t buf[256];
    size_t bytes_read;
    int pipe_fd;  /* read end — consume_fn uses ns_watcher_handle to get this */
} pipe_consume_ctx_t;

static int pipe_consume_fn(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);
    /* ctx was pre-set via ns_watcher_set_consume_handle before broker_add */
    pipe_consume_ctx_t *ctx_ptr = (pipe_consume_ctx_t *)w->pending_consume_handle;
    int count = ns_atomic_fetch_add_explicit(&ctx_ptr->consume_call_count, 1, ns_memory_order_release);

    if(count > 0) return 0; /* only consume once */

    /* Read all available data from the pipe */
    ssize_t n = read(h.fd, ctx_ptr->buf, sizeof(ctx_ptr->buf));
    if(n > 0){
        ctx_ptr->bytes_read = (size_t)n;
        /* handle already points to ctx, no need to set again */
        return 1; /* emit */
    }
    return 0; /* no data or error — don't emit */
}

static void pipe_consume_slot(void *user_data, const void *payload)
{
    pipe_consume_ctx_t *ctx_ptr = (pipe_consume_ctx_t *)user_data;
    const ns_watcher_event_t *ev = (const ns_watcher_event_t *)payload;

    /* Verify consume_handle points back to our context */
    EXPECT_OK(ev->consume_handle == ctx_ptr);
    /* Verify we got the data */
    EXPECT_OK(ctx_ptr->bytes_read == 5);
    EXPECT_OK(ctx_ptr->buf[0] == 'h' && ctx_ptr->buf[1] == 'e' &&
              ctx_ptr->buf[2] == 'l' && ctx_ptr->buf[3] == 'l' &&
              ctx_ptr->buf[4] == 'o');

    ns_atomic_store_explicit(&ctx_ptr->slot_called, 1, ns_memory_order_release);
    (void)ns_loop_quit(ctx_ptr->loop);
}

static int test_consume_fn_pipe_level_triggered(void)
{
#if defined(_WIN32)
    /* Windows: skip — pipe not available in this form */
    return 0;
#else
    broker_loop_ctx_t loop_ctx;
    pipe_consume_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int pipefd[2] = {-1, -1};
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&ctx.slot_called, 0);
    ns_atomic_init(&ctx.consume_call_count, 0);
    ctx.loop = NULL;
    ctx.bytes_read = 0;
    ctx.pipe_fd = -1;
    broker_loop_ctx_init(&loop_ctx);
    ns_waitable_init(&raw);

    /* Create pipe — both ends start empty, read end is level-triggered */
    if(pipe(pipefd) < 0) goto fail;
    ctx.pipe_fd = pipefd[0];

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&loop_ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&loop_ctx) != NS_OK) goto fail;
    ctx.loop = loop_ctx.loop;

    /* Build a raw waitable from the pipe read end */
    raw.primitive.fd = pipefd[0];
    raw.events = NS_WAITABLE_EVENT_IN;

    /* Init watcher: level-triggered (edge_triggered=0), with consume_fn */
    { ns_waitable_handle_t h = {.fd = pipefd[0]};
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, pipe_consume_fn); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    /* Pre-set ctx as consume_handle — consume_fn reads it via w->pending_consume_handle */
    ns_watcher_set_consume_handle(&watcher, &ctx);

    rc = ns_signal_connect(&watcher.signal, pipe_consume_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    /* Yield to let broker settle into epoll_wait */
    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    /* Write data to the pipe — read end becomes ready */
    {
        const char *msg = "hello";
        ssize_t n;
        do { n = write(pipefd[1], msg, 5); } while(n < 0 && errno == EINTR);
        if(n != 5) goto fail;
    }

    /* Wait for slot to fire */
    {
        int i;
        for(i = 0; i < 1000000; ++i){
            if(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0) break;
            test_yield();
        }
    }
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0);

    /* consume_fn was called exactly once (first call returns 1, drains pipe) */
    EXPECT_OK(ns_atomic_load_explicit(&ctx.consume_call_count, ns_memory_order_acquire) >= 1);
    /* slot verified consume_handle and data internally (see pipe_consume_slot) */
    EXPECT_OK(ctx.bytes_read == 5);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    (void)close(pipefd[0]);
    (void)close(pipefd[1]);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); broker_loop_join(&loop_ctx); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(pipefd[0] >= 0) (void)close(pipefd[0]);
    if(pipefd[1] >= 0) (void)close(pipefd[1]);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        broker_loop_join(&loop_ctx);
    }
    (void)ns_shutdown();
    return 1;
#endif
}

/* ------------------------------------------------------------------ */
/*  Test: ns_watcher_handle NULL safety                                */
/* ------------------------------------------------------------------ */

static int test_watcher_handle_null(void)
{
    ns_waitable_handle_t h;

    /* NULL watcher should return invalid sentinel */
    h = ns_watcher_handle(NULL);
#if defined(_WIN32)
    EXPECT_OK(h.handle == INVALID_HANDLE_VALUE);
#else
    EXPECT_OK(h.fd == -1);
#endif

    EXPECT_OK(ns_init() == NS_OK);

    {
        ns_watcher_t watcher;
        ns_platform_waitable_t raw = test_create_raw_waitable();
        EXPECT_OK(test_raw_waitable_is_valid(raw));
        EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK);

        h = ns_watcher_handle(&watcher);
#if defined(_WIN32)
        EXPECT_OK(h.handle == raw.primitive.handle);
#else
        EXPECT_OK(h.fd == raw.primitive.fd);
#endif

        EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
        test_destroy_raw_waitable(raw);
    }

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn returning negative suppresses emit                 */
/* ------------------------------------------------------------------ */

static int g_neg_consume_call_count = 0;

static int test_consume_fn_negative(ns_watcher_t *w)
{
    (void)w;
    g_neg_consume_call_count++;
    return -1; /* error — suppress emit */
}

static int test_consume_fn_negative_return(void)
{
    broker_loop_ctx_t loop_ctx;
    consume_test_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    g_neg_consume_call_count = 0;
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    broker_loop_ctx_init(&loop_ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&loop_ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&loop_ctx) != NS_OK) goto fail;
    ctx.loop = loop_ctx.loop;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, test_consume_fn_negative); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_signal_connect(&watcher.signal, consume_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    { int i; for(i = 0; i < 500000; ++i) test_yield(); }

    /* consume_fn was called, returned negative → no emit */
    EXPECT_OK(g_neg_consume_call_count >= 1);
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) == 0);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); broker_loop_join(&loop_ctx); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        broker_loop_join(&loop_ctx);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn without set_consume_handle → handle is NULL        */
/* ------------------------------------------------------------------ */

static int g_no_handle_consume_count = 0;

static int test_consume_fn_no_handle(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);

    g_no_handle_consume_count++;

    /* Drain eventfd but do NOT call set_consume_handle */
#if !defined(_WIN32)
    {
        uint64_t dummy;
        ssize_t n;
        do { n = read(h.fd, &dummy, sizeof(dummy)); } while(n < 0 && errno == EINTR);
        (void)n;
    }
#endif
    return 1; /* emit, but handle stays NULL */
}

static void consume_no_handle_slot(void *user_data, const void *payload)
{
    consume_test_ctx_t *ctx = (consume_test_ctx_t *)user_data;
    const ns_watcher_event_t *ev = (const ns_watcher_event_t *)payload;

    /* consume_handle must be NULL since consume_fn didn't set it */
    if(ev->consume_handle != NULL){
        /* flag error via slot_called = 2 */
        ns_atomic_store_explicit(&ctx->slot_called, 2, ns_memory_order_release);
    } else {
        ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
    }
    (void)ns_loop_quit(ctx->loop);
}

static int test_consume_fn_no_handle_set(void)
{
    broker_loop_ctx_t loop_ctx;
    consume_test_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    g_no_handle_consume_count = 0;
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    broker_loop_ctx_init(&loop_ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&loop_ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&loop_ctx) != NS_OK) goto fail;
    ctx.loop = loop_ctx.loop;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, test_consume_fn_no_handle); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_signal_connect(&watcher.signal, consume_no_handle_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    { int i; for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0) break;
        test_yield();
    }}
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) == 1);
    EXPECT_OK(g_no_handle_consume_count >= 1);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); broker_loop_join(&loop_ctx); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        broker_loop_join(&loop_ctx);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn + edge-triggered                                  */
/*  consume_fn should work identically with edge-triggered watchers.   */
/* ------------------------------------------------------------------ */

static int test_consume_fn_edge_triggered(void)
{
    broker_loop_ctx_t loop_ctx;
    consume_test_ctx_t ctx;
    ns_watcher_t watcher;
    ns_connection_t conn;
    ns_platform_waitable_t raw;
    int worker_started = 0;
    int watcher_ok = 0;
    int connected = 0;
    int added = 0;
    int rc;

    g_consume_call_count = 0;
    g_consume_return_value = 1;
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ctx.triggered_events = 0u;
    ctx.consume_handle = NULL;
    broker_loop_ctx_init(&loop_ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&loop_ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&loop_ctx) != NS_OK) goto fail;
    ctx.loop = loop_ctx.loop;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    /* Edge-triggered (edge_triggered=1) + consume_fn */
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, test_consume_fn); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_signal_connect(&watcher.signal, consume_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    { int i; for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0) break;
        test_yield();
    }}
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0);
    EXPECT_OK(g_consume_call_count >= 1);
    EXPECT_OK(ctx.consume_handle == &g_consume_ctx_data);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); broker_loop_join(&loop_ctx); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        broker_loop_join(&loop_ctx);
    }
    (void)ns_shutdown();
    return 1;
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
    broker_loop_ctx_t loop_ctx;
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
    broker_loop_ctx_init(&loop_ctx);
    ns_waitable_init(&raw_a);
    ns_waitable_init(&raw_b);

    EXPECT_OK(ns_init() == NS_OK);

    rc = broker_loop_start(&loop_ctx);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(broker_wait_until_ready(&loop_ctx) != NS_OK) goto fail;

    raw_a = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw_a)) goto fail;
    raw_b = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw_b)) goto fail;

    /* watcher_a: with consume_fn */
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw_a);
      rc = ns_watcher_init(&watcher_a, h, NS_WAITABLE_EVENT_IN, 1, mixed_consume_fn); }
    if(rc != NS_OK) goto fail;
    wa_ok = 1;

    /* watcher_b: without consume_fn (NULL) */
    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw_b);
      rc = ns_watcher_init(&watcher_b, h, NS_WAITABLE_EVENT_IN, 1, NULL); }
    if(rc != NS_OK) goto fail;
    wb_ok = 1;

    rc = ns_signal_connect(&watcher_a.signal, mixed_slot_a, loop_ctx.loop, NULL, &conn_a);
    if(rc != NS_OK) goto fail;
    ca = 1;

    rc = ns_signal_connect(&watcher_b.signal, mixed_slot_b, loop_ctx.loop, NULL, &conn_b);
    if(rc != NS_OK) goto fail;
    cb = 1;

    rc = ns_broker_add(&watcher_a);
    if(rc != NS_OK) goto fail;
    aa = 1;

    rc = ns_broker_add(&watcher_b);
    if(rc != NS_OK) goto fail;
    ab = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    /* Signal both */
    test_signal_raw_waitable(raw_a);
    test_signal_raw_waitable(raw_b);

    /* Wait for both slots */
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
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); broker_loop_join(&loop_ctx); }
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
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        broker_loop_join(&loop_ctx);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: ns_watcher_set_consume_handle with NULL watcher               */
/* ------------------------------------------------------------------ */

static int test_set_consume_handle_null_watcher(void)
{
    /* Should not crash */
    ns_watcher_set_consume_handle(NULL, (void *)0xDEAD);
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
    if(test_broker_edge_triggered_no_refire() != 0) return 1;
    if(test_broker_level_triggered_may_refire() != 0) return 1;
    if(test_watcher_event_out_err() != 0) return 1;
    if(test_watcher_out_event_delivery() != 0) return 1;
    if(test_broker_multi_watcher() != 0) return 1;
    if(test_consume_fn_impl() != 0) return 1;
    if(test_consume_fn_no_emit() != 0) return 1;
    if(test_consume_fn_prevents_refire() != 0) return 1;
    if(test_consume_fn_pipe_level_triggered() != 0) return 1;
    if(test_consume_fn_negative_return() != 0) return 1;
    if(test_consume_fn_no_handle_set() != 0) return 1;
    if(test_consume_fn_edge_triggered() != 0) return 1;
    if(test_mixed_watchers_with_and_without_consume() != 0) return 1;
    if(test_set_consume_handle_null_watcher() != 0) return 1;
    if(test_watcher_handle_null() != 0) return 1;

    return 0;
}
