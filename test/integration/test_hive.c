/**
 * @file test_hive.c
 * @brief Long-stability "群蜂归巢" integration test.
 * @date 2026-06-20
 *
 * Runs 20 fd watchers + 200 random timers for 120 seconds.
 * Standalone CMake target with TIMEOUT 1500 and LABELS "integration;long-stability".
 */

#include "test_macros.h"
#include "test_helpers.h"
#include "integration_helpers.h"

#include <nanosig/nanosig.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define NUM_WATCHERS   20u
#define NUM_TIMERS    200u
#define RUN_DURATION_US (120u * 1000000u * integration_test_scale())  /* 120s x scale */
#define REPORT_INTERVAL_US (30u * 1000000u) /* every 30 seconds */

/* ------------------------------------------------------------------ */
/*  Global test state                                                   */
/* ------------------------------------------------------------------ */

static ns_loop_t *g_loop = NULL;
static ns_watcher_t g_watchers[NUM_WATCHERS];
static ns_connection_t g_watcher_conns[NUM_WATCHERS];
static ns_platform_waitable_t g_raw_waitables[NUM_WATCHERS];
static ns_timer_t g_timers[NUM_TIMERS];
static ns_connection_t g_timer_conns[NUM_TIMERS];

static atomic_int g_watcher_count;
static atomic_int g_timer_count;
static atomic_int g_quit_requested;

/* ------------------------------------------------------------------ */
/*  Forward declarations                                                */
/* ------------------------------------------------------------------ */

static int setup_watchers(void);
static int setup_timers(void);
static int teardown_all(void);

/* ------------------------------------------------------------------ */
/*  Slot callbacks                                                      */
/* ------------------------------------------------------------------ */

static void hive_watcher_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_watcher_count, 1, ns_memory_order_relaxed);
}

static void hive_timer_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_timer_count, 1, ns_memory_order_relaxed);
}

/* ------------------------------------------------------------------ */
/*  Setup                                                               */
/* ------------------------------------------------------------------ */

static int setup_watchers(void)
{
    size_t i;
    int rc;

    for(i = 0u; i < NUM_WATCHERS; i++){
        g_raw_waitables[i] = test_create_raw_waitable();
        if(!test_raw_waitable_is_valid(g_raw_waitables[i])){
            INTEGRATION_PHASE("hive: failed to create raw waitable %zu", i);
            return -1;
        }
#if defined(_WIN32)
        rc = ns_watcher_init_handle(&g_watchers[i], g_raw_waitables[i].handle,
                                    NS_WAITABLE_EVENT_IN, 1);
#else
        rc = ns_watcher_init_fd(&g_watchers[i], g_raw_waitables[i].fd,
                                NS_WAITABLE_EVENT_IN, 1);
#endif
        if(rc != NS_OK){
            INTEGRATION_PHASE("hive: ns_watcher_init_fd failed for %zu (rc=%d)", i, rc);
            return -1;
        }
        rc = ns_signal_connect(&g_watchers[i].signal, hive_watcher_slot,
                               g_loop, NULL, &g_watcher_conns[i]);
        if(rc != NS_OK){
            INTEGRATION_PHASE("hive: ns_signal_connect watcher %zu failed (rc=%d)", i, rc);
            return -1;
        }
        rc = ns_broker_add(&g_watchers[i]);
        if(rc != NS_OK){
            INTEGRATION_PHASE("hive: ns_broker_add watcher %zu failed (rc=%d)", i, rc);
            return -1;
        }
    }

    INTEGRATION_PHASE("hive: %u watchers setup complete", (unsigned)NUM_WATCHERS);
    return 0;
}

static int setup_timers(void)
{
    size_t i;
    int rc;

    srand((unsigned int)time(NULL));

    for(i = 0u; i < NUM_TIMERS; i++){
        /* Random interval between 1ms and 5000ms */
        ns_time_us_t interval_us = (ns_time_us_t)((rand() % 5000) + 1) * 1000u;

        rc = ns_timer_init(&g_timers[i], interval_us, NS_TIMER_ATTR_REPEAT);
        if(rc != NS_OK){
            INTEGRATION_PHASE("hive: ns_timer_init %zu failed (rc=%d)", i, rc);
            return -1;
        }
        rc = ns_signal_connect(&g_timers[i].signal, hive_timer_slot,
                               g_loop, NULL, &g_timer_conns[i]);
        if(rc != NS_OK){
            INTEGRATION_PHASE("hive: ns_signal_connect timer %zu failed (rc=%d)", i, rc);
            return -1;
        }
        rc = ns_timer_start(&g_timers[i]);
        if(rc != NS_OK){
            INTEGRATION_PHASE("hive: ns_timer_start %zu failed (rc=%d)", i, rc);
            return -1;
        }
    }

    INTEGRATION_PHASE("hive: %u timers started", (unsigned)NUM_TIMERS);
    return 0;
}

