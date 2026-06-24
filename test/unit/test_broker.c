/**
 * @file test_broker.c
 * @brief Event broker runtime tests.
 * @date 2026-06-14
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig.h>
#include "test_macros.h"
#include "test_helpers.h"
#include "nanosig/internal/ns_broker.h"

#include <stdio.h>
#include <stdint.h>

#if !defined(_WIN32)
#include <pthread.h>
#include <sched.h>
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

    rc = ns_loop_create(&ctx->loop, NULL);
    if(rc != NS_OK){
        ns_atomic_store_explicit(&ctx->thread_rc, rc, ns_memory_order_release);
        ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);
        return;
    }

    ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);
    rc = ns_loop_run(ctx->loop);
    ns_atomic_store_explicit(&ctx->thread_rc, rc, ns_memory_order_release);
    (void)ns_loop_destroy(ctx->loop);
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
    EXPECT_OK(ns_watcher_init_fd(&watcher, 0, NS_WAITABLE_EVENT_IN, 0) == NS_E_SHUTDOWN);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_E_INVAL);

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_watcher_init_fd(NULL, 0, NS_WAITABLE_EVENT_IN, 0) == NS_E_INVAL);
    EXPECT_OK(ns_watcher_init_fd(&watcher, -1, NS_WAITABLE_EVENT_IN, 0) == NS_E_INVAL);
    EXPECT_OK(ns_watcher_init_fd(&watcher, 0, invalid_event, 0) == NS_E_INVAL);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_E_INVAL);
    EXPECT_OK(ns_watcher_init_handle(&watcher, NULL, NS_WAITABLE_EVENT_IN, 0) == NS_E_INVAL);
    EXPECT_OK(ns_broker_add(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_broker_remove(NULL) == NS_E_INVAL);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
#if defined(_WIN32)
    EXPECT_OK(ns_watcher_init_handle(&watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#else
    EXPECT_OK(ns_watcher_init_fd(&watcher, raw.fd, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#endif
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
#if defined(_WIN32)
    EXPECT_OK(ns_watcher_init_handle(&watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#else
    EXPECT_OK(ns_watcher_init_fd(&watcher, raw.fd, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#endif

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
#if defined(_WIN32)
    rc = ns_watcher_init_handle(&watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0);
#else
    rc = ns_watcher_init_fd(&watcher, raw.fd, NS_WAITABLE_EVENT_IN, 1);
#endif
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
#if defined(_WIN32)
    EXPECT_OK(ns_watcher_init_handle(&watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#else
    EXPECT_OK(ns_watcher_init_fd(&watcher, raw.fd, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#endif
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
    EXPECT_OK(ns_loop_create(&test_loop, NULL) == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
#if defined(_WIN32)
    rc = ns_watcher_init_handle(&watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0);
#else
    rc = ns_watcher_init_fd(&watcher, raw.fd, NS_WAITABLE_EVENT_IN, 1);
#endif
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
    EXPECT_OK(ns_loop_destroy(test_loop) == NS_OK);
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
#if defined(_WIN32)
    EXPECT_OK(ns_watcher_init_handle(&watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#else
    EXPECT_OK(ns_watcher_init_fd(&watcher, raw.fd, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#endif
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

    /* Invalid fd (-1) — init_fd may fail, and add with invalid waitable should too */
    if(ns_watcher_init_fd(&watcher, -1, NS_WAITABLE_EVENT_IN, 0) == NS_OK){
        EXPECT_OK(ns_broker_add(&watcher) != NS_OK);
        (void)ns_broker_remove(&watcher);
        EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    }

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: edge_triggered vs level_triggered watchers (Minor 4-B4)      */
/*  Verify both edge and level modes can create and register watchers  */
/*  that work correctly. The deep semantic difference (edge vs level   */
/*  retrigger) is platform-dependent and validated in platform tests.   */
/* ------------------------------------------------------------------ */

static int test_broker_edge_vs_level(void)
{
    ns_watcher_t edge_watcher, level_watcher;
    ns_platform_waitable_t raw;
    int rc;

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));

    /* Edge-triggered watcher: create and register */
#if defined(_WIN32)
    rc = ns_watcher_init_handle(&edge_watcher, raw.handle, NS_WAITABLE_EVENT_IN, 1);
#else
    rc = ns_watcher_init_fd(&edge_watcher, raw.fd, NS_WAITABLE_EVENT_IN, 1);
#endif
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_broker_add(&edge_watcher) == NS_OK);
    EXPECT_OK(ns_broker_remove(&edge_watcher) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&edge_watcher) == NS_OK);

    /* Level-triggered watcher: create and register */
#if defined(_WIN32)
    rc = ns_watcher_init_handle(&level_watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0);
#else
    rc = ns_watcher_init_fd(&level_watcher, raw.fd, NS_WAITABLE_EVENT_IN, 0);
#endif
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_broker_add(&level_watcher) == NS_OK);
    EXPECT_OK(ns_broker_remove(&level_watcher) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&level_watcher) == NS_OK);

    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
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
#if defined(_WIN32)
    rc = ns_watcher_init_handle(&watcher_out, raw.handle, NS_WAITABLE_EVENT_OUT, 0);
#else
    rc = ns_watcher_init_fd(&watcher_out, raw.fd, NS_WAITABLE_EVENT_OUT, 0);
#endif
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_out) == NS_OK);

    /* Watcher with EVENT_ERR flag */
#if defined(_WIN32)
    rc = ns_watcher_init_handle(&watcher_err, raw.handle, NS_WAITABLE_EVENT_ERR, 0);
#else
    rc = ns_watcher_init_fd(&watcher_err, raw.fd, NS_WAITABLE_EVENT_ERR, 0);
#endif
    EXPECT_OK(rc == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher_err) == NS_OK);

    /* Combined flags: IN|OUT|ERR */
    {
        ns_watcher_t watcher_all;
#if defined(_WIN32)
        rc = ns_watcher_init_handle(&watcher_all, raw.handle,
                                    NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR, 0);
#else
        rc = ns_watcher_init_fd(&watcher_all, raw.fd,
                                NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR, 0);
#endif
        EXPECT_OK(rc == NS_OK);
        EXPECT_OK(ns_watcher_deinit(&watcher_all) == NS_OK);
    }

    test_destroy_raw_waitable(raw);
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
    if(test_broker_edge_vs_level() != 0) return 1;
    if(test_watcher_event_out_err() != 0) return 1;

    return 0;
}
