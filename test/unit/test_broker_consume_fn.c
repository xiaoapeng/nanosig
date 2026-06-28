/**
 * @file test_broker_consume_fn.c
 * @brief Event broker consume_fn variant tests.
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
    atomic_int thread_rc;
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
        ns_atomic_store_explicit(&ctx->thread_rc, rc, ns_memory_order_release);
        test_thread_signal_failed(&g_broker_thread, rc);
        return rc;
    }

    test_thread_signal_ready(&g_broker_thread);
    rc = ns_loop_run(ctx->loop);
    ns_atomic_store_explicit(&ctx->thread_rc, rc, ns_memory_order_release);
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
/*  Shared consume test context                                         */
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
static int g_consume_return_value = 1;
static int g_consume_ctx_data = 42;

static int test_consume_fn(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);

    g_consume_call_count++;

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

/* ------------------------------------------------------------------ */
/*  Test: consume_fn basic — reads eventfd, sets handle, slot receives */
/* ------------------------------------------------------------------ */

static int test_consume_fn_impl(void)
{
    broker_test_ctx_t loop_ctx;
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
    ns_atomic_init(&loop_ctx.slot_called, 0);
    loop_ctx.loop = NULL;
    loop_ctx.triggered_events = 0u;
    ns_atomic_init(&loop_ctx.thread_rc, 0);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    test_thread_init(&g_broker_thread, broker_worker_entry, &loop_ctx);
    rc = test_thread_start(&g_broker_thread);
    if(rc != 0) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;
    ctx.loop = loop_ctx.loop;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, test_consume_fn); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

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

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    { int i; for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0) break;
        test_yield();
    }}
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0);

    EXPECT_OK(g_consume_call_count >= 1);
    EXPECT_OK(ctx.consume_handle == &g_consume_ctx_data);
    EXPECT_OK((ctx.triggered_events & NS_WAITABLE_EVENT_IN) != 0u);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); test_thread_join(&g_broker_thread); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn returning 0 suppresses emit                        */
/* ------------------------------------------------------------------ */

static int test_consume_fn_no_emit(void)
{
    broker_test_ctx_t loop_ctx;
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
    g_consume_return_value = 0;
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ns_atomic_init(&loop_ctx.slot_called, 0);
    loop_ctx.loop = NULL;
    loop_ctx.triggered_events = 0u;
    ns_atomic_init(&loop_ctx.thread_rc, 0);
    test_thread_init(&g_broker_thread, broker_worker_entry, &loop_ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = test_thread_start(&g_broker_thread);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;
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

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    { int i; for(i = 0; i < 500000; ++i) test_yield(); }

    EXPECT_OK(g_consume_call_count >= 1);
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) == 0);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); test_thread_join(&g_broker_thread); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn prevents level-triggered refire                    */
/*  With consume_fn draining the eventfd, level-triggered epoll fires   */
/*  exactly once (no tight loop).                                       */
/* ------------------------------------------------------------------ */

static atomic_int g_consume_refire_count;

static int test_consume_refire_fn(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);
    int count = ns_atomic_fetch_add_explicit(&g_consume_refire_count, 1, ns_memory_order_release);

#if !defined(_WIN32)
    {
        uint64_t dummy;
        ssize_t n;
        do { n = read(h.fd, &dummy, sizeof(dummy)); } while(n < 0 && errno == EINTR);
        (void)n;
    }
#endif

    return (count == 0) ? 1 : 0;
}

static void consume_refire_slot(void *user_data, const void *payload)
{
    broker_test_ctx_t *ctx = (broker_test_ctx_t *)user_data;
    (void)payload;

    ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
    (void)ns_loop_quit(ctx->loop);
}

