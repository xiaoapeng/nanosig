/**
 * @file test_signal.c
 * @brief Signal/slot runtime unit tests.
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__unix__) || defined(__linux__)
#include <pthread.h>
#include <sched.h>
#else
#error "test_signal requires pthreads or Win32 threads"
#endif

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

#define EXPECT_OK(expr) \
    do { \
        if(expect_true((expr)) != 0){ \
            fprintf(stderr, "EXPECT failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while(0)

#define EXPECT_EQ(a, b) \
    do { \
        if(expect_true((a) == (b)) != 0){ \
            fprintf(stderr, "EXPECT failed at %s:%d: %s == %s (%d != %d)\n", \
                __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
            return 1; \
        } \
    } while(0)

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
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "test-no-payload");
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_no_payload, loop, NULL, &conn);
    EXPECT_OK(rc == NS_OK);

    rc = emit_quit_run(&sig, NULL, loop);
    EXPECT_OK(rc == NS_OK);

    EXPECT_EQ(g_no_payload_called, 1);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);
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
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);

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
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: cross-thread emit                                            */
/* ------------------------------------------------------------------ */

typedef struct cross_thread_ctx {
    atomic_int ready;
    atomic_int slot_called;
    atomic_int thread_rc;
    ns_loop_t *loop;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} cross_thread_ctx_t;

static void slot_cross_thread(void *user_data, const void *payload)
{
    cross_thread_ctx_t *ctx = (cross_thread_ctx_t *)user_data;
    (void)payload;
    ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
}

static void cross_thread_worker(cross_thread_ctx_t *ctx)
{
    int rc;

    rc = ns_loop_create(&ctx->loop, NULL);
    if(rc != NS_OK){
        ns_atomic_store_explicit(&ctx->thread_rc, rc, ns_memory_order_release);
        return;
    }

    ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);

    rc = ns_loop_run(ctx->loop);

    ns_atomic_store_explicit(&ctx->thread_rc, rc, ns_memory_order_release);
    ns_loop_destroy(ctx->loop);
}

#if defined(_WIN32)
static DWORD WINAPI cross_thread_main(LPVOID arg)
{
    cross_thread_worker((cross_thread_ctx_t *)arg);
    return 0u;
}
#else
static void *cross_thread_main(void *arg)
{
    cross_thread_worker((cross_thread_ctx_t *)arg);
    return NULL;
}
#endif

static int test_cross_thread_emit(void)
{
    cross_thread_ctx_t ctx;
    ns_signal_t sig;
    ns_connection_t conn;
    int rc;

    ns_atomic_init(&ctx.ready, 0);
    ns_atomic_init(&ctx.slot_called, 0);
    ns_atomic_init(&ctx.thread_rc, NS_OK);
    ctx.loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);

    /* Start worker thread that creates its own loop and runs it */
#if defined(_WIN32)
    ctx.thread = CreateThread(NULL, 0u, cross_thread_main, &ctx, 0u, NULL);
    EXPECT_OK(ctx.thread != NULL);
#else
    EXPECT_OK(pthread_create(&ctx.thread, NULL, cross_thread_main, &ctx) == 0);
#endif

    /* Wait for worker to be ready */
    while(ns_atomic_load_explicit(&ctx.ready, ns_memory_order_acquire) == 0){
        test_yield();
    }

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

#if defined(_WIN32)
    WaitForSingleObject(ctx.thread, INFINITE);
    CloseHandle(ctx.thread);
#else
    pthread_join(ctx.thread, NULL);
#endif

    EXPECT_OK(ns_atomic_load_explicit(&ctx.thread_rc, ns_memory_order_acquire) == NS_OK);

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
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);

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
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);
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
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);

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
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);
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
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);

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
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);
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
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);

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
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);
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
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);

    EXPECT_OK(ns_signal_connect(&sig, slot_no_payload, loop, NULL, &conn) == NS_E_INVAL);
    EXPECT_OK(ns_signal_emit_raw(&sig, NULL, 0u) == NS_E_INVAL);
    EXPECT_OK(ns_signal_disconnect_all(&sig) == NS_E_INVAL);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);

    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: concurrent connect + emit (thread-safe slot_list)            */
/* ------------------------------------------------------------------ */

typedef struct concurrent_ctx {
    atomic_int ready;
    atomic_int done;
    atomic_int slot_called;
    ns_loop_t *loop;
    ns_signal_t *signal;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} concurrent_ctx_t;

static void slot_concurrent(void *user_data, const void *payload)
{
    concurrent_ctx_t *ctx = (concurrent_ctx_t *)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&ctx->slot_called, 1, ns_memory_order_relaxed);
}

static void concurrent_worker(concurrent_ctx_t *ctx)
{
    int rc;

    rc = ns_loop_create(&ctx->loop, NULL);
    if(rc != NS_OK){
        ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);
        return;
    }

    ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);

    rc = ns_loop_run(ctx->loop);

    ns_loop_destroy(ctx->loop);
}

#if defined(_WIN32)
static DWORD WINAPI concurrent_main(LPVOID arg)
{
    concurrent_worker((concurrent_ctx_t *)arg);
    return 0u;
}
#else
static void *concurrent_main(void *arg)
{
    concurrent_worker((concurrent_ctx_t *)arg);
    return NULL;
}
#endif

static int test_concurrent_connect_emit(void)
{
    concurrent_ctx_t ctx;
    ns_signal_t sig;
    ns_connection_t conns[8];
    int i;
    int rc;

    ns_atomic_init(&ctx.ready, 0);
    ns_atomic_init(&ctx.done, 0);
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ctx.signal = &sig;

    EXPECT_OK(ns_init() == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "concurrent-test");
    EXPECT_OK(rc == NS_OK);

    /* Start worker thread that runs a loop */
#if defined(_WIN32)
    ctx.thread = CreateThread(NULL, 0u, concurrent_main, &ctx, 0u, NULL);
    EXPECT_OK(ctx.thread != NULL);
#else
    EXPECT_OK(pthread_create(&ctx.thread, NULL, concurrent_main, &ctx) == 0);
#endif

    /* Wait for worker to be ready */
    while(ns_atomic_load_explicit(&ctx.ready, ns_memory_order_acquire) == 0){
        test_yield();
    }

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

#if defined(_WIN32)
    WaitForSingleObject(ctx.thread, INFINITE);
    CloseHandle(ctx.thread);
#else
    pthread_join(ctx.thread, NULL);
#endif

    /* At least some slots should have been called */
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) > 0);

    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
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

    return 0;
}
