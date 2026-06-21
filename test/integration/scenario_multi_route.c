/**
 * @file scenario_multi_route.c
 * @brief Multi-loop routing: chain, fan-in, fan-out.
 * @date 2026-06-20
 *
 * 3 loops (A/B/C) in background threads with cross-loop signal routing.
 * Chain: watcher-1 → signal S1 on loop-A → emit S2 → loop-B → emit S3 → loop-C.
 * Fan-in: 1 watcher triggers S1.
 * Fan-out: 1 signal → 1 slot per loop (for counting).
 * Capacity: NS_CAPACITY_32768
 *
 * #included into test_layer3.c
 */

#define MR_NUM_LOOPS 3u

static ns_loop_t *g_mr_loops[MR_NUM_LOOPS];
static ns_signal_t g_mr_signal_chain[MR_NUM_LOOPS];
static ns_watcher_t g_mr_watcher;
static ns_connection_t g_mr_watcher_conn;
static ns_platform_waitable_t g_mr_raw;
static ns_connection_t g_mr_chain_conns[MR_NUM_LOOPS * MR_NUM_LOOPS];
static ns_connection_t g_mr_interchain_conns[MR_NUM_LOOPS - 1u];
static atomic_int g_mr_route_counts[MR_NUM_LOOPS][MR_NUM_LOOPS];

/* Chain propagation slot */
static void mr_chain_slot(void *user_data, const void *payload)
{
    (void)payload;
    size_t sig_idx = (size_t)(intptr_t)user_data;
    if(sig_idx + 1u < MR_NUM_LOOPS){
        (void)ns_signal_emit_raw(&g_mr_signal_chain[sig_idx + 1u], NULL, 0u);
    }
}

/* Watcher trigger slot */
static void mr_trigger_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    (void)ns_signal_emit_raw(&g_mr_signal_chain[0], NULL, 0u);
}

/* Counting slot */
static void mr_count_slot(void *user_data, const void *payload)
{
    (void)payload;
    uintptr_t val = (uintptr_t)user_data;
    size_t sig_idx = (size_t)(val >> 16u);
    size_t loop_idx = (size_t)(val & 0xFFFFu);
    /* Only count the first event to avoid overflow from repeat deliveries */
    if(ns_atomic_load_explicit(&g_mr_route_counts[sig_idx][loop_idx], ns_memory_order_relaxed) == 0){
        ns_atomic_store_explicit(&g_mr_route_counts[sig_idx][loop_idx], 1, ns_memory_order_release);
    }
}

/* Condition: all counts >= 1 */
static int mr_cond_all(void *ctx)
{
    size_t i, j;
    (void)ctx;
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        for(j = 0u; j < MR_NUM_LOOPS; j++){
            if(ns_atomic_load_explicit(&g_mr_route_counts[i][j], ns_memory_order_acquire) < 1)
                return 0;
        }
    }
    return 1;
}

static int scenario_multi_route(void)
{
    size_t i, j;
    size_t conn_idx = 0u;

    for(i = 0u; i < MR_NUM_LOOPS; i++){
        for(j = 0u; j < MR_NUM_LOOPS; j++){
            ns_atomic_init(&g_mr_route_counts[i][j], 0);
        }
        g_mr_loops[i] = NULL;
    }
    ns_waitable_init(&g_mr_raw);

    INTEGRATION_PHASE("multi_route: init");
    EXPECT_OK(ns_init() == NS_OK);

    /* Create 3 loops */
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        char name[32];
        (void)snprintf(name, sizeof(name), "mr_loop_%zu", i);
        g_mr_loops[i] = integration_create_loop(NS_CAPACITY_32768, name);
        EXPECT_OK(g_mr_loops[i] != NULL);
    }

    /* Init chain signals */
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        EXPECT_OK(ns_signal_init_raw(&g_mr_signal_chain[i], 0u, 0u, "mr_chain") == NS_OK);
    }

    /* Chain interconnects: S_i on loop_i emits S_{i+1} */
    for(i = 0u; i < MR_NUM_LOOPS - 1u; i++){
        EXPECT_OK(ns_signal_connect(&g_mr_signal_chain[i], mr_chain_slot, g_mr_loops[i],
                                    (void *)(intptr_t)i, &g_mr_interchain_conns[i]) == NS_OK);
    }

    /* Fan-out: connect each signal to 1 count slot per loop */
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        for(j = 0u; j < MR_NUM_LOOPS; j++){
            uintptr_t val = (uintptr_t)((i << 16u) | j);
            EXPECT_OK(ns_signal_connect(&g_mr_signal_chain[i], mr_count_slot, g_mr_loops[j],
                                        (void *)val, &g_mr_chain_conns[conn_idx]) == NS_OK);
            conn_idx++;
        }
    }

    /* Create watcher */
    g_mr_raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(g_mr_raw));
