/**
 * @file test_signal.c
 * @brief Signal/slot runtime unit tests.
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <nanosig/nanosig.h>

#include "test_macros.h"
#include "test_thread.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#else
#error "test_signal requires pthreads or Win32 threads"
#endif

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

static void test_yield(void)
{
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

/**
 * Helper: emit then quit then run.
 *
 * Same-thread tests emit before entering ns_loop_run. After dispatching
 * the pending call the loop would block on wakeup_wait forever, so we
 * request quit first; ns_loop_run drains the queue then exits.
 */
static int emit_quit_run(ns_signal_t *sig, const void *payload, ns_loop_t *loop)
{
    int rc;

    rc = ns_signal_emit_raw(sig, payload, sig->payload_size);
    if(rc != NS_OK) return rc;

    rc = ns_loop_quit(loop);
    if(rc != NS_OK) return rc;

    rc = ns_loop_run(loop);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Test: no-payload signal, same thread                               */
/* ------------------------------------------------------------------ */

static int g_no_payload_called = 0;

static void slot_no_payload(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_no_payload_called++;
}

static int test_no_payload_same_thread(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    int rc;

    g_no_payload_called = 0;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "test-no-payload");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_no_payload, loop, NULL, &conn);
    EXPECT_OK(rc == NS_OK);

    rc = emit_quit_run(&sig, NULL, loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_no_payload_called, 1);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: payload signal, same thread                                  */
/* ------------------------------------------------------------------ */

typedef struct test_payload {
    int value;
} test_payload_t;

static int g_payload_called = 0;
static int g_payload_value = 0;

static void slot_with_payload(void *user_data, const void *payload)
{
    const test_payload_t *p = (const test_payload_t *)payload;
    (void)user_data;
    g_payload_called++;
    g_payload_value = p->value;
}

static int test_payload_same_thread(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    test_payload_t payload;
    int rc;

    g_payload_called = 0;
    g_payload_value = 0;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, sizeof(test_payload_t), 0u, "test-payload");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_with_payload, loop, (void *)0xDEAD, &conn);
    EXPECT_OK(rc == NS_OK);

    payload.value = 42;
    rc = emit_quit_run(&sig, &payload, loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_payload_called, 1);
    EXPECT_EQ(g_payload_value, 42);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: cross-thread emit                                            */
/* ------------------------------------------------------------------ */

typedef struct cross_thread_ctx {
    atomic_int slot_called;
    ns_loop_t *loop;
} cross_thread_ctx_t;

static test_thread_t g_cross_thread;

static int cross_thread_entry(void *arg)
{
    cross_thread_ctx_t *ctx = (cross_thread_ctx_t *)arg;
    int rc;

    rc = ns_loop_init(&ctx->loop, NULL);
    if(rc != NS_OK){
        test_thread_signal_failed(&g_cross_thread, rc);
        return rc;
    }

    test_thread_signal_ready(&g_cross_thread);

    rc = ns_loop_run(ctx->loop);

    ns_loop_deinit(ctx->loop);
    return rc;
}

static void slot_cross_thread(void *user_data, const void *payload)
{
    cross_thread_ctx_t *ctx = (cross_thread_ctx_t *)user_data;
    (void)payload;
    ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
}

static int test_cross_thread_emit(void)
{
    cross_thread_ctx_t ctx;
    ns_signal_t sig;
    ns_connection_t conn;
    int rc;

    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);

    /* Start worker thread that creates its own loop and runs it */
    test_thread_init(&g_cross_thread, cross_thread_entry, &ctx);
    EXPECT_OK(test_thread_start(&g_cross_thread) == 0);

    /* Wait for worker to be ready */
    EXPECT_OK(test_thread_wait_ready(&g_cross_thread) == 0);

    /* Connect signal to worker's loop from main thread */
    rc = ns_signal_init_raw(&sig, 0u, 0u, "cross-thread-sig");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_cross_thread, ctx.loop, &ctx, &conn);
    EXPECT_OK(rc == NS_OK);

    /* Emit from main thread - should enqueue to worker's loop */
    rc = ns_signal_emit_raw(&sig, NULL, 0u);
    EXPECT_OK(rc == NS_OK);

    /* Wait for slot to be called */
    while(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) == 0){
        test_yield();
    }

    /* Quit worker loop after slot was dispatched */
    EXPECT_OK(ns_loop_quit(ctx.loop) == NS_OK);

    test_thread_join(&g_cross_thread);

    EXPECT_OK(g_cross_thread.rc == 0);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: disconnect stops future calls                                */
