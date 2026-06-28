/**
 * @file scenario_reentrant_race.c
 * @brief 8 threads concurrently emitting the same signal.
 * @date 2026-06-20
 *
 * 8 threads simultaneously emit to the same signal.
 * Slot callback performs random watcher/timer operations.
 * Capacity: NS_CAPACITY_65536
 *
 * #included into test_layer2.c
 */

#include "test_macros.h"
#include "integration_helpers.h"

#define RR_NUM_THREADS 8u
#define RR_EMITS_PER_THREAD (100u * integration_test_scale())
#define RR_NUM_WATCHERS 3u

static ns_signal_t g_rr_signal;
static ns_loop_t *g_rr_loop;
static ns_connection_t g_rr_conn;
static ns_watcher_t g_rr_watchers[RR_NUM_WATCHERS];
static ns_connection_t g_rr_wconns[RR_NUM_WATCHERS];
static ns_platform_waitable_t g_rr_raw[RR_NUM_WATCHERS];
static ns_timer_t g_rr_timer;
static ns_connection_t g_rr_timer_conn;
static atomic_int g_rr_count;
static atomic_int g_rr_threads_ready;

#include "test_thread.h"

static void rr_slot(void *user_data, const void *payload)
{
    size_t i;
    (void)user_data;
    (void)payload;

    ns_atomic_fetch_add_explicit(&g_rr_count, 1, ns_memory_order_relaxed);

    /* Random operations inside slot */
    i = (size_t)((unsigned int)rand() % RR_NUM_WATCHERS);
    switch(rand() % 3){
    case 0:
        (void)ns_broker_add(&g_rr_watchers[i]);
        break;
    case 1:
        (void)ns_broker_remove(&g_rr_watchers[i]);
        break;
    case 2:
        (void)ns_timer_restart(&g_rr_timer);
        break;
    }
}

static test_thread_t g_rr_threads[RR_NUM_THREADS];

static int rr_worker_entry(void *arg)
{
    unsigned int i;
    (void)arg;

    ns_atomic_fetch_add_explicit(&g_rr_threads_ready, 1, ns_memory_order_release);

    for(i = 0u; i < RR_EMITS_PER_THREAD; i++){
        (void)ns_signal_emit_raw(&g_rr_signal, NULL, 0u);
    }

    return 0;
}

static int scenario_reentrant_race(void)
{
    size_t i;

    ns_atomic_init(&g_rr_count, 0);
    ns_atomic_init(&g_rr_threads_ready, 0);
    g_rr_loop = NULL;
    srand((unsigned int)7777);

    INTEGRATION_PHASE("reentrant_race: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_rr_loop = integration_create_loop(NS_CAPACITY_65536, "rr_race");
    EXPECT_OK(g_rr_loop != NULL);
    EXPECT_OK(ns_loop_start(g_rr_loop) == NS_OK);

    EXPECT_OK(ns_signal_init_raw(&g_rr_signal, 0u, 0u, "rr_sig") == NS_OK);
    EXPECT_OK(ns_signal_connect(&g_rr_signal, rr_slot, g_rr_loop, NULL, &g_rr_conn) == NS_OK);

    EXPECT_OK(ns_timer_init(&g_rr_timer, 50000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&g_rr_timer.signal, rr_slot, g_rr_loop, NULL, &g_rr_timer_conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&g_rr_timer) == NS_OK);

    for(i = 0u; i < RR_NUM_WATCHERS; i++){
        g_rr_raw[i] = test_create_raw_waitable();
        EXPECT_OK(test_raw_waitable_is_valid(g_rr_raw[i]));
        { ns_waitable_handle_t h = NS_WAITABLE_GET(&g_rr_raw[i]); EXPECT_OK(ns_watcher_init(&g_rr_watchers[i], h, NS_WAITABLE_EVENT_IN, 1, NULL) == NS_OK); }
        EXPECT_OK(ns_signal_connect(&g_rr_watchers[i].signal, rr_slot, g_rr_loop, NULL, &g_rr_wconns[i]) == NS_OK);
        EXPECT_OK(ns_broker_add(&g_rr_watchers[i]) == NS_OK);
    }

    INTEGRATION_PHASE("reentrant_race: spawning %d threads x %d emits",
                      RR_NUM_THREADS, RR_EMITS_PER_THREAD);

    for(i = 0u; i < RR_NUM_THREADS; i++){
        test_thread_init(&g_rr_threads[i], rr_worker_entry, NULL);
        EXPECT_OK(test_thread_start(&g_rr_threads[i]) == 0);
    }

    for(i = 0u; i < RR_NUM_THREADS; i++){
        test_thread_join(&g_rr_threads[i]);
    }

    /* Allow async dispatch to settle */
#if defined(_WIN32)
    Sleep(1000u);
#else
    { struct timespec ts = {1, 0}; (void)nanosleep(&ts, NULL); }
#endif

    {
        int total = ns_atomic_load_explicit(&g_rr_count, ns_memory_order_acquire);
        INTEGRATION_STATS("reentrant_race: total slot calls=%d", total);
        EXPECT_OK(total > 0);
    }

    EXPECT_OK(ns_loop_stop(g_rr_loop) == NS_OK);

    EXPECT_OK(ns_signal_disconnect(&g_rr_timer_conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&g_rr_timer) == NS_OK);

    for(i = 0u; i < RR_NUM_WATCHERS; i++){
        (void)ns_broker_remove(&g_rr_watchers[i]);
        EXPECT_OK(ns_signal_disconnect(&g_rr_wconns[i]) == NS_OK);
        EXPECT_OK(ns_watcher_deinit(&g_rr_watchers[i]) == NS_OK);
        test_destroy_raw_waitable(g_rr_raw[i]);
    }

    EXPECT_OK(ns_signal_disconnect(&g_rr_conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&g_rr_signal) == NS_OK);

    EXPECT_OK(ns_loop_deinit(g_rr_loop) == NS_OK);
    g_rr_loop = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("reentrant_race: completed without crash");
    return 0;
}

#undef RR_NUM_THREADS
#undef RR_EMITS_PER_THREAD
#undef RR_NUM_WATCHERS

#ifdef SCENARIO_MAIN
int main(void) { return scenario_reentrant_race(); }
#endif