static int test_consume_fn_prevents_refire(void)
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

    ns_atomic_init(&g_consume_refire_count, 0);
    ns_atomic_init(&ctx.slot_called, 0);
    ctx.loop = NULL;
    ctx.triggered_events = 0u;
    ns_atomic_init(&ctx.thread_rc, 0);
    test_thread_init(&g_broker_thread, broker_worker_entry, &ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = test_thread_start(&g_broker_thread);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

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

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    if(broker_wait_until_slot_called(&ctx) != NS_OK) goto fail;

    test_thread_join(&g_broker_thread);
    worker_started = 0;

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
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: pipe + level-triggered + consume_fn                          */
/*  Uses a real pipe (not eventfd) in level-triggered mode. Without     */
/*  consume_fn draining the pipe, level-triggered epoll would fire      */
/*  repeatedly (tight loop). With consume_fn reading the pipe, the fd   */
/*  becomes not-ready after consume, leading to exactly one fire.       */
/* ------------------------------------------------------------------ */

typedef struct pipe_consume_ctx {
    atomic_int slot_called;
    atomic_int consume_call_count;
    ns_loop_t *loop;
    uint8_t buf[256];
    size_t bytes_read;
    int pipe_fd;
} pipe_consume_ctx_t;

static int pipe_consume_fn(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);
    pipe_consume_ctx_t *ctx_ptr = (pipe_consume_ctx_t *)w->pending_consume_handle;
    int count = ns_atomic_fetch_add_explicit(&ctx_ptr->consume_call_count, 1, ns_memory_order_release);

    if(count > 0) return 0;

    ssize_t n = read(h.fd, ctx_ptr->buf, sizeof(ctx_ptr->buf));
    if(n > 0){
        ctx_ptr->bytes_read = (size_t)n;
        return 1;
    }
    return 0;
}

static void pipe_consume_slot(void *user_data, const void *payload)
{
    pipe_consume_ctx_t *ctx_ptr = (pipe_consume_ctx_t *)user_data;
    const ns_watcher_event_t *ev = (const ns_watcher_event_t *)payload;

    EXPECT_OK(ev->consume_handle == ctx_ptr);
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
    return 0;
#else
    broker_test_ctx_t loop_ctx;
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
    ns_atomic_init(&loop_ctx.slot_called, 0);
    loop_ctx.loop = NULL;
    loop_ctx.triggered_events = 0u;
    ns_atomic_init(&loop_ctx.thread_rc, 0);
    test_thread_init(&g_broker_thread, broker_worker_entry, &loop_ctx);
    ns_waitable_init(&raw);

    if(pipe(pipefd) < 0) goto fail;
    ctx.pipe_fd = pipefd[0];

    EXPECT_OK(ns_init() == NS_OK);

    rc = test_thread_start(&g_broker_thread);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;
    ctx.loop = loop_ctx.loop;

    raw.primitive.fd = pipefd[0];
    raw.events = NS_WAITABLE_EVENT_IN;

    { ns_waitable_handle_t h = {.fd = pipefd[0]};
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, pipe_consume_fn); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    ns_watcher_set_consume_handle(&watcher, &ctx);

    rc = ns_signal_connect(&watcher.signal, pipe_consume_slot, ctx.loop, &ctx, &conn);
    if(rc != NS_OK) goto fail;
    connected = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    {
        const char *msg = "hello";
        ssize_t n;
        do { n = write(pipefd[1], msg, 5); } while(n < 0 && errno == EINTR);
        if(n != 5) goto fail;
    }

    {
        int i;
        for(i = 0; i < 1000000; ++i){
            if(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0) break;
            test_yield();
        }
    }
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) != 0);

    EXPECT_OK(ns_atomic_load_explicit(&ctx.consume_call_count, ns_memory_order_acquire) >= 1);
    EXPECT_OK(ctx.bytes_read == 5);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    (void)close(pipefd[0]);
    (void)close(pipefd[1]);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); test_thread_join(&g_broker_thread); }
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
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
#endif
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn returning negative suppresses emit                 */
/* ------------------------------------------------------------------ */

static int g_neg_consume_call_count = 0;

static int test_consume_fn_negative(ns_watcher_t *w)
{
    (void)w;
    g_neg_consume_call_count++;
    return -1;
}

static int test_consume_fn_negative_return(void)
{
    broker_test_ctx_t loop_ctx;
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
    ns_atomic_init(&loop_ctx.slot_called, 0);
    loop_ctx.loop = NULL;
    loop_ctx.triggered_events = 0u;
    ns_atomic_init(&loop_ctx.thread_rc, 0);
    test_thread_init(&g_broker_thread, broker_worker_entry, &loop_ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = test_thread_start(&g_broker_thread);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;
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

    EXPECT_OK(g_neg_consume_call_count >= 1);
    EXPECT_OK(ns_atomic_load_explicit(&ctx.slot_called, ns_memory_order_acquire) == 0);

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    test_destroy_raw_waitable(raw);
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); test_thread_join(&g_broker_thread); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn without set_consume_handle -> handle is NULL       */
/* ------------------------------------------------------------------ */

static int g_no_handle_consume_count = 0;

static int test_consume_fn_no_handle(ns_watcher_t *w)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);

    g_no_handle_consume_count++;