/* ------------------------------------------------------------------ */

static int g_disconnect_called = 0;

static void slot_disconnect(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_disconnect_called++;
}

static int test_disconnect_stops_future(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    int rc;

    g_disconnect_called = 0;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "disconnect-test");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_disconnect, loop, NULL, &conn);
    EXPECT_OK(rc == NS_OK);

    /* Disconnect before any emit */
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);

    /* Emit after disconnect - slot should not be called */
    rc = emit_quit_run(&sig, NULL, loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_disconnect_called, 0);

    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: disconnect_all removes all connections                       */
/* ------------------------------------------------------------------ */

static int g_multi_called = 0;

static void slot_multi(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_multi_called++;
}

static int test_disconnect_all(void)
{
    ns_signal_t sig;
    ns_connection_t conn1;
    ns_connection_t conn2;
    ns_connection_t conn3;
    ns_loop_t *loop = NULL;
    int rc;

    g_multi_called = 0;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "disconnect-all-test");
    EXPECT_OK(rc == NS_OK);

    EXPECT_OK(ns_signal_connect(&sig, slot_multi, loop, NULL, &conn1) == NS_OK);
    EXPECT_OK(ns_signal_connect(&sig, slot_multi, loop, NULL, &conn2) == NS_OK);
    EXPECT_OK(ns_signal_connect(&sig, slot_multi, loop, NULL, &conn3) == NS_OK);

    /* Disconnect all */
    EXPECT_OK(ns_signal_disconnect_all(&sig) == NS_OK);

    /* Emit - no slots should fire */
    rc = emit_quit_run(&sig, NULL, loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_multi_called, 0);

    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: emit before run (queued, dispatched on run)                  */
/* ------------------------------------------------------------------ */

static int g_queue_first_called = 0;

static void slot_queue_first(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_queue_first_called++;
}

static int test_emit_before_run(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    int rc;

    g_queue_first_called = 0;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "queue-first-test");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_queue_first, loop, NULL, &conn);
    EXPECT_OK(rc == NS_OK);

    /* Emit before run, then quit so run exits after draining */
    rc = ns_signal_emit_raw(&sig, NULL, 0u);
    EXPECT_OK(rc == NS_OK);

    EXPECT_OK(ns_loop_quit(loop) == NS_OK);

    rc = ns_loop_run(loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_queue_first_called, 1);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: multiple connections receive emit                            */
/* ------------------------------------------------------------------ */

static int g_multi_slot_a = 0;
static int g_multi_slot_b = 0;

static void slot_a(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_multi_slot_a++;
}

static void slot_b(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_multi_slot_b++;
}

static int test_multiple_connections(void)
{
    ns_signal_t sig;
    ns_connection_t conn_a;
    ns_connection_t conn_b;
    ns_loop_t *loop = NULL;
    int rc;

    g_multi_slot_a = 0;
    g_multi_slot_b = 0;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "multi-conn-test");
    EXPECT_OK(rc == NS_OK);

    EXPECT_OK(ns_signal_connect(&sig, slot_a, loop, NULL, &conn_a) == NS_OK);
    EXPECT_OK(ns_signal_connect(&sig, slot_b, loop, NULL, &conn_b) == NS_OK);

    rc = emit_quit_run(&sig, NULL, loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_multi_slot_a, 1);
    EXPECT_EQ(g_multi_slot_b, 1);

    EXPECT_OK(ns_signal_disconnect(&conn_a) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn_b) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: explicit init is required                                    */
/* ------------------------------------------------------------------ */

static int test_uninitialized_signal_rejected(void)
{
    ns_signal_t sig = {0};
    ns_connection_t conn;
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    EXPECT_OK(ns_signal_connect(&sig, slot_no_payload, loop, NULL, &conn) == NS_E_INVAL);
    EXPECT_OK(ns_signal_emit_raw(&sig, NULL, 0u) == NS_E_INVAL);
    EXPECT_OK(ns_signal_disconnect_all(&sig) == NS_E_INVAL);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);

    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: concurrent connect + emit (thread-safe slot_list)            */
