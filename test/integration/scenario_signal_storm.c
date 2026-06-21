/**
 * @file scenario_signal_storm.c
 * @brief 1 signal + 50 slots across 2 loops, 4 threads emitting.
 * @date 2026-06-20
 *
 * 1 signal → 50 slots (25 per loop). 4 threads each emit 250 times = 1000 total.
 * Each slot expects 1000 calls → total 50000.
 * Capacity: NS_CAPACITY_262144
 *
 * #included into test_layer2.c
 */

#define SIGSTORM_SLOTS_PER_LOOP 25u
#define SIGSTORM_NUM_THREADS 4u
#define SIGSTORM_EMITS_PER_THREAD (250u * integration_test_scale())
#define SIGSTORM_TOTAL_EMITS (SIGSTORM_NUM_THREADS * SIGSTORM_EMITS_PER_THREAD)
#define SIGSTORM_EXPECTED_PER_SLOT SIGSTORM_TOTAL_EMITS

static ns_signal_t g_ss_signal;
static ns_loop_t *g_ss_loop_a;
static ns_loop_t *g_ss_loop_b;
static ns_connection_t g_ss_conns[SIGSTORM_SLOTS_PER_LOOP * 2u];
static atomic_int g_ss_counts[SIGSTORM_SLOTS_PER_LOOP * 2u];
static atomic_int g_ss_threads_ready;
static atomic_int g_ss_threads_done;

static void ss_slot(void *user_data, const void *payload)
{
    size_t idx = (size_t)(intptr_t)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_ss_counts[idx], 1, ns_memory_order_relaxed);
}

#if defined(_WIN32)
static DWORD WINAPI ss_worker_main(LPVOID arg)
#else
static void *ss_worker_main(void *arg)
#endif
{
    int i;
    (void)arg;

    ns_atomic_fetch_add_explicit(&g_ss_threads_ready, 1, ns_memory_order_release);

    for(i = 0; i < SIGSTORM_EMITS_PER_THREAD; i++){
        (void)ns_signal_emit_raw(&g_ss_signal, NULL, 0u);
    }

    ns_atomic_fetch_add_explicit(&g_ss_threads_done, 1, ns_memory_order_release);

#if defined(_WIN32)
    return 0u;
#else
    return NULL;
#endif
}

#if defined(_WIN32)
static HANDLE g_ss_threads[SIGSTORM_NUM_THREADS];
#else
static pthread_t g_ss_threads[SIGSTORM_NUM_THREADS];
#endif

