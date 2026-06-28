/**
 * @file scenario_lifecycle_chaos.c
 * @brief Random lifecycle operations with watchers and timers.
 * @date 2026-06-20
 *
 * Runs 5 watchers + 3 timers concurrently with a worker thread
 * doing random add/remove/disconnect/cancel/restart operations.
 * Runs 5 seconds, verifies 0 crash.
 * Capacity: NS_CAPACITY_32768
 *
 * #included into test_layer1.c
 */

#include "test_macros.h"
#include "integration_helpers.h"

#define CHAOS_NUM_WATCHERS 5u
#define CHAOS_NUM_TIMERS 3u
#define CHAOS_RUN_DURATION_US (5u * 1000000u * integration_test_scale())  /* 5s x scale */
#define CHAOS_OPS_PER_SECOND 20u

#include <stdlib.h>
#include "test_thread.h"
#if !defined(_WIN32)
#include <pthread.h>
#endif

static ns_loop_t *g_chaos_loop;
static ns_watcher_t g_chaos_watchers[CHAOS_NUM_WATCHERS];
static ns_connection_t g_chaos_wconns[CHAOS_NUM_WATCHERS];
static ns_platform_waitable_t g_chaos_raw[CHAOS_NUM_WATCHERS];
static ns_timer_t g_chaos_timers[CHAOS_NUM_TIMERS];
static ns_connection_t g_chaos_tconns[CHAOS_NUM_TIMERS];
static ns_signal_t g_chaos_signal;
static atomic_int g_chaos_quit;
static atomic_int g_chaos_op_count;

static void chaos_dummy_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
}

static test_thread_t g_chaos_thread;

static int chaos_worker_entry(void *arg)
{
    (void)arg;

    while(ns_atomic_load_explicit(&g_chaos_quit, ns_memory_order_relaxed) == 0){
        int choice;


        choice = rand() % 5;
        switch(choice){
        case 0:{
            size_t i = (size_t)((unsigned int)rand() % CHAOS_NUM_WATCHERS);
            (void)ns_broker_add(&g_chaos_watchers[i]);
            break;
        }
        case 1:{
            size_t i = (size_t)((unsigned int)rand() % CHAOS_NUM_WATCHERS);
            (void)ns_broker_remove(&g_chaos_watchers[i]);
            break;
        }
        case 2:{
            size_t i = (size_t)((unsigned int)rand() % CHAOS_NUM_TIMERS);
            (void)ns_timer_cancel(&g_chaos_timers[i]);
            (void)ns_timer_start(&g_chaos_timers[i]);
            break;
        }
        case 3:{
            size_t i = (size_t)((unsigned int)rand() % CHAOS_NUM_TIMERS);
            (void)ns_timer_restart(&g_chaos_timers[i]);
            break;
        }
        case 4:
            (void)ns_signal_emit_raw(&g_chaos_signal, NULL, 0u);
            break;
        }
        ns_atomic_fetch_add_explicit(&g_chaos_op_count, 1, ns_memory_order_relaxed);

#if defined(_WIN32)
        SwitchToThread();
#else
        { struct timespec ts = {0, 5000000}; (void)nanosleep(&ts, NULL); }
#endif
    }

    return 0;
}

static int scenario_lifecycle_chaos(void)
{
    size_t i;
    int rc;

    ns_atomic_init(&g_chaos_quit, 0);
    ns_atomic_init(&g_chaos_op_count, 0);
    g_chaos_loop = NULL;
    srand((unsigned int)42);

    INTEGRATION_PHASE("lifecycle_chaos: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_chaos_loop = integration_create_loop(NS_CAPACITY_32768, "chaos");
    EXPECT_OK(g_chaos_loop != NULL);
    EXPECT_OK(ns_loop_start(g_chaos_loop) == NS_OK);

    /* Create watchers */
    for(i = 0u; i < CHAOS_NUM_WATCHERS; i++){
        g_chaos_raw[i] = test_create_raw_waitable();
        EXPECT_OK(test_raw_waitable_is_valid(g_chaos_raw[i]));
        { ns_waitable_handle_t h = NS_WAITABLE_GET(&g_chaos_raw[i]); rc = ns_watcher_init(&g_chaos_watchers[i], h, NS_WAITABLE_EVENT_IN, 1, NULL); }
        EXPECT_OK(rc == NS_OK);
        EXPECT_OK(ns_signal_connect(&g_chaos_watchers[i].signal, chaos_dummy_slot,
                                    g_chaos_loop, NULL, &g_chaos_wconns[i]) == NS_OK);
        EXPECT_OK(ns_broker_add(&g_chaos_watchers[i]) == NS_OK);
    }

    /* Create timers */
    for(i = 0u; i < CHAOS_NUM_TIMERS; i++){
        EXPECT_OK(ns_timer_init(&g_chaos_timers[i], 50000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
        EXPECT_OK(ns_signal_connect(&g_chaos_timers[i].signal, chaos_dummy_slot,
                                    g_chaos_loop, NULL, &g_chaos_tconns[i]) == NS_OK);
        EXPECT_OK(ns_timer_start(&g_chaos_timers[i]) == NS_OK);
    }

    /* Create signal for worker to emit */
    EXPECT_OK(ns_signal_init_raw(&g_chaos_signal, 0u, 0u, "chaos_signal") == NS_OK);

    INTEGRATION_PHASE("lifecycle_chaos: starting chaos worker for 5s");
    test_thread_init(&g_chaos_thread, chaos_worker_entry, NULL);
    EXPECT_OK(test_thread_start(&g_chaos_thread) == 0);

    /* Sleep for run duration */
#if defined(_WIN32)
    Sleep(5000u);
#else
    { struct timespec ts = {5, 0}; (void)nanosleep(&ts, NULL); }
#endif

    ns_atomic_store_explicit(&g_chaos_quit, 1, ns_memory_order_release);
    test_thread_join(&g_chaos_thread);

    {
        int ops = ns_atomic_load_explicit(&g_chaos_op_count, ns_memory_order_relaxed);
        INTEGRATION_STATS("lifecycle_chaos: %d total ops, 0 crashes", ops);
    }

    /* Cleanup */
    EXPECT_OK(ns_loop_stop(g_chaos_loop) == NS_OK);

    for(i = 0u; i < CHAOS_NUM_TIMERS; i++){
        EXPECT_OK(ns_signal_disconnect(&g_chaos_tconns[i]) == NS_OK);
        EXPECT_OK(ns_timer_deinit(&g_chaos_timers[i]) == NS_OK);
    }
    for(i = 0u; i < CHAOS_NUM_WATCHERS; i++){
        (void)ns_broker_remove(&g_chaos_watchers[i]);
        EXPECT_OK(ns_signal_disconnect(&g_chaos_wconns[i]) == NS_OK);
        EXPECT_OK(ns_watcher_deinit(&g_chaos_watchers[i]) == NS_OK);
        test_destroy_raw_waitable(g_chaos_raw[i]);
    }

    EXPECT_OK(ns_signal_deinit_raw(&g_chaos_signal) == NS_OK);
    EXPECT_OK(ns_loop_deinit(g_chaos_loop) == NS_OK);
    g_chaos_loop = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("lifecycle_chaos: completed without crash");
    return 0;
}

#undef CHAOS_NUM_WATCHERS
#undef CHAOS_NUM_TIMERS
#undef CHAOS_RUN_DURATION_US
#undef CHAOS_OPS_PER_SECOND

#ifdef SCENARIO_MAIN
int main(void) { return scenario_lifecycle_chaos(); }
#endif