/* ------------------------------------------------------------------ */

typedef struct concurrent_ctx {
    atomic_int done;
    atomic_int slot_called;
    ns_loop_t *loop;
    ns_signal_t *signal;
} concurrent_ctx_t;

static test_thread_t g_concurrent_thread;

static int concurrent_entry(void *arg)
{
    concurrent_ctx_t *ctx = (concurrent_ctx_t *)arg;
    int rc;

    rc = ns_loop_init(&ctx->loop, NULL);
    if(rc != NS_OK){
        test_thread_signal_failed(&g_concurrent_thread, rc);
        return rc;
    }

    test_thread_signal_ready(&g_concurrent_thread);

    rc = ns_loop_run(ctx->loop);

    ns_loop_deinit(ctx->loop);
    return rc;
}

static void slot_concurrent(void *user_data, const void *payload)
{
    concurrent_ctx_t *ctx = (concurrent_ctx_t *)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&ctx->slot_called, 1, ns_memory_order_relaxed);
}

static int test_concurrent_connect_emit(void)
{
    concurrent_ctx_t ctx;
    ns_signal_t sig;
    ns_connection_t conns[8];
    int i;
    int rc;

    ns_atomic_init(&ctx.done, 0);
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ctx.signal = &sig;

    EXPECT_OK(ns_init() == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "concurrent-test");
    EXPECT_OK(rc == NS_OK);

    /* Start worker thread that runs a loop */
    test_thread_init(&g_concurrent_thread, concurrent_entry, &ctx);
    EXPECT_OK(test_thread_start(&g_concurrent_thread) == 0);

    /* Wait for worker to be ready */
    EXPECT_OK(test_thread_wait_ready(&g_concurrent_thread) == 0);

    /* Concurrent connect + emit: main thread connects/disconnects while emitting */
    for(i = 0; i < 8; i++){
        rc = ns_signal_connect(&sig, slot_concurrent, ctx.loop, &ctx, &conns[i]);
        EXPECT_OK(rc == NS_OK);

        rc = ns_signal_emit_raw(&sig, NULL, 0u);
        EXPECT_OK(rc == NS_OK);

        rc = ns_signal_disconnect(&conns[i]);
        EXPECT_OK(rc == NS_OK);
    }

    /* Quit worker loop after concurrent operations */
    EXPECT_OK(ns_loop_quit(ctx.loop) == NS_OK);

    test_thread_join(&g_concurrent_thread);

    /* At least some slots should have been called */
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) > 0);

    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: disconnect does not cancel already-enqueued slot             */
/* ------------------------------------------------------------------ */

static int g_disconnect_does_not_retract = 0;

static void slot_disconnect_no_retract(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_disconnect_does_not_retract++;
}

static int test_disconnect_does_not_retract_enqueued(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    int rc;

    g_disconnect_does_not_retract = 0;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "disconnect-no-retract");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_disconnect_no_retract, loop, NULL, &conn);
    EXPECT_OK(rc == NS_OK);

    /* Emit BEFORE disconnect — slot_call is already committed to the ring */
    rc = ns_signal_emit_raw(&sig, NULL, 0u);
    EXPECT_OK(rc == NS_OK);

    /* Disconnect AFTER emit — must NOT retract the already-queued call */
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);

    /* Run loop — enqueued slot_call must still dispatch */
    EXPECT_OK(ns_loop_quit(loop) == NS_OK);
    rc = ns_loop_run(loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_disconnect_does_not_retract, 1);

    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: emit with wrong payload size is rejected                     */
/* ------------------------------------------------------------------ */

static int test_emit_wrong_payload_size(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    int rc;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    /* Signal with 4-byte payload */
    rc = ns_signal_init_raw(&sig, 4u, 0u, "wrong-size");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_no_payload, loop, NULL, &conn);
    EXPECT_OK(rc == NS_OK);

    /* emit_raw with size=8, but signal was init'd with 4 — should reject */
    rc = ns_signal_emit_raw(&sig, NULL, 8u);
    EXPECT_EQ(rc, NS_E_INVAL);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: connect with NULL target_loop is rejected                     */
