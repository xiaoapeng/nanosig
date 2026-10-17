/**
 * @file test_broker_handler.c
 * @brief Event broker watcher handler (callback-only) tests.
 * @date 2026-10-17
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
/*  Basic handler: drains the source and records triggered_events       */
/* ------------------------------------------------------------------ */

static atomic_int g_handler_call_count;
static atomic_uint g_handler_last_events;

static void counting_handler(ns_watcher_t *w, uint32_t triggered_events)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);

    /* 先发布 payload 再发布计数：测试以 call_count 为就绪条件，顺序反了会让
       acquire 读到的计数早于 payload 落地（TSAN 下可复现）。 */
    ns_atomic_store_explicit(&g_handler_last_events, triggered_events, ns_memory_order_release);
    ns_atomic_fetch_add_explicit(&g_handler_call_count, 1, ns_memory_order_release);

#if !defined(_WIN32)
    {
        uint64_t dummy;
        ssize_t n;
        do { n = read(h.fd, &dummy, sizeof(dummy)); } while(n < 0 && errno == EINTR);
        (void)n;
    }
#else
    (void)h;
#endif
}

/* ------------------------------------------------------------------ */
/*  Test: handler is the single readiness outlet and receives events    */
/* ------------------------------------------------------------------ */

static int test_handler_basic(void)
{
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;
    int watcher_ok = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&g_handler_call_count, 0);
    ns_atomic_store_explicit(&g_handler_last_events, 0u, ns_memory_order_relaxed);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, counting_handler); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    {
        ns_waitable_handle_t h = ns_watcher_handle(&watcher);
#if !defined(_WIN32)
        EXPECT_OK(h.fd == raw.primitive.fd);
#endif
        (void)h;
    }

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    { int i; for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&g_handler_call_count, ns_memory_order_acquire) != 0) break;
        test_yield();
    }}
    EXPECT_OK(ns_atomic_load_explicit(&g_handler_call_count, ns_memory_order_acquire) >= 1);
    EXPECT_OK((ns_atomic_load_explicit(&g_handler_last_events, ns_memory_order_acquire) & NS_WAITABLE_EVENT_IN) != 0u);

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

/* ------------------------------------------------------------------ */
/*  Test: level-triggered + handler drains -> exactly one fire (AC-6)    */
/*  Without draining in the handler, a level-triggered source would keep */
/*  firing on every waitset_wait. Draining stops the re-fire.            */
/* ------------------------------------------------------------------ */

static atomic_int g_refire_count;

static void drain_handler(ns_watcher_t *w, uint32_t triggered_events)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);

    (void)triggered_events;
    ns_atomic_fetch_add_explicit(&g_refire_count, 1, ns_memory_order_release);

#if !defined(_WIN32)
    {
        uint64_t dummy;
        ssize_t n;
        do { n = read(h.fd, &dummy, sizeof(dummy)); } while(n < 0 && errno == EINTR);
        (void)n;
    }
#else
    (void)h;
#endif
}

static int test_handler_drain_prevents_refire(void)
{
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;
    int watcher_ok = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&g_refire_count, 0);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, drain_handler); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    { int i; for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&g_refire_count, ns_memory_order_acquire) != 0) break;
        test_yield();
    }}
    EXPECT_OK(ns_atomic_load_explicit(&g_refire_count, ns_memory_order_acquire) >= 1);

    /* Give the broker time to potentially re-fire; a drained source must not. */
    { int i; for(i = 0; i < 500000; ++i) test_yield(); }
    EXPECT_EQ(ns_atomic_load_explicit(&g_refire_count, ns_memory_order_acquire), 1);

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

/* ------------------------------------------------------------------ */
/*  Test: edge-triggered + handler that does not drain fires once        */
/* ------------------------------------------------------------------ */

static atomic_int g_edge_call_count;

static void no_drain_handler(ns_watcher_t *w, uint32_t triggered_events)
{
    (void)w;
    (void)triggered_events;
    ns_atomic_fetch_add_explicit(&g_edge_call_count, 1, ns_memory_order_release);
}

