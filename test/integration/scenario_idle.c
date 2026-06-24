/**
 * @file scenario_idle.c
 * @brief Idle scenario — runs an empty loop with a timer-based exit.
 * @date 2026-06-20
 *
 * Creates a loop with 0 watchers, starts a 3-second oneshot timer,
 * and idles for ~3 seconds. Verifies clean shutdown with no leaks.
 * Capacity: NS_CAPACITY_1024 (no emit activity expected).
 *
 * #included into test_layer3.c
 *
 * Duration scaled by NANOSIG_TEST_SCALE env var.
 */

#define IDLE_DURATION_US (3u * 1000000u * integration_test_scale()) /* 3s × scale */

static ns_loop_t *g_idle_loop;

static void idle_exit_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    (void)ns_loop_quit(g_idle_loop);
}

static int scenario_idle(void)
{
    ns_timer_t exit_timer;
    ns_connection_t exit_conn;
    int rc;

    g_idle_loop = NULL;

    INTEGRATION_PHASE("idle: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_idle_loop = integration_create_loop(NS_CAPACITY_1024, "idle");
    EXPECT_OK(g_idle_loop != NULL);

    EXPECT_OK(ns_timer_init(&exit_timer, IDLE_DURATION_US, 0) == NS_OK);
    EXPECT_OK(ns_signal_connect(&exit_timer.signal, idle_exit_slot, g_idle_loop, NULL, &exit_conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&exit_timer) == NS_OK);

    INTEGRATION_PHASE("idle: running loop for %d seconds", (int)(IDLE_DURATION_US / 1000000u));
    rc = ns_loop_run(g_idle_loop);
    EXPECT_OK(rc == NS_OK);

    INTEGRATION_PHASE("idle: loop exited, cleaning up");
    EXPECT_OK(ns_signal_disconnect(&exit_conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&exit_timer) == NS_OK);
    EXPECT_OK(ns_loop_deinit(g_idle_loop) == NS_OK);
    g_idle_loop = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("idle: no events, clean exit");
    return 0;
}

#undef IDLE_DURATION_US