/* ------------------------------------------------------------------ */

static int test_connect_null_loop(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    int rc;

    EXPECT_OK(ns_init() == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "null-loop");
    EXPECT_OK(rc == NS_OK);

    /* NULL target_loop must be rejected */
    rc = ns_signal_connect(&sig, slot_no_payload, NULL, NULL, &conn);
    EXPECT_EQ(rc, NS_E_INVAL);

    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: deinit with active connections                                */
/* ------------------------------------------------------------------ */

static int g_deinit_with_conns_called = 0;

static void slot_deinit_with_conns(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    g_deinit_with_conns_called++;
}

static int test_deinit_with_connections(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    int rc;

    g_deinit_with_conns_called = 0;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "deinit-with-conns");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_deinit_with_conns, loop, NULL, &conn);
    EXPECT_OK(rc == NS_OK);

    /* Deinit while connection still active — implementation must handle gracefully */
    rc = ns_signal_deinit_raw(&sig);
    EXPECT_OK(rc == NS_OK);

    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: signal as struct member, cross-thread emit                    */
/* ------------------------------------------------------------------ */

typedef struct struct_signal_ctx {
    ns_signal_t sig;
    atomic_int slot_called;
    ns_loop_t *loop;
} struct_signal_ctx_t;

static test_thread_t g_struct_member_thread;

static int struct_member_entry(void *arg)
{
    struct_signal_ctx_t *ctx = (struct_signal_ctx_t *)arg;
    int rc;

    rc = ns_loop_init(&ctx->loop, NULL);
    if(rc != NS_OK){
        test_thread_signal_failed(&g_struct_member_thread, rc);
        return rc;
    }

    test_thread_signal_ready(&g_struct_member_thread);
    (void)ns_loop_run(ctx->loop);
    ns_loop_deinit(ctx->loop);
    return rc;
}

static void slot_struct_member(void *user_data, const void *payload)
{
    struct_signal_ctx_t *ctx = (struct_signal_ctx_t *)user_data;
    (void)payload;
    ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
}

static int test_signal_struct_member_cross_thread(void)
{
    struct_signal_ctx_t ctx;

    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);

    /* signal embedded in a struct */
    EXPECT_OK(ns_signal_init_raw(&ctx.sig, 0u, 0u, "struct-member") == NS_OK);

    /* Start worker thread */
    test_thread_init(&g_struct_member_thread, struct_member_entry, &ctx);
    EXPECT_OK(test_thread_start(&g_struct_member_thread) == 0);

    EXPECT_OK(test_thread_wait_ready(&g_struct_member_thread) == 0);

    {
        ns_connection_t conn;
        int rc;

        rc = ns_signal_connect(&ctx.sig, slot_struct_member, ctx.loop, &ctx, &conn);
        EXPECT_OK(rc == NS_OK);

        rc = ns_signal_emit_raw(&ctx.sig, NULL, 0u);
        EXPECT_OK(rc == NS_OK);

        while(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) == 0){
            test_yield();
        }

        EXPECT_OK(ns_loop_quit(ctx.loop) == NS_OK);

        test_thread_join(&g_struct_member_thread);

        EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    }

    EXPECT_OK(ns_signal_deinit_raw(&ctx.sig) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    if(test_no_payload_same_thread() != 0) return 1;
    if(test_payload_same_thread() != 0) return 1;
    if(test_cross_thread_emit() != 0) return 1;
    if(test_disconnect_stops_future() != 0) return 1;
    if(test_disconnect_all() != 0) return 1;
    if(test_emit_before_run() != 0) return 1;
    if(test_multiple_connections() != 0) return 1;
    if(test_uninitialized_signal_rejected() != 0) return 1;
    if(test_concurrent_connect_emit() != 0) return 1;
    if(test_disconnect_does_not_retract_enqueued() != 0) return 1;
    if(test_emit_wrong_payload_size() != 0) return 1;
    if(test_connect_null_loop() != 0) return 1;
    if(test_deinit_with_connections() != 0) return 1;
    if(test_signal_struct_member_cross_thread() != 0) return 1;

    return 0;
}
