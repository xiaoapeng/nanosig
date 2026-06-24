/**
 * @file scenario_watchdog.c
 * @brief Watchdog timer monitoring fd watcher activity.
 * @date 2026-06-20
 *
 * Two fd watchers with a 100ms repeat timer polling activity counters.
 * Capacity: NS_CAPACITY_8192
 *
 * #included into test_layer1.c
 */

#define WATCHDOG_NUM_WATCHERS 2u
#define WATCHDOG_INTERVAL_US 100000u

static ns_loop_t *g_wd_loop;
static ns_watcher_t g_wd_watchers[WATCHDOG_NUM_WATCHERS];
static ns_connection_t g_wd_watcher_conns[WATCHDOG_NUM_WATCHERS];
static ns_platform_waitable_t g_wd_raw[WATCHDOG_NUM_WATCHERS];
static ns_timer_t g_wd_timer;
static ns_connection_t g_wd_timer_conn;
static atomic_int g_wd_counts[WATCHDOG_NUM_WATCHERS];

static void wd_watcher_slot(void *user_data, const void *payload)
{
    size_t idx = (size_t)(intptr_t)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_wd_counts[idx], 1, ns_memory_order_relaxed);
}

static void wd_timer_slot(void *user_data, const void *payload)
{
    size_t i;
    (void)user_data;
    (void)payload;

    for(i = 0u; i < WATCHDOG_NUM_WATCHERS; i++){
        int cnt = ns_atomic_load_explicit(&g_wd_counts[i], ns_memory_order_relaxed);
        INTEGRATION_STATS("watchdog: watcher[%zu] count=%d", i, cnt);
    }

    /* Signal activity detected — quit loop */
    (void)ns_loop_quit(g_wd_loop);
}

static int scenario_watchdog(void)
{
    size_t i;
    int rc;

    for(i = 0u; i < WATCHDOG_NUM_WATCHERS; i++){
        ns_atomic_init(&g_wd_counts[i], 0);
    }
    g_wd_loop = NULL;

    INTEGRATION_PHASE("watchdog: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_wd_loop = integration_create_loop(NS_CAPACITY_8192, "watchdog");
    EXPECT_OK(g_wd_loop != NULL);

    /* Create watchers */
    for(i = 0u; i < WATCHDOG_NUM_WATCHERS; i++){
        g_wd_raw[i] = test_create_raw_waitable();
        EXPECT_OK(test_raw_waitable_is_valid(g_wd_raw[i]));
#if defined(_WIN32)
        EXPECT_OK(ns_watcher_init_handle(&g_wd_watchers[i], g_wd_raw[i].handle,
                                         NS_WAITABLE_EVENT_IN, 1) == NS_OK);
#else
        EXPECT_OK(ns_watcher_init_fd(&g_wd_watchers[i], g_wd_raw[i].fd,
                                     NS_WAITABLE_EVENT_IN, 1) == NS_OK);
#endif
        EXPECT_OK(ns_signal_connect(&g_wd_watchers[i].signal, wd_watcher_slot,
                                    g_wd_loop, (void *)(intptr_t)i,
                                    &g_wd_watcher_conns[i]) == NS_OK);
        EXPECT_OK(ns_broker_add(&g_wd_watchers[i]) == NS_OK);
    }

    /* Create watchdog timer */
    EXPECT_OK(ns_timer_init(&g_wd_timer, WATCHDOG_INTERVAL_US, NS_TIMER_ATTR_REPEAT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&g_wd_timer.signal, wd_timer_slot,
                                g_wd_loop, NULL, &g_wd_timer_conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&g_wd_timer) == NS_OK);

    /* Trigger raw waitables */
    for(i = 0u; i < WATCHDOG_NUM_WATCHERS; i++){
        test_signal_raw_waitable(g_wd_raw[i]);
    }

    INTEGRATION_PHASE("watchdog: running loop");
    rc = ns_loop_run(g_wd_loop);
    EXPECT_OK(rc == NS_OK);

    /* Verify at least one watcher was triggered */
    {
        int total = 0;
        for(i = 0u; i < WATCHDOG_NUM_WATCHERS; i++){
            total += ns_atomic_load_explicit(&g_wd_counts[i], ns_memory_order_relaxed);
        }
        INTEGRATION_STATS("watchdog: total watcher events=%d", total);
        EXPECT_OK(total > 0);
    }

    /* Cleanup */
    EXPECT_OK(ns_signal_disconnect(&g_wd_timer_conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&g_wd_timer) == NS_OK);

    for(i = 0u; i < WATCHDOG_NUM_WATCHERS; i++){
        EXPECT_OK(ns_broker_remove(&g_wd_watchers[i]) == NS_OK);
        EXPECT_OK(ns_signal_disconnect(&g_wd_watcher_conns[i]) == NS_OK);
        EXPECT_OK(ns_watcher_deinit(&g_wd_watchers[i]) == NS_OK);
        test_destroy_raw_waitable(g_wd_raw[i]);
    }

    EXPECT_OK(ns_loop_deinit(g_wd_loop) == NS_OK);
    g_wd_loop = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("watchdog: watchers active, timer fired");
    return 0;
}

#undef WATCHDOG_NUM_WATCHERS
#undef WATCHDOG_INTERVAL_US