static int test_handler_edge_triggered_no_refire(void)
{
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;
    int watcher_ok = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&g_edge_call_count, 0);
    ns_waitable_init(&raw);

    EXPECT_OK(ns_init() == NS_OK);

    raw = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(raw)) goto fail;

    { ns_waitable_handle_t h = RAW_TO_HANDLE(raw);
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 1, no_drain_handler); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

    rc = ns_broker_add(&watcher);
    if(rc != NS_OK) goto fail;
    added = 1;

    { int i; for(i = 0; i < 100000; ++i) test_yield(); }

    test_signal_raw_waitable(raw);

    { int i; for(i = 0; i < 1000000; ++i){
        if(ns_atomic_load_explicit(&g_edge_call_count, ns_memory_order_acquire) != 0) break;
        test_yield();
    }}
    EXPECT_OK(ns_atomic_load_explicit(&g_edge_call_count, ns_memory_order_acquire) >= 1);

    { int i; for(i = 0; i < 500000; ++i) test_yield(); }
    EXPECT_EQ(ns_atomic_load_explicit(&g_edge_call_count, ns_memory_order_acquire), 1);

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

/* ------------------------------------------------------------------ */
/*  Test: pipe + level-triggered; the handler reads the actual bytes    */
/*  (handler is the only consumer).                                     */
/* ------------------------------------------------------------------ */

typedef struct pipe_handler_ctx {
    atomic_int handler_called;
    uint8_t buf[256];
    size_t bytes_read;
} pipe_handler_ctx_t;

static pipe_handler_ctx_t *g_pipe_ctx;

static void pipe_handler(ns_watcher_t *w, uint32_t triggered_events)
{
    ns_waitable_handle_t h = ns_watcher_handle(w);
    ssize_t n;

    (void)triggered_events;
    do {
        n = read(h.fd, g_pipe_ctx->buf, sizeof(g_pipe_ctx->buf));
    } while(n < 0 && errno == EINTR);

    if(n > 0) g_pipe_ctx->bytes_read = (size_t)n;
    ns_atomic_store_explicit(&g_pipe_ctx->handler_called, 1, ns_memory_order_release);
}

static int test_handler_pipe_level_triggered(void)
{
#if defined(_WIN32)
    return 0;
#else
    ns_watcher_t watcher = {0};
    ns_platform_waitable_t raw;
    pipe_handler_ctx_t ctx;
    int pipefd[2] = {-1, -1};
    int watcher_ok = 0;
    int added = 0;
    int rc;

    ns_atomic_init(&ctx.handler_called, 0);
    ctx.bytes_read = 0;
    g_pipe_ctx = &ctx;
    ns_waitable_init(&raw);

    if(pipe(pipefd) < 0){ g_pipe_ctx = NULL; return 1; }

    EXPECT_OK(ns_init() == NS_OK);

    raw.primitive.fd = pipefd[0];
    raw.events = NS_WAITABLE_EVENT_IN;

    { ns_waitable_handle_t h = {.fd = pipefd[0]};
      rc = ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, pipe_handler); }
    if(rc != NS_OK) goto fail;
    watcher_ok = 1;

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
            if(ns_atomic_load_explicit(&ctx.handler_called, ns_memory_order_acquire) != 0) break;
            test_yield();
        }
    }
    EXPECT_OK(ns_atomic_load_explicit(&ctx.handler_called, ns_memory_order_acquire) != 0);
    EXPECT_OK(ctx.bytes_read == 5);
    EXPECT_OK(ctx.buf[0] == 'h' && ctx.buf[1] == 'e' && ctx.buf[2] == 'l'
              && ctx.buf[3] == 'l' && ctx.buf[4] == 'o');

    EXPECT_OK(ns_broker_remove(&watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_ok = 0;
    (void)close(pipefd[0]);
    (void)close(pipefd[1]);
    g_pipe_ctx = NULL;
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(&watcher);
    if(watcher_ok) (void)ns_watcher_deinit(&watcher);
    if(pipefd[0] >= 0) (void)close(pipefd[0]);
    if(pipefd[1] >= 0) (void)close(pipefd[1]);
    g_pipe_ctx = NULL;
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

    h = ns_watcher_handle(NULL);
#if defined(_WIN32)
    EXPECT_OK(h.handle == INVALID_HANDLE_VALUE);
#else
    EXPECT_OK(h.fd == -1);
#endif

    EXPECT_OK(ns_init() == NS_OK);

    {
        ns_watcher_t watcher = {0};
        ns_platform_waitable_t raw = test_create_raw_waitable();
        EXPECT_OK(test_raw_waitable_is_valid(raw));
        EXPECT_OK(ns_watcher_init(&watcher, RAW_TO_HANDLE(raw), NS_WAITABLE_EVENT_IN, 0, counting_handler) == NS_OK);

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
    if(test_handler_basic() != 0) return 1;
    if(test_handler_drain_prevents_refire() != 0) return 1;
    if(test_handler_edge_triggered_no_refire() != 0) return 1;
    if(test_handler_pipe_level_triggered() != 0) return 1;
    if(test_watcher_handle_null() != 0) return 1;

    return 0;
}