static int scenario_signal_storm(void)
{
    size_t i;
    int all_ok = 1;

    for(i = 0u; i < SIGSTORM_SLOTS_PER_LOOP * 2u; i++){
        ns_atomic_init(&g_ss_counts[i], 0);
    }
    ns_atomic_init(&g_ss_threads_ready, 0);
    ns_atomic_init(&g_ss_threads_done, 0);
    g_ss_loop_a = NULL;
    g_ss_loop_b = NULL;

    INTEGRATION_PHASE("signal_storm: init");
    EXPECT_OK(ns_init() == NS_OK);

    g_ss_loop_a = integration_create_loop(NS_CAPACITY_262144, "ss_a");
    EXPECT_OK(g_ss_loop_a != NULL);
    g_ss_loop_b = integration_create_loop(NS_CAPACITY_262144, "ss_b");
    EXPECT_OK(g_ss_loop_b != NULL);

    EXPECT_OK(ns_loop_start(g_ss_loop_a) == NS_OK);
    EXPECT_OK(ns_loop_start(g_ss_loop_b) == NS_OK);

    EXPECT_OK(ns_signal_init_raw(&g_ss_signal, 0u, 0u, "ss_sig") == NS_OK);

    /* Connect 25 slots to loop A, 25 to loop B */
    for(i = 0u; i < SIGSTORM_SLOTS_PER_LOOP; i++){
        EXPECT_OK(ns_signal_connect(&g_ss_signal, ss_slot, g_ss_loop_a,
                                    (void *)(intptr_t)i, &g_ss_conns[i]) == NS_OK);
    }
    for(i = 0u; i < SIGSTORM_SLOTS_PER_LOOP; i++){
        size_t idx = SIGSTORM_SLOTS_PER_LOOP + i;
        EXPECT_OK(ns_signal_connect(&g_ss_signal, ss_slot, g_ss_loop_b,
                                    (void *)(intptr_t)idx, &g_ss_conns[idx]) == NS_OK);
    }

    INTEGRATION_PHASE("signal_storm: spawning %d threads x %d emits",
                      SIGSTORM_NUM_THREADS, SIGSTORM_EMITS_PER_THREAD);

    /* Start worker threads */
    for(i = 0u; i < SIGSTORM_NUM_THREADS; i++){
#if defined(_WIN32)
        g_ss_threads[i] = CreateThread(NULL, 0u, ss_worker_main, NULL, 0u, NULL);
        EXPECT_OK(g_ss_threads[i] != NULL);
#else
        EXPECT_OK(pthread_create(&g_ss_threads[i], NULL, ss_worker_main, NULL) == 0);
#endif
    }

    /* Wait for all threads */
    for(i = 0u; i < SIGSTORM_NUM_THREADS; i++){
#if defined(_WIN32)
        WaitForSingleObject(g_ss_threads[i], 10000u);
        CloseHandle(g_ss_threads[i]);
#else
        (void)pthread_join(g_ss_threads[i], NULL);
#endif
    }

    /* Give async dispatch time to complete */
#if defined(_WIN32)
    Sleep(1000u);
#else
    { struct timespec ts = {1, 0}; (void)nanosleep(&ts, NULL); }
#endif

    /* Verify counts */
    INTEGRATION_PHASE("signal_storm: verifying %zu slot counts",
                      SIGSTORM_SLOTS_PER_LOOP * 2u);
    for(i = 0u; i < SIGSTORM_SLOTS_PER_LOOP * 2u; i++){
        int cnt = ns_atomic_load_explicit(&g_ss_counts[i], ns_memory_order_acquire);
        if(cnt != SIGSTORM_EXPECTED_PER_SLOT){
            INTEGRATION_STATS("signal_storm: slot[%zu] = %d (expected %d)",
                              i, cnt, SIGSTORM_EXPECTED_PER_SLOT);
            all_ok = 0;
        }
    }

    /* Cleanup */
    for(i = 0u; i < SIGSTORM_SLOTS_PER_LOOP * 2u; i++){
        EXPECT_OK(ns_signal_disconnect(&g_ss_conns[i]) == NS_OK);
    }
    EXPECT_OK(ns_signal_deinit_raw(&g_ss_signal) == NS_OK);

    EXPECT_OK(ns_loop_stop(g_ss_loop_a) == NS_OK);
    EXPECT_OK(ns_loop_stop(g_ss_loop_b) == NS_OK);
    EXPECT_OK(ns_loop_destroy(g_ss_loop_a) == NS_OK);
    EXPECT_OK(ns_loop_destroy(g_ss_loop_b) == NS_OK);
    g_ss_loop_a = NULL;
    g_ss_loop_b = NULL;

    integration_verify_clean_shutdown();
    if(all_ok){
        INTEGRATION_PASS("signal_storm: all %zu slots received %d emits",
                         SIGSTORM_SLOTS_PER_LOOP * 2u, SIGSTORM_EXPECTED_PER_SLOT);
    }
    return all_ok ? 0 : 1;
}

#undef SIGSTORM_SLOTS_PER_LOOP
#undef SIGSTORM_NUM_THREADS
#undef SIGSTORM_EMITS_PER_THREAD
#undef SIGSTORM_TOTAL_EMITS
#undef SIGSTORM_EXPECTED_PER_SLOT