#if defined(_WIN32)
    EXPECT_OK(ns_watcher_init_handle(&g_mr_watcher, g_mr_raw.handle, NS_WAITABLE_EVENT_IN, 1) == NS_OK);
#else
    EXPECT_OK(ns_watcher_init_fd(&g_mr_watcher, g_mr_raw.fd, NS_WAITABLE_EVENT_IN, 1) == NS_OK);
#endif
    /* Watcher signal → trigger slot on loop-0 */
    EXPECT_OK(ns_signal_connect(&g_mr_watcher.signal, mr_trigger_slot,
                                g_mr_loops[0], NULL, &g_mr_watcher_conn) == NS_OK);
    EXPECT_OK(ns_broker_add(&g_mr_watcher) == NS_OK);

    /* Start all loops in background threads */
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        EXPECT_OK(ns_loop_start(g_mr_loops[i]) == NS_OK);
    }

    INTEGRATION_PHASE("multi_route: trigger watcher to start chain");
    test_signal_raw_waitable(g_mr_raw);

    /* Wait for chain to propagate to all 3 loops */
    EXPECT_OK(integration_wait_for_condition(mr_cond_all, NULL, 10u * 1000000u) == 0);

    /* Stop all loops */
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        EXPECT_OK(ns_loop_stop(g_mr_loops[i]) == NS_OK);
    }

    /* Verify routing: every (signal,loop) has delivery */
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        for(j = 0u; j < MR_NUM_LOOPS; j++){
            int cnt = ns_atomic_load_explicit(&g_mr_route_counts[i][j], ns_memory_order_acquire);
            INTEGRATION_STATS("multi_route: S%zu → loop%zu = %d", i + 1u, j + 1u, cnt);
            EXPECT_OK(cnt >= 1);
        }
    }

    /* Cleanup */
    (void)ns_broker_remove(&g_mr_watcher);
    EXPECT_OK(ns_signal_disconnect(&g_mr_watcher_conn) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&g_mr_watcher) == NS_OK);
    test_destroy_raw_waitable(g_mr_raw);

    conn_idx = 0u;
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        for(j = 0u; j < MR_NUM_LOOPS; j++){
            EXPECT_OK(ns_signal_disconnect(&g_mr_chain_conns[conn_idx]) == NS_OK);
            conn_idx++;
        }
    }
    for(i = 0u; i < MR_NUM_LOOPS - 1u; i++){
        EXPECT_OK(ns_signal_disconnect(&g_mr_interchain_conns[i]) == NS_OK);
    }
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        EXPECT_OK(ns_signal_deinit_raw(&g_mr_signal_chain[i]) == NS_OK);
    }
    for(i = 0u; i < MR_NUM_LOOPS; i++){
        EXPECT_OK(ns_loop_destroy(g_mr_loops[i]) == NS_OK);
        g_mr_loops[i] = NULL;
    }

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("multi_route: chain/fan-in/fan-out verified");
    return 0;
}

#undef MR_NUM_LOOPS
