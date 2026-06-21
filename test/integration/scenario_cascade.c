/**
 * @file scenario_cascade.c
 * @brief Timer cascade scenario — linear chain depth 10.
 * @date 2026-06-20
 *
 * Creates a timer cascade chain of depth 10. Each timer fires, then
 * uses ns_timer_restart in its slot to trigger the next stage.
 * Verifies all 10 stages fire in sequence.
 * Capacity: NS_CAPACITY_16384
 *
 * #included into test_layer1.c
 */

#define CASCADE_DEPTH 10

static ns_timer_t g_cascade_timers[CASCADE_DEPTH];
static ns_connection_t g_cascade_conns[CASCADE_DEPTH];
static ns_loop_t *g_cascade_loop;
static atomic_int g_cascade_stage;

static void cascade_slot(void *user_data, const void *payload)
{
    int stage = (int)(intptr_t)user_data;
    (void)payload;

    ns_atomic_store_explicit(&g_cascade_stage, stage, ns_memory_order_release);

    if(stage < CASCADE_DEPTH - 1){
        (void)ns_timer_restart(&g_cascade_timers[stage + 1]);
    } else {
        (void)ns_loop_quit(g_cascade_loop);
    }
}

static int scenario_cascade(void)
{
    int i;
    int rc;

    ns_atomic_init(&g_cascade_stage, -1);
    g_cascade_loop = NULL;

    INTEGRATION_PHASE("cascade: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_cascade_loop = integration_create_loop(NS_CAPACITY_16384, "cascade");
    EXPECT_OK(g_cascade_loop != NULL);

    for(i = 0; i < CASCADE_DEPTH; i++){
        EXPECT_OK(ns_timer_create(&g_cascade_timers[i], 10000u, 0) == NS_OK);
        EXPECT_OK(ns_signal_connect(&g_cascade_timers[i].signal, cascade_slot,
                                    g_cascade_loop, (void *)(intptr_t)i,
                                    &g_cascade_conns[i]) == NS_OK);
    }

    INTEGRATION_PHASE("cascade: starting stage 0");
    EXPECT_OK(ns_timer_start(&g_cascade_timers[0]) == NS_OK);

    rc = ns_loop_run(g_cascade_loop);
    EXPECT_OK(rc == NS_OK);

    {
        int final_stage = ns_atomic_load_explicit(&g_cascade_stage, ns_memory_order_acquire);
        INTEGRATION_STATS("cascade: final stage = %d", final_stage);
        EXPECT_EQ(final_stage, CASCADE_DEPTH - 1);
    }

    for(i = CASCADE_DEPTH - 1; i >= 0; i--){
        EXPECT_OK(ns_signal_disconnect(&g_cascade_conns[i]) == NS_OK);
        EXPECT_OK(ns_timer_destroy(&g_cascade_timers[i]) == NS_OK);
    }
    EXPECT_OK(ns_loop_destroy(g_cascade_loop) == NS_OK);
    g_cascade_loop = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("cascade: all %d stages fired", CASCADE_DEPTH);
    return 0;
}

#undef CASCADE_DEPTH
