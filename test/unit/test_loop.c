/**
 * @file test_loop.c
 * @brief Loop lifecycle unit tests.
 * @date 2026-05-24
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig.h>

#include "test_macros.h"

#include <stdio.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#else
#error "test_loop requires pthreads or Win32 threads"
#endif

typedef struct loop_thread_ctx {
    atomic_int ready;
    atomic_int thread_failed;
    atomic_int failure_code;
    atomic_int failure_line;
    ns_loop_t *loop;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} loop_thread_ctx_t;

static void test_yield(void)
{
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

static int test_invalid_args_and_lifecycle(void)
{
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_loop_quit(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_loop_run(NULL) == NS_E_INVAL);
    EXPECT_OK(ns_loop_deinit(NULL) == NS_E_INVAL);

    EXPECT_OK(ns_is_initialized() == 0);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_E_SHUTDOWN);
    EXPECT_OK(ns_shutdown() == NS_OK);

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_is_initialized() != 0);
    EXPECT_OK(ns_init() == NS_E_EXISTS);
    EXPECT_OK(ns_shutdown() == NS_OK);
    EXPECT_OK(ns_is_initialized() == 0);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

static int test_same_thread_binding(void)
{
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    ns_loop_config_t invalid_cfg = NS_LOOP_CONFIG_DEFAULT();
    ns_loop_t *loop = NULL;

    cfg.debug_name = "main-loop";
    invalid_cfg.flags = 1u;

    EXPECT_OK(cfg.queue_byte_capacity == NS_CAPACITY_1024);
    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, &invalid_cfg) == NS_E_INVAL);
    EXPECT_OK(ns_loop_init(&loop, &cfg) == NS_OK);
    EXPECT_OK(loop != NULL);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

static void loop_thread_run(loop_thread_ctx_t *ctx)
{
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    int rc = NS_OK;

    cfg.debug_name = "worker-loop";

    rc = ns_loop_init(&ctx->loop, &cfg);
    if(rc != NS_OK){
        ns_atomic_store_explicit(&ctx->failure_line, __LINE__, ns_memory_order_relaxed);
        goto out;
    }

    ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);

    rc = ns_loop_run(ctx->loop);
    if(rc != NS_OK){
        ns_atomic_store_explicit(&ctx->failure_line, __LINE__, ns_memory_order_relaxed);
        goto out;
    }

out:
    if(ctx->loop != NULL){
        int destroy_rc = ns_loop_deinit(ctx->loop);
        if((rc == NS_OK) && (destroy_rc != NS_OK)){
            rc = destroy_rc;
            ns_atomic_store_explicit(&ctx->failure_line, __LINE__, ns_memory_order_relaxed);
        }
        ctx->loop = NULL;
    }

    ns_atomic_store_explicit(&ctx->failure_code, rc, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&ctx->thread_failed, (rc == NS_OK) ? 0 : 1, ns_memory_order_release);
}

#if defined(_WIN32)
static DWORD WINAPI loop_thread_main(LPVOID arg)
{
    loop_thread_run((loop_thread_ctx_t *)arg);
    return 0u;
}
#else
static void *loop_thread_main(void *arg)
{
    loop_thread_run((loop_thread_ctx_t *)arg);
    return NULL;
}
#endif

static int loop_thread_start(loop_thread_ctx_t *ctx)
{
#if defined(_WIN32)
    ctx->thread = CreateThread(NULL, 0u, loop_thread_main, ctx, 0u, NULL);
    return ctx->thread != NULL ? 0 : 1;
#else
    return pthread_create(&ctx->thread, NULL, loop_thread_main, ctx) == 0 ? 0 : 1;
#endif
}

static int loop_thread_join(loop_thread_ctx_t *ctx)
{
#if defined(_WIN32)
    DWORD wait_rc = WaitForSingleObject(ctx->thread, INFINITE);
    BOOL close_rc = CloseHandle(ctx->thread);

    return (wait_rc == WAIT_OBJECT_0) && (close_rc != 0) ? 0 : 1;
#else
    return pthread_join(ctx->thread, NULL) == 0 ? 0 : 1;
#endif
}

static int test_cross_thread_quit_and_ownership(void)
{
    loop_thread_ctx_t ctx;

    ctx.loop = NULL;
    ns_atomic_init(&ctx.ready, 0);
    ns_atomic_init(&ctx.thread_failed, 0);
    ns_atomic_init(&ctx.failure_code, NS_OK);
    ns_atomic_init(&ctx.failure_line, 0);

    EXPECT_OK(ns_init() == NS_OK);
    if(expect_true(loop_thread_start(&ctx) == 0) != 0){
        fprintf(stderr, "EXPECT failed at %s:%d: %s\n", __FILE__, __LINE__, "loop_thread_start(&ctx) == 0");
        (void)ns_shutdown();
        return 1;
    }

    while(ns_atomic_load_explicit(&ctx.ready, ns_memory_order_acquire) == 0){
        test_yield();
    }

    EXPECT_OK(ctx.loop != NULL);
    EXPECT_OK(ns_loop_quit(ctx.loop) == NS_OK);
    EXPECT_OK(loop_thread_join(&ctx) == 0);
    if(expect_true(ns_atomic_load_explicit(&ctx.thread_failed, ns_memory_order_acquire) == 0) != 0){
        fprintf(
            stderr,
            "worker thread failed: rc=%d line=%d\n",
            ns_atomic_load_explicit(&ctx.failure_code, ns_memory_order_relaxed),
            ns_atomic_load_explicit(&ctx.failure_line, ns_memory_order_relaxed));
        return 1;
    }
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  async loop tests                                                   */
/* ------------------------------------------------------------------ */