static int teardown_all(void)
{
    size_t i;
    int ok = 1;

    /* Disconnect watchers, remove from broker, deinit */
    for(i = 0u; i < NUM_WATCHERS; i++){
        (void)ns_broker_remove(&g_watchers[i]);
        (void)ns_signal_disconnect(&g_watcher_conns[i]);
        (void)ns_watcher_deinit(&g_watchers[i]);
        test_destroy_raw_waitable(g_raw_waitables[i]);
    }

    /* Cancel and destroy timers */
    for(i = 0u; i < NUM_TIMERS; i++){
        (void)ns_signal_disconnect(&g_timer_conns[i]);
        (void)ns_timer_deinit(&g_timers[i]);
    }

    if(g_loop != NULL){
        (void)ns_loop_stop(g_loop);
        (void)ns_loop_deinit(g_loop);
        g_loop = NULL;
    }

    integration_verify_clean_shutdown();
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    int rc;
    ns_time_us_t start_time, now;
    ns_time_us_t last_report = 0u;
    ns_timer_t exit_timer;
    ns_connection_t exit_conn;

    ns_atomic_init(&g_watcher_count, 0);
    ns_atomic_init(&g_timer_count, 0);
    ns_atomic_init(&g_quit_requested, 0);

    INTEGRATION_PHASE("hive: ns_init()");
    EXPECT_OK(ns_init() == NS_OK);

    g_loop = integration_create_loop(NS_CAPACITY_65536, "hive");
    EXPECT_OK(g_loop != NULL);

    EXPECT_OK(ns_loop_start(g_loop) == NS_OK);

    /* Create a 120s oneshot timer to auto-exit the loop */
    EXPECT_OK(ns_timer_init(&exit_timer, RUN_DURATION_US, 0) == NS_OK);
    EXPECT_OK(ns_signal_connect(&exit_timer.signal, hive_timer_slot,
                                g_loop, NULL, &exit_conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&exit_timer) == NS_OK);

    EXPECT_OK(setup_watchers() == 0);
    EXPECT_OK(setup_timers() == 0);

    EXPECT_OK(ns_platform_clock_monotonic_us(&start_time) == NS_OK);

    INTEGRATION_PHASE("hive: running for 120 seconds...");

    /* Main monitoring loop */
    for(;;){
        EXPECT_OK(ns_platform_clock_monotonic_us(&now) == NS_OK);

        if(now - start_time >= RUN_DURATION_US) break;

        /* Report every 30 seconds */
        if(now - last_report >= REPORT_INTERVAL_US){
            int wc = ns_atomic_load_explicit(&g_watcher_count, ns_memory_order_relaxed);
            int tc = ns_atomic_load_explicit(&g_timer_count, ns_memory_order_relaxed);
            INTEGRATION_PHASE("hive: t=%llus watchers=%d timers=%d",
                (unsigned long long)((now - start_time) / 1000000u), wc, tc);
            last_report = now;
        }

        /* Sleep 1 second between checks */
#if defined(_WIN32)
        Sleep(1000u);
#else
        {
            struct timespec ts = {1, 0};
            (void)nanosleep(&ts, NULL);
        }
#endif
    }

    /* Stop the loop */
    rc = ns_loop_stop(g_loop);
    INTEGRATION_PHASE("hive: loop_stop rc=%d", rc);

    /* Print final stats */
    {
        int wc = ns_atomic_load_explicit(&g_watcher_count, ns_memory_order_relaxed);
        int tc = ns_atomic_load_explicit(&g_timer_count, ns_memory_order_relaxed);
        INTEGRATION_STATS("hive: watcher_events=%d timer_fires=%d", wc, tc);
        EXPECT_OK(tc > 0);
    }

    /* Cleanup exit timer */
    (void)ns_signal_disconnect(&exit_conn);
    (void)ns_timer_deinit(&exit_timer);

    EXPECT_OK(teardown_all() == 0);
    INTEGRATION_PASS("hive: ALL PASSED");
    return 0;
}
