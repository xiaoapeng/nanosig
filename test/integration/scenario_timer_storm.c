/**
 * @file scenario_timer_storm.c
 * @brief 500 timer burst within 100ms.
 * @date 2026-06-20
 *
 * Creates 500 oneshot timers with 100us-1ms intervals,
 * all started within 100ms. Verifies all fire correctly.
 * Capacity: NS_CAPACITY_262144
 *
 * #included into test_layer2.c
 */

#define STORM_NUM_TIMERS 500u  /* Array capacity; scaled exit via integration_test_scale */
#define STORM_MAX_INTERVAL_US 1000u  /* 1ms */

static ns_loop_t *g_storm_loop;
static ns_timer_t g_storm_timers[STORM_NUM_TIMERS];
static ns_connection_t g_storm_conns[STORM_NUM_TIMERS];
static atomic_int g_storm_count;

static void storm_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_storm_count, 1, ns_memory_order_relaxed);
    if((unsigned int)ns_atomic_load_explicit(&g_storm_count, ns_memory_order_relaxed) >= STORM_NUM_TIMERS){
        (void)ns_loop_quit(g_storm_loop);
    }
}

static int scenario_timer_storm(void)
{
    unsigned int i;

    ns_atomic_init(&g_storm_count, 0);
    g_storm_loop = NULL;

    INTEGRATION_PHASE("timer_storm: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_storm_loop = integration_create_loop(NS_CAPACITY_262144, "storm");
    EXPECT_OK(g_storm_loop != NULL);

    srand((unsigned int)12345);

    for(i = 0u; i < STORM_NUM_TIMERS; i++){
        ns_time_us_t interval = (ns_time_us_t)(((unsigned int)rand() % STORM_MAX_INTERVAL_US) + 100u);
        EXPECT_OK(ns_timer_init(&g_storm_timers[i], interval, 0) == NS_OK);
        EXPECT_OK(ns_signal_connect(&g_storm_timers[i].signal, storm_slot,
                                    g_storm_loop, NULL, &g_storm_conns[i]) == NS_OK);
    }

    INTEGRATION_PHASE("timer_storm: starting %u timers", STORM_NUM_TIMERS);
    for(i = 0u; i < STORM_NUM_TIMERS; i++){
        EXPECT_OK(ns_timer_start(&g_storm_timers[i]) == NS_OK);
    }

    {
        int rc = ns_loop_run(g_storm_loop);
        EXPECT_OK(rc == NS_OK);
    }

    {
        unsigned int fired = (unsigned int)ns_atomic_load_explicit(&g_storm_count, ns_memory_order_acquire);
        INTEGRATION_STATS("timer_storm: %u/%u fired", fired, STORM_NUM_TIMERS);
        EXPECT_EQ((int)fired, (int)STORM_NUM_TIMERS);
    }

    for(i = 0u; i < STORM_NUM_TIMERS; i++){
        EXPECT_OK(ns_signal_disconnect(&g_storm_conns[i]) == NS_OK);
        EXPECT_OK(ns_timer_deinit(&g_storm_timers[i]) == NS_OK);
    }
    EXPECT_OK(ns_loop_deinit(g_storm_loop) == NS_OK);
    g_storm_loop = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("timer_storm: all %u timers fired", STORM_NUM_TIMERS);
    return 0;
}

#undef STORM_NUM_TIMERS
#undef STORM_MAX_INTERVAL_US