/** ns_loop_start → ns_loop_stop 基本生命周期 */
static int test_async_start_stop(void)
{
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_loop_start(loop) == NS_OK);
    EXPECT_OK(ns_loop_stop(loop) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/** 重复 start 应返回 NS_E_BUSY */
static int test_async_repeat_start(void)
{
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_loop_start(loop) == NS_OK);
    EXPECT_OK(ns_loop_start(loop) == NS_E_BUSY);
    EXPECT_OK(ns_loop_stop(loop) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/** stop 未 start 的 loop 应返回 NS_E_INVAL */
static int test_async_stop_without_start(void)
{
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_loop_stop(loop) == NS_E_INVAL);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/** start 后 run 应返回 NS_E_BUSY */
static int test_async_run_while_started(void)
{
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_loop_start(loop) == NS_OK);
    EXPECT_OK(ns_loop_run(loop) == NS_E_BUSY);
    EXPECT_OK(ns_loop_stop(loop) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/** start 后 destroy 应返回 NS_E_BUSY */
static int test_async_destroy_while_started(void)
{
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_loop_start(loop) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_E_BUSY);
    EXPECT_OK(ns_loop_stop(loop) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/** start → stop → start → stop 循环使用 */
static int test_async_twice(void)
{
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    EXPECT_OK(ns_loop_start(loop) == NS_OK);
    EXPECT_OK(ns_loop_stop(loop) == NS_OK);

    EXPECT_OK(ns_loop_start(loop) == NS_OK);
    EXPECT_OK(ns_loop_stop(loop) == NS_OK);

    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

static atomic_int g_slot_called;

static void on_slot(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;
    (void)user_data;

    ns_atomic_store_explicit(&g_slot_called, 1, ns_memory_order_release);
}

/** start → emit → 验证 slot 被调 → stop */
static int test_async_signal_delivery(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
    int i;

    ns_atomic_init(&g_slot_called, 0);

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_signal_init(&sig, ns_no_payload_t) == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    EXPECT_OK(ns_signal_connect_typed(sig, on_slot, ns_no_payload_t, loop, NULL, &conn) == NS_OK);
    EXPECT_OK(ns_loop_start(loop) == NS_OK);

    /* emit，等待 slot 被调 */
    EXPECT_OK(ns_signal_emit(sig, NS_NO_PAYLOAD) == NS_OK);

    for(i = 0; i < 1000000; i++){
        if(ns_atomic_load_explicit(&g_slot_called, ns_memory_order_acquire) != 0) break;
        test_yield();
    }
    EXPECT_OK(ns_atomic_load_explicit(&g_slot_called, ns_memory_order_acquire) != 0);

    EXPECT_OK(ns_loop_stop(loop) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

static void on_quit_slot(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    ns_loop_t *loop = (ns_loop_t *)user_data;

    ns_atomic_store_explicit(&g_slot_called, 1, ns_memory_order_release);
    ns_loop_quit(loop);
}

/** slot 内 quit，stop 应正常 join */
static int test_async_quit_in_slot(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;

    ns_atomic_init(&g_slot_called, 0);

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_signal_init(&sig, ns_no_payload_t) == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_signal_connect_typed(sig, on_quit_slot, ns_no_payload_t, loop, loop, &conn) == NS_OK);
    EXPECT_OK(ns_loop_start(loop) == NS_OK);

    EXPECT_OK(ns_signal_emit(sig, NS_NO_PAYLOAD) == NS_OK);

    /* slot 已 quit，stop 只需 join */
    EXPECT_OK(ns_loop_stop(loop) == NS_OK);
    EXPECT_OK(ns_atomic_load_explicit(&g_slot_called, ns_memory_order_acquire) != 0);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: 2 loops running simultaneously, cross-emit between them      */
/* ------------------------------------------------------------------ */

typedef struct multi_loop_ctx {
    atomic_int ready;
    atomic_int slot_a_called;
    atomic_int slot_b_called;
    atomic_int fail_code;
    atomic_int fail_line;
    ns_loop_t *loop;
    int id;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} multi_loop_ctx_t;

static void slot_multi_loop_a(void *user_data, const void *payload)
{
    multi_loop_ctx_t *ctx = (multi_loop_ctx_t *)user_data;
    (void)payload;
    ns_atomic_store_explicit(&ctx->slot_a_called, 1, ns_memory_order_release);
}

static void slot_multi_loop_b(void *user_data, const void *payload)
{
    multi_loop_ctx_t *ctx = (multi_loop_ctx_t *)user_data;
    (void)payload;
    ns_atomic_store_explicit(&ctx->slot_b_called, 1, ns_memory_order_release);
}

static void multi_loop_worker(multi_loop_ctx_t *ctx)
{
    int rc;

    rc = ns_loop_init(&ctx->loop, NULL);
    if(rc != NS_OK){
        ns_atomic_store_explicit(&ctx->fail_code, rc, ns_memory_order_release);
        ns_atomic_store_explicit(&ctx->fail_line, __LINE__, ns_memory_order_release);
        return;
    }

    ns_atomic_store_explicit(&ctx->ready, 1, ns_memory_order_release);
    rc = ns_loop_run(ctx->loop);
    if(rc != NS_OK){
        ns_atomic_store_explicit(&ctx->fail_code, rc, ns_memory_order_release);
        ns_atomic_store_explicit(&ctx->fail_line, __LINE__, ns_memory_order_release);
    }
    ns_loop_deinit(ctx->loop);
}

#if defined(_WIN32)
static DWORD WINAPI multi_loop_main(LPVOID arg)
{
    multi_loop_worker((multi_loop_ctx_t *)arg);
    return 0u;
}
#else
static void *multi_loop_main(void *arg)
{
    multi_loop_worker((multi_loop_ctx_t *)arg);
    return NULL;
}
#endif

static int test_multi_loop_simultaneous(void)
{
    multi_loop_ctx_t ctx_a;
    multi_loop_ctx_t ctx_b;
    ns_signal_t sig;
    ns_connection_t conn_a;
    ns_connection_t conn_b;
    int rc;

    ns_atomic_init(&ctx_a.ready, 0);
    ns_atomic_init(&ctx_a.slot_a_called, 0);
    ns_atomic_init(&ctx_a.fail_code, NS_OK);
    ns_atomic_init(&ctx_a.fail_line, 0);
    ctx_a.loop = NULL;
    ctx_a.id = 1;

    ns_atomic_init(&ctx_b.ready, 0);
    ns_atomic_init(&ctx_b.slot_b_called, 0);
    ns_atomic_init(&ctx_b.fail_code, NS_OK);
    ns_atomic_init(&ctx_b.fail_line, 0);
    ctx_b.loop = NULL;
    ctx_b.id = 2;

    EXPECT_OK(ns_init() == NS_OK);

    rc = ns_signal_init_raw(&sig, 0u, 0u, "multi-loop");
    EXPECT_OK(rc == NS_OK);

    /* Start loop A thread */
#if defined(_WIN32)
    ctx_a.thread = CreateThread(NULL, 0u, multi_loop_main, &ctx_a, 0u, NULL);
    EXPECT_OK(ctx_a.thread != NULL);
#else
    EXPECT_OK(pthread_create(&ctx_a.thread, NULL, multi_loop_main, &ctx_a) == 0);
#endif
    while(ns_atomic_load_explicit(&ctx_a.ready, ns_memory_order_acquire) == 0){
        test_yield();
    }

    /* Start loop B thread */
#if defined(_WIN32)
    ctx_b.thread = CreateThread(NULL, 0u, multi_loop_main, &ctx_b, 0u, NULL);
    EXPECT_OK(ctx_b.thread != NULL);
#else
    EXPECT_OK(pthread_create(&ctx_b.thread, NULL, multi_loop_main, &ctx_b) == 0);
#endif
    while(ns_atomic_load_explicit(&ctx_b.ready, ns_memory_order_acquire) == 0){
        test_yield();
    }

    /* Connect sig to slot_a on loop A, slot_b on loop B */
    rc = ns_signal_connect(&sig, slot_multi_loop_a, ctx_a.loop, &ctx_a, &conn_a);
    EXPECT_OK(rc == NS_OK);

    rc = ns_signal_connect(&sig, slot_multi_loop_b, ctx_b.loop, &ctx_b, &conn_b);
    EXPECT_OK(rc == NS_OK);

    /* Emit once — both loops should receive the signal */
    rc = ns_signal_emit_raw(&sig, NULL, 0u);
    EXPECT_OK(rc == NS_OK);

    /* Wait for both slots */
    while(ns_atomic_load_explicit(&ctx_a.slot_a_called, ns_memory_order_acquire) == 0 ||
          ns_atomic_load_explicit(&ctx_b.slot_b_called, ns_memory_order_acquire) == 0){
        test_yield();
    }
    EXPECT_OK(ns_atomic_load_explicit(&ctx_a.slot_a_called, ns_memory_order_acquire) != 0);
    EXPECT_OK(ns_atomic_load_explicit(&ctx_b.slot_b_called, ns_memory_order_acquire) != 0);

    /* Quit and join */
    EXPECT_OK(ns_loop_quit(ctx_a.loop) == NS_OK);
    EXPECT_OK(ns_loop_quit(ctx_b.loop) == NS_OK);
#if defined(_WIN32)
    WaitForSingleObject(ctx_a.thread, INFINITE); CloseHandle(ctx_a.thread);
    WaitForSingleObject(ctx_b.thread, INFINITE); CloseHandle(ctx_b.thread);
#else
    pthread_join(ctx_a.thread, NULL);
    pthread_join(ctx_b.thread, NULL);
#endif
    EXPECT_OK(ns_atomic_load_explicit(&ctx_a.fail_code, ns_memory_order_acquire) == NS_OK);
    EXPECT_OK(ns_atomic_load_explicit(&ctx_b.fail_code, ns_memory_order_acquire) == NS_OK);

    EXPECT_OK(ns_signal_disconnect(&conn_a) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&conn_b) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: queue overflow returns E_QUEUE_FULL                          */
/* ------------------------------------------------------------------ */

static void slot_overflow_helper(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
}

static int test_loop_queue_overflow(void)
{
    ns_loop_config_t small_cfg;
    ns_loop_t *loop = NULL;
    ns_signal_t sig;
    ns_connection_t conn;
    int rc;

    /* Use a small ring to trigger overflow */
    small_cfg = NS_LOOP_CONFIG_DEFAULT();
    small_cfg.queue_byte_capacity = NS_CAPACITY_128;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_signal_init_raw(&sig, 0u, 0u, "overflow") == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, &small_cfg) == NS_OK);
    EXPECT_OK(ns_signal_connect(&sig, slot_overflow_helper, loop, NULL, &conn) == NS_OK);

    /* Emit until E_QUEUE_FULL — small ring should fill up quickly */
    {
        int emit_count = 0;
        int got_queue_full = 0;
        for(emit_count = 0; emit_count < 100; emit_count++){
            rc = ns_signal_emit_raw(&sig, NULL, 0u);
            if(rc == NS_E_QUEUE_FULL){
                got_queue_full = 1;
                break;
            }
            EXPECT_OK(rc == NS_OK);
        }
        EXPECT_EQ(got_queue_full, 1);
    }

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: start a loop, emit to it from another thread, stop           */
/* ------------------------------------------------------------------ */

static void slot_async_cross(void *user_data, const ns_no_payload_t *payload)
{
    ns_loop_t *loop = (ns_loop_t *)user_data;
    (void)payload;
    ns_atomic_store_explicit(&g_slot_called, 1, ns_memory_order_release);
    (void)ns_loop_quit(loop);
}

#if defined(_WIN32)
static DWORD WINAPI async_emit_main(LPVOID arg)
{
    ns_signal_t *sig = (ns_signal_t *)arg;
    (void)ns_signal_emit_raw(sig, NULL, 0u);
    return 0u;
}
#else
static void *async_emit_main(void *arg)
{
    ns_signal_t *sig = (ns_signal_t *)arg;
    (void)ns_signal_emit_raw(sig, NULL, 0u);
    return NULL;
}
#endif

static int test_async_cross_thread(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif

    ns_atomic_init(&g_slot_called, 0);

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_signal_init(&sig, ns_no_payload_t) == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_signal_connect_typed(sig, slot_async_cross, ns_no_payload_t, loop, loop, &conn) == NS_OK);

    EXPECT_OK(ns_loop_start(loop) == NS_OK);

    /* Emit from another thread */
#if defined(_WIN32)
    thread = CreateThread(NULL, 0u, async_emit_main, &sig, 0u, NULL);
    EXPECT_OK(thread != NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    EXPECT_OK(pthread_create(&thread, NULL, async_emit_main, &sig) == 0);
    pthread_join(thread, NULL);
#endif

    /* Slot quits loop, stop joins thread */
    EXPECT_OK(ns_loop_stop(loop) == NS_OK);
    EXPECT_OK(ns_atomic_load_explicit(&g_slot_called, ns_memory_order_acquire) != 0);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    return 0;
}

int main(void)
{
    if(test_invalid_args_and_lifecycle() != 0) return 1;
    if(test_same_thread_binding() != 0) return 1;
    if(test_cross_thread_quit_and_ownership() != 0) return 1;
    if(test_async_start_stop() != 0) return 1;
    if(test_async_repeat_start() != 0) return 1;
    if(test_async_stop_without_start() != 0) return 1;
    if(test_async_run_while_started() != 0) return 1;
    if(test_async_destroy_while_started() != 0) return 1;
    if(test_async_twice() != 0) return 1;
    if(test_async_signal_delivery() != 0) return 1;
    if(test_async_quit_in_slot() != 0) return 1;
    if(test_multi_loop_simultaneous() != 0) return 1;
    if(test_loop_queue_overflow() != 0) return 1;
    if(test_async_cross_thread() != 0) return 1;

    return 0;
}
