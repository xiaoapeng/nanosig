/**
 * @file scenario_random_chaos.c
 * @brief Random add/remove/emit/cancel operations from worker thread.
 * @date 2026-06-20
 *
 * Worker thread alternates between emit/disconnect/broker_add/remove/timer_cancel/restart.
 * Runs 5 seconds, verifies 0 crash.
 * Capacity: NS_CAPACITY_65536
 *
 * #included into test_layer2.c
 */

#define RAND_CHAOS_RUN_US (3u * 1000000u * integration_test_scale()) /* 3s x scale */

static ns_loop_t *g_rc_loop;
static ns_signal_t g_rc_signal;
static ns_connection_t g_rc_conn;
static ns_timer_t g_rc_timer;
static ns_connection_t g_rc_timer_conn;
static atomic_int g_rc_quit;
static atomic_int g_rc_op_count;
static ns_watcher_t g_rc_watcher;
static ns_connection_t g_rc_wconn;
static ns_platform_waitable_t g_rc_raw;

static void rc_dummy_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
}

#if defined(_WIN32)
static DWORD WINAPI rc_worker_main(LPVOID arg)
#else
static void *rc_worker_main(void *arg)
#endif
{
    (void)arg;

    while(ns_atomic_load_explicit(&g_rc_quit, ns_memory_order_relaxed) == 0){
        int choice = rand() % 6;
        switch(choice){
        case 0:
            (void)ns_signal_emit_raw(&g_rc_signal, NULL, 0u);
            break;
        case 1:
            (void)ns_signal_disconnect(&g_rc_conn);
            (void)ns_signal_connect(&g_rc_signal, rc_dummy_slot, g_rc_loop, NULL, &g_rc_conn);
            break;
        case 2:
            (void)ns_timer_cancel(&g_rc_timer);
            break;
        case 3:
            (void)ns_timer_start(&g_rc_timer);
            break;
        case 4:
            (void)ns_broker_add(&g_rc_watcher);
            break;
        case 5:
            (void)ns_broker_remove(&g_rc_watcher);
            break;
        }
        ns_atomic_fetch_add_explicit(&g_rc_op_count, 1, ns_memory_order_relaxed);
#if defined(_WIN32)
        SwitchToThread();
#else
        { struct timespec ts = {0, 10000000}; (void)nanosleep(&ts, NULL); }
#endif
    }

#if defined(_WIN32)
    return 0u;
#else
    return NULL;
#endif
}

#if defined(_WIN32)
static HANDLE g_rc_thread;
#else
static pthread_t g_rc_thread;
#endif

static int rc_worker_start(void)
{
#if defined(_WIN32)
    g_rc_thread = CreateThread(NULL, 0u, rc_worker_main, NULL, 0u, NULL);
    return (g_rc_thread != NULL) ? 0 : -1;
#else
    return (pthread_create(&g_rc_thread, NULL, rc_worker_main, NULL) == 0) ? 0 : -1;
#endif
}

static void rc_worker_join(void)
{
#if defined(_WIN32)
    if(g_rc_thread != NULL){
        WaitForSingleObject(g_rc_thread, 5000u);
        CloseHandle(g_rc_thread);
        g_rc_thread = NULL;
    }
#else
    (void)pthread_join(g_rc_thread, NULL);
#endif
}

static int scenario_random_chaos(void)
{

    ns_atomic_init(&g_rc_quit, 0);
    ns_atomic_init(&g_rc_op_count, 0);
    g_rc_loop = NULL;
    srand((unsigned int)9999);

    INTEGRATION_PHASE("random_chaos: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_rc_loop = integration_create_loop(NS_CAPACITY_65536, "rand_chaos");
    EXPECT_OK(g_rc_loop != NULL);
    EXPECT_OK(ns_loop_start(g_rc_loop) == NS_OK);

    EXPECT_OK(ns_signal_init_raw(&g_rc_signal, 0u, 0u, "rc_sig") == NS_OK);
    EXPECT_OK(ns_signal_connect(&g_rc_signal, rc_dummy_slot, g_rc_loop, NULL, &g_rc_conn) == NS_OK);

    EXPECT_OK(ns_timer_create(&g_rc_timer, 50000u, NS_TIMER_ATTR_REPEAT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&g_rc_timer.signal, rc_dummy_slot, g_rc_loop, NULL, &g_rc_timer_conn) == NS_OK);
    EXPECT_OK(ns_timer_start(&g_rc_timer) == NS_OK);

    g_rc_raw = test_create_raw_waitable();
    EXPECT_OK(test_raw_waitable_is_valid(g_rc_raw));
#if defined(_WIN32)
    EXPECT_OK(ns_watcher_init_handle(&g_rc_watcher, g_rc_raw.handle, NS_WAITABLE_EVENT_IN, 1) == NS_OK);
#else
    EXPECT_OK(ns_watcher_init_fd(&g_rc_watcher, g_rc_raw.fd, NS_WAITABLE_EVENT_IN, 1) == NS_OK);
#endif
    EXPECT_OK(ns_signal_connect(&g_rc_watcher.signal, rc_dummy_slot, g_rc_loop, NULL, &g_rc_wconn) == NS_OK);
    EXPECT_OK(ns_broker_add(&g_rc_watcher) == NS_OK);

    INTEGRATION_PHASE("random_chaos: running 3s chaos");
    EXPECT_OK(rc_worker_start() == 0);

#if defined(_WIN32)
    Sleep(3000u);
#else
    { struct timespec ts = {3, 0}; (void)nanosleep(&ts, NULL); }
#endif

    ns_atomic_store_explicit(&g_rc_quit, 1, ns_memory_order_release);
    rc_worker_join();

    {
        int ops = ns_atomic_load_explicit(&g_rc_op_count, ns_memory_order_relaxed);
        INTEGRATION_STATS("random_chaos: %d operations, 0 crashes", ops);
    }

    EXPECT_OK(ns_loop_stop(g_rc_loop) == NS_OK);

    EXPECT_OK(ns_signal_disconnect(&g_rc_timer_conn) == NS_OK);
    EXPECT_OK(ns_timer_destroy(&g_rc_timer) == NS_OK);

    (void)ns_broker_remove(&g_rc_watcher);
    EXPECT_OK(ns_signal_disconnect(&g_rc_wconn) == NS_OK);
    EXPECT_OK(ns_watcher_deinit(&g_rc_watcher) == NS_OK);
    test_destroy_raw_waitable(g_rc_raw);

    EXPECT_OK(ns_signal_disconnect(&g_rc_conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&g_rc_signal) == NS_OK);

    EXPECT_OK(ns_loop_destroy(g_rc_loop) == NS_OK);
    g_rc_loop = NULL;

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("random_chaos: completed without crash");
    return 0;
}

#undef RAND_CHAOS_RUN_US
