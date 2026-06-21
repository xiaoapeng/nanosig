/**
 * @file scenario_dual_loop.c
 * @brief Dual-loop bridge scenario.
 * @date 2026-06-20
 *
 * Two loops in background threads, one signal connected to both.
 * Emit 100x, verify both slots receive all events.
 * Capacity: NS_CAPACITY_16384
 *
 * #included into test_layer1.c
 */

#define DUAL_LOOP_EMIT_COUNT 100

static atomic_int g_dual_count_a;
static atomic_int g_dual_count_b;
static ns_loop_t *g_dual_loop_a;
static ns_loop_t *g_dual_loop_b;
static ns_connection_t g_dual_conn_a;
static ns_connection_t g_dual_conn_b;

static void dual_slot_a(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_dual_count_a, 1, ns_memory_order_relaxed);
}

static void dual_slot_b(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_dual_count_b, 1, ns_memory_order_relaxed);
}

static int dual_cond_a(void *ctx)
{
    (void)ctx;
    return ns_atomic_load_explicit(&g_dual_count_a, ns_memory_order_acquire) >= DUAL_LOOP_EMIT_COUNT;
}

static int dual_cond_b(void *ctx)
{
    (void)ctx;
    return ns_atomic_load_explicit(&g_dual_count_b, ns_memory_order_acquire) >= DUAL_LOOP_EMIT_COUNT;
}

static int scenario_dual_loop(void)
{
    ns_signal_t signal;
    int i;

    ns_atomic_init(&g_dual_count_a, 0);
    ns_atomic_init(&g_dual_count_b, 0);
    g_dual_loop_a = NULL;
    g_dual_loop_b = NULL;

    INTEGRATION_PHASE("dual_loop: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_dual_loop_a = integration_create_loop(NS_CAPACITY_16384, "dual_a");
    EXPECT_OK(g_dual_loop_a != NULL);
    g_dual_loop_b = integration_create_loop(NS_CAPACITY_16384, "dual_b");
    EXPECT_OK(g_dual_loop_b != NULL);

    EXPECT_OK(ns_signal_init_raw(&signal, 0u, 0u, "dual_loop") == NS_OK);
    EXPECT_OK(ns_signal_connect(&signal, dual_slot_a, g_dual_loop_a, NULL, &g_dual_conn_a) == NS_OK);
    EXPECT_OK(ns_signal_connect(&signal, dual_slot_b, g_dual_loop_b, NULL, &g_dual_conn_b) == NS_OK);

    EXPECT_OK(ns_loop_start(g_dual_loop_a) == NS_OK);
    EXPECT_OK(ns_loop_start(g_dual_loop_b) == NS_OK);

    INTEGRATION_PHASE("dual_loop: emitting %d signals", DUAL_LOOP_EMIT_COUNT);
    for(i = 0; i < DUAL_LOOP_EMIT_COUNT; i++){
        EXPECT_OK(ns_signal_emit_raw(&signal, NULL, 0u) == NS_OK);
    }

    EXPECT_OK(integration_wait_for_condition(dual_cond_a, NULL, 5u * 1000000u) == 0);
    EXPECT_OK(integration_wait_for_condition(dual_cond_b, NULL, 5u * 1000000u) == 0);

    {
        int ca = ns_atomic_load_explicit(&g_dual_count_a, ns_memory_order_acquire);
        int cb = ns_atomic_load_explicit(&g_dual_count_b, ns_memory_order_acquire);
        INTEGRATION_STATS("dual_loop: count_a=%d count_b=%d", ca, cb);
        EXPECT_EQ(ca, DUAL_LOOP_EMIT_COUNT);
        EXPECT_EQ(cb, DUAL_LOOP_EMIT_COUNT);
    }

    EXPECT_OK(ns_signal_disconnect(&g_dual_conn_a) == NS_OK);
    EXPECT_OK(ns_signal_disconnect(&g_dual_conn_b) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&signal) == NS_OK);

    EXPECT_OK(ns_loop_stop(g_dual_loop_a) == NS_OK);
    EXPECT_OK(ns_loop_stop(g_dual_loop_b) == NS_OK);
    EXPECT_OK(ns_loop_destroy(g_dual_loop_a) == NS_OK);
    EXPECT_OK(ns_loop_destroy(g_dual_loop_b) == NS_OK);
    g_dual_loop_a = NULL;
    g_dual_loop_b = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("dual_loop: both loops received all %d emits", DUAL_LOOP_EMIT_COUNT);
    return 0;
}

#undef DUAL_LOOP_EMIT_COUNT