#if !defined(_WIN32)
    {
        uint64_t dummy;
        ssize_t n;
        do { n = read(h.fd, &dummy, sizeof(dummy)); } while(n < 0 && errno == EINTR);
        (void)n;
    }
#endif
    return 1;
}

static void consume_no_handle_slot(void *user_data, const void *payload)
{
    consume_test_ctx_t *ctx = (consume_test_ctx_t *)user_data;
    const ns_watcher_event_t *ev = (const ns_watcher_event_t *)payload;

    if(ev->consume_handle != NULL){
        ns_atomic_store_explicit(&ctx->slot_called, 2, ns_memory_order_release);
    } else {
        ns_atomic_store_explicit(&ctx->slot_called, 1, ns_memory_order_release);
    }
    (void)ns_loop_quit(ctx->loop);
}

static int test_consume_fn_no_handle_set(void)
{
    broker_test_ctx_t loop_ctx;
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
    ns_atomic_init(&loop_ctx.slot_called, 0);
    loop_ctx.loop = NULL;
    loop_ctx.triggered_events = 0u;
    ns_atomic_init(&loop_ctx.thread_rc, 0);
    test_thread_init(&g_broker_thread, broker_worker_entry, &loop_ctx);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    rc = test_thread_start(&g_broker_thread);
    if(rc != NS_OK) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;
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
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); test_thread_join(&g_broker_thread); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: consume_fn + edge-triggered                                   */
/*  consume_fn works identically with edge-triggered watchers.          */
/* ------------------------------------------------------------------ */

static int test_consume_fn_edge_triggered(void)
{
    broker_test_ctx_t loop_ctx;
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
    ns_atomic_init(&loop_ctx.slot_called, 0);
    loop_ctx.loop = NULL;
    loop_ctx.triggered_events = 0u;
    ns_atomic_init(&loop_ctx.thread_rc, 0);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    test_thread_init(&g_broker_thread, broker_worker_entry, &loop_ctx);
    rc = test_thread_start(&g_broker_thread);
    if(rc != 0) goto fail;
    worker_started = 1;
    if(test_thread_wait_ready(&g_broker_thread) != 0) goto fail;
    ctx.loop = loop_ctx.loop;

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

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
    if(worker_started){ (void)ns_loop_quit(loop_ctx.loop); test_thread_join(&g_broker_thread); }
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(connected) (void)ns_signal_disconnect(&conn);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(test_raw_waitable_is_valid(raw)) test_destroy_raw_waitable(raw);
    if(worker_started){
        if(loop_ctx.loop != NULL) (void)ns_loop_quit(loop_ctx.loop);
        test_thread_join(&g_broker_thread);
    }
    (void)ns_shutdown();
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Test: ns_watcher_set_consume_handle with NULL watcher               */
/* ------------------------------------------------------------------ */

static int test_set_consume_handle_null_watcher(void)
{
    ns_watcher_set_consume_handle(NULL, (void *)0xDEAD);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: ns_watcher_handle NULL safety                                */
/* ------------------------------------------------------------------ */

static int test_watcher_handle_null(void)
{
    ns_waitable_handle_t h;

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
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    if(test_consume_fn_impl() != 0) return 1;
    if(test_consume_fn_no_emit() != 0) return 1;
    if(test_consume_fn_prevents_refire() != 0) return 1;
    if(test_consume_fn_pipe_level_triggered() != 0) return 1;
    if(test_consume_fn_negative_return() != 0) return 1;
    if(test_consume_fn_no_handle_set() != 0) return 1;
    if(test_consume_fn_edge_triggered() != 0) return 1;
    if(test_set_consume_handle_null_watcher() != 0) return 1;
    if(test_watcher_handle_null() != 0) return 1;

    return 0;
}