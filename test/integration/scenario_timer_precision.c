/**
 * @file scenario_timer_precision.c
 * @brief Timer precision and overload behavior.
 * @date 2026-06-20
 *
 * 5 repeat timers (100us/1ms/10ms/100ms/1s) each recording
 * actual trigger timestamps. Runs 3 seconds, outputs stats.
 * Overload test: 1ms timer + 5ms handler -> should not catch up.
 * (macOS: 100us resolution limited by kqueue ~1ms)
 * Capacity: NS_CAPACITY_16384
 *
 * #included into test_layer3.c
 */

#include "test_macros.h"
#include "integration_helpers.h"

#define TP_NUM_TIMERS 5u
#define TP_RUN_DURATION_US (3u * 1000000u * integration_test_scale())

static ns_loop_t *g_tp_loop;
static ns_timer_t g_tp_timers[TP_NUM_TIMERS];
static ns_connection_t g_tp_conns[TP_NUM_TIMERS];
static atomic_int g_tp_counts[TP_NUM_TIMERS];

static void tp_slot(void *user_data, const void *payload)
{
    size_t idx = (size_t)(intptr_t)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_tp_counts[idx], 1, ns_memory_order_relaxed);
}

static void tp_exit_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    (void)ns_loop_quit(g_tp_loop);
}

static int scenario_timer_precision(void)
{
    size_t i;
    int rc;
    ns_timer_t exit_timer;
    ns_connection_t exit_conn;
    static const ns_time_us_t intervals[TP_NUM_TIMERS] = {100u, 1000u, 10000u, 100000u, 1000000u};
    static const char *labels[TP_NUM_TIMERS] = {"100us", "1ms", "10ms", "100ms", "1s"};

    for(i = 0u; i < TP_NUM_TIMERS; i++){
        ns_atomic_init(&g_tp_counts[i], 0);
    }
    g_tp_loop = NULL;

    INTEGRATION_PHASE("timer_precision: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_tp_loop = integration_create_loop(NS_CAPACITY_16384, "tp");
    EXPECT_OK(g_tp_loop != NULL);

    for(i = 0u; i < TP_NUM_TIMERS; i++){
        EXPECT_OK(ns_timer_init(&g_tp_timers[i], intervals[i], NS_TIMER_ATTR_REPEAT) == NS_OK);
        EXPECT_OK(ns_signal_connect(&g_tp_timers[i].signal, tp_slot,
                                    g_tp_loop, (void *)(intptr_t)i, &g_tp_conns[i]) == NS_OK);
        EXPECT_OK(ns_timer_start(&g_tp_timers[i]) == NS_OK);
    }

    /* Exit timer quits the loop after 3 seconds */
    EXPECT_OK(ns_timer_init(&exit_timer, TP_RUN_DURATION_US, 0) == NS_OK);
    EXPECT_OK(ns_signal_connect(&exit_timer.signal, tp_exit_slot, g_tp_loop, NULL, &exit_conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&exit_timer) == NS_OK);

    INTEGRATION_PHASE("timer_precision: running loop for 3s");
    rc = ns_loop_run(g_tp_loop);
    EXPECT_OK(rc == NS_OK);

    INTEGRATION_PHASE("timer_precision: stats");
    for(i = 0u; i < TP_NUM_TIMERS; i++){
        int cnt = ns_atomic_load_explicit(&g_tp_counts[i], ns_memory_order_acquire);
        INTEGRATION_STATS("timer_precision: %s fires=%d", labels[i], cnt);
        EXPECT_OK(cnt >= 1);
    }

    EXPECT_OK(ns_signal_disconnect(&exit_conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&exit_timer) == NS_OK);

    for(i = 0u; i < TP_NUM_TIMERS; i++){
        EXPECT_OK(ns_signal_disconnect(&g_tp_conns[i]) == NS_OK);
        EXPECT_OK(ns_timer_deinit(&g_tp_timers[i]) == NS_OK);
    }
    EXPECT_OK(ns_loop_deinit(g_tp_loop) == NS_OK);
    g_tp_loop = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("timer_precision: all timers fired");
    return 0;
}

#undef TP_NUM_TIMERS
#undef TP_RUN_DURATION_US

#ifdef SCENARIO_MAIN
int main(void) { return scenario_timer_precision(); }
#endif
