/**
 * @file scenario_lifecycle_marathon.c
 * @brief Multi-phase marathon: 4 phases, 3 rounds.
 * @date 2026-06-20
 *
 * 4 phases × 30s (shortened to 3s each for CI):
 *   Phase 1: Create 5 timers
 *   Phase 2: Create 5 fd watchers
 *   Phase 3: Mixed cancel/remove/add
 *   Phase 4: Gradual shutdown
 * Repeats for 3 rounds.
 * Capacity: NS_CAPACITY_16384
 *
 * #included into test_layer3.c
 */

#define LM_NUM_TIMERS 5u
#define LM_NUM_WATCHERS 5u
#define LM_PHASE_DURATION_US (3u * 1000000u * integration_test_scale()) /* 3s per phase x scale */
#define LM_NUM_ROUNDS 3u

static int scenario_lifecycle_marathon(void)
{
    unsigned int round, i;

    for(round = 0u; round < LM_NUM_ROUNDS; round++){
        ns_loop_t *loop = NULL;
        ns_timer_t timers[LM_NUM_TIMERS];
        ns_connection_t timer_conns[LM_NUM_TIMERS];
        ns_watcher_t watchers[LM_NUM_WATCHERS];
        ns_connection_t watcher_conns[LM_NUM_WATCHERS];
        ns_platform_waitable_t raw[LM_NUM_WATCHERS];
        ns_signal_t signal;
        ns_connection_t sig_conn;

        INTEGRATION_PHASE("marathon: round %u/3 start", round + 1u);
        EXPECT_OK(ns_init() == NS_OK);

        loop = integration_create_loop(NS_CAPACITY_16384, "marathon");
        EXPECT_OK(loop != NULL);
        EXPECT_OK(ns_loop_start(loop) == NS_OK);

        EXPECT_OK(ns_signal_init_raw(&signal, 0u, 0u, "marathon_sig") == NS_OK);
        EXPECT_OK(ns_signal_connect(&signal, dummy_slot, loop, NULL, &sig_conn) == NS_OK);

        /* Phase 1: Create timers */
        INTEGRATION_PHASE("marathon: round %u phase 1 — create %u timers", round + 1u, LM_NUM_TIMERS);
        for(i = 0u; i < LM_NUM_TIMERS; i++){
            EXPECT_OK(ns_timer_init(&timers[i], 100000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
            EXPECT_OK(ns_signal_connect(&timers[i].signal, dummy_slot, loop, NULL, &timer_conns[i]) == NS_OK);
            EXPECT_OK(ns_timer_start(&timers[i]) == NS_OK);
        }
#if defined(_WIN32)
        Sleep(1000u);
#else
        { struct timespec ts = {1, 0}; (void)nanosleep(&ts, NULL); }
#endif

        /* Phase 2: Create watchers */
        INTEGRATION_PHASE("marathon: round %u phase 2 — create %u watchers", round + 1u, LM_NUM_WATCHERS);
        for(i = 0u; i < LM_NUM_WATCHERS; i++){
            raw[i] = test_create_raw_waitable();
            EXPECT_OK(test_raw_waitable_is_valid(raw[i]));
            { ns_waitable_handle_t h = NS_WAITABLE_GET(&raw[i]); EXPECT_OK(ns_watcher_init(&watchers[i], h, NS_WAITABLE_EVENT_IN, 1, NULL) == NS_OK); }
            EXPECT_OK(ns_signal_connect(&watchers[i].signal, dummy_slot, loop, NULL, &watcher_conns[i]) == NS_OK);
            EXPECT_OK(ns_broker_add(&watchers[i]) == NS_OK);
        }
#if defined(_WIN32)
        Sleep(1000u);
#else
        { struct timespec ts = {1, 0}; (void)nanosleep(&ts, NULL); }
#endif

        /* Phase 3: Mixed operations */
        INTEGRATION_PHASE("marathon: round %u phase 3 — mixed ops", round + 1u);
        for(i = 0u; i < LM_NUM_TIMERS; i++){
            (void)ns_timer_cancel(&timers[i]);
            (void)ns_timer_start(&timers[i]);
        }
        for(i = 0u; i < LM_NUM_WATCHERS; i++){
            (void)ns_broker_remove(&watchers[i]);
            (void)ns_broker_add(&watchers[i]);
        }
#if defined(_WIN32)
        Sleep(1000u);
#else
        { struct timespec ts = {1, 0}; (void)nanosleep(&ts, NULL); }
#endif

        /* Phase 4: Teardown */
        INTEGRATION_PHASE("marathon: round %u phase 4 — shutdown", round + 1u);
        EXPECT_OK(ns_loop_stop(loop) == NS_OK);

        EXPECT_OK(ns_signal_disconnect(&sig_conn) == NS_OK);
        EXPECT_OK(ns_signal_deinit_raw(&signal) == NS_OK);

        for(i = 0u; i < LM_NUM_WATCHERS; i++){
            (void)ns_broker_remove(&watchers[i]);
            EXPECT_OK(ns_signal_disconnect(&watcher_conns[i]) == NS_OK);
            EXPECT_OK(ns_watcher_deinit(&watchers[i]) == NS_OK);
            test_destroy_raw_waitable(raw[i]);
        }
        for(i = 0u; i < LM_NUM_TIMERS; i++){
            EXPECT_OK(ns_signal_disconnect(&timer_conns[i]) == NS_OK);
            EXPECT_OK(ns_timer_deinit(&timers[i]) == NS_OK);
        }
        EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

        integration_verify_clean_shutdown();
        INTEGRATION_PASS("marathon: round %u/3 complete", round + 1u);
    }

    INTEGRATION_PASS("marathon: all %u rounds completed without leak", LM_NUM_ROUNDS);
    return 0;
}

#undef LM_NUM_TIMERS
#undef LM_NUM_WATCHERS
#undef LM_PHASE_DURATION_US
#undef LM_NUM_ROUNDS
