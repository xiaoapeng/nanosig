/**
 * @file test_broker.c
 * @brief Event broker runtime tests.
 * @date 2026-06-14
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig.h>

#include <stdio.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

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
            fprintf(stderr, "EXPECT failed at %s:%d: %s == %s (%lld != %lld)\n", \
                __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
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

static ns_platform_waitable_t test_create_raw_waitable(void)
{
    ns_platform_waitable_t w = ns_waitable_init();

#if defined(_WIN32)
    w.handle = CreateEventA(NULL, FALSE, FALSE, NULL);
#else
    w.fd = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
#endif
    w.events = NS_WAITABLE_EVENT_IN;
    return w;
}

static int test_raw_waitable_is_valid(ns_platform_waitable_t w)
{
#if defined(_WIN32)
    return w.handle != NULL;
#else
    return w.fd >= 0;
#endif
}

static void test_destroy_raw_waitable(ns_platform_waitable_t w)
{
#if defined(_WIN32)
    if(w.handle != NULL) CloseHandle((HANDLE)w.handle);
#else
    if(w.fd >= 0) close(w.fd);
#endif
}

static void test_signal_raw_waitable(ns_platform_waitable_t w)
{
#if defined(_WIN32)
    (void)SetEvent((HANDLE)w.handle);
#else
    uint64_t val = 1u;
    (void)write(w.fd, &val, sizeof(val));
#endif
}

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
    EXPECT_OK(ns_broker_add(NULL, NULL) == NS_E_INVAL);
    EXPECT_OK(ns_broker_remove(NULL, NULL) == NS_E_INVAL);

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
    ns_event_broker_t *broker;

    EXPECT_OK(ns_init() == NS_OK);
    broker = ns_broker();
    EXPECT_OK(broker != NULL);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
#if defined(_WIN32)
    EXPECT_OK(ns_watcher_init_handle(&watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#else
    EXPECT_OK(ns_watcher_init_fd(&watcher, raw.fd, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#endif

    EXPECT_OK(ns_broker_add(broker, &watcher) == NS_OK);
    EXPECT_OK(ns_broker_add(broker, &watcher) == NS_E_EXISTS);
    EXPECT_OK(ns_broker_remove(broker, &watcher) == NS_OK);
    EXPECT_OK(ns_broker_remove(broker, &watcher) == NS_E_INVAL);

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
    ns_event_broker_t *broker;
    int worker_started = 0;
    int watcher_initialized = 0;
    int connected = 0;
    int added = 0;
    int rc;

    broker_loop_ctx_init(&ctx);
    raw = ns_waitable_init();

    EXPECT_OK(ns_init() == NS_OK);
    broker = ns_broker();
    EXPECT_OK(broker != NULL);

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

    rc = ns_broker_add(broker, &watcher);
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

    EXPECT_OK(ns_broker_remove(broker, &watcher) == NS_OK);
    added = 0;
    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    connected = 0;
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    watcher_initialized = 0;
    test_destroy_raw_waitable(raw);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;

fail:
    if(added) (void)ns_broker_remove(broker, &watcher);
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
    ns_event_broker_t *broker;

    EXPECT_OK(ns_init() == NS_OK);
    broker = ns_broker();
    EXPECT_OK(broker != NULL);

    raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(raw));
#if defined(_WIN32)
    EXPECT_OK(ns_watcher_init_handle(&watcher, raw.handle, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#else
    EXPECT_OK(ns_watcher_init_fd(&watcher, raw.fd, NS_WAITABLE_EVENT_IN, 0) == NS_OK);
#endif
    EXPECT_OK(ns_broker_add(broker, &watcher) == NS_OK);

    EXPECT_OK(ns_shutdown() == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(raw);
    return 0;
}

int main(void)
{
    if(test_broker_lifecycle() != 0) return 1;
    if(test_watcher_invalid_paths() != 0) return 1;
    if(test_broker_add_remove() != 0) return 1;
    if(test_watcher_event_reaches_loop() != 0) return 1;
    if(test_shutdown_removes_residual_watcher() != 0) return 1;

    return 0;
}
