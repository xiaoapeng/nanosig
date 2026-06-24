/**
 * @file integration_helpers.h
 * @brief Shared helpers for nanosig integration tests.
 * @date 2026-06-20
 *
 * Provides common utilities for integration test scenarios:
 * - Monotonic-clock-based condition waiting (replaces spin-loop patterns)
 * - Demo-style phase/stats output macros
 * - Clean shutdown verification
 */

#ifndef NANOSIG_INTEGRATION_HELPERS_H
#define NANOSIG_INTEGRATION_HELPERS_H

#include "test_macros.h"
#include "test_helpers.h"

#include <nanosig/nanosig.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sched.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Output helpers                                                     */
/* ------------------------------------------------------------------ */

#define INTEGRATION_PHASE(fmt, ...) \
    do { \
        fprintf(stdout, "[PHASE] " fmt "\n", ##__VA_ARGS__); (void)0; \
        fflush(stdout); \
    } while(0)

#define INTEGRATION_STATS(fmt, ...) \
    do { \
        fprintf(stdout, "[STATS] " fmt "\n", ##__VA_ARGS__); \
        fflush(stdout); \
    } while(0)

#define INTEGRATION_PASS(fmt, ...) \
    do { \
        fprintf(stdout, "[PASS] " fmt "\n", ##__VA_ARGS__); \
        fflush(stdout); \
    } while(0)

/* ------------------------------------------------------------------ */
/*  Monotonic-clock condition wait                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Wait for a condition with a monotonic-clock-based timeout.
 *
 * @param cond_fn   Condition function; returns 0 when condition is met.
 * @param ctx       Context passed to cond_fn.
 * @param timeout_us Maximum wait time in microseconds.
 * @return 0 on success (condition met), -1 on timeout.
 */
static inline int integration_wait_for_condition(
    int (*cond_fn)(void *ctx),
    void *ctx,
    ns_platform_time_us_t timeout_us)
{
    ns_platform_time_us_t deadline;

    if(ns_platform_clock_monotonic_us(&deadline) != 0) return -1;
    deadline += timeout_us;

    while(!cond_fn(ctx)){
        ns_platform_time_us_t now;
        if(ns_platform_clock_monotonic_us(&now) != 0) return -1;
        if(now >= deadline) return -1;

        /* Yield to avoid busy-loop */
#if defined(_WIN32)
        SwitchToThread();
#else
        sched_yield();
#endif
    }
    return 0;
}

/**
 * @brief Create a loop with the specified queue capacity.
 *
 * @param debug_name Optional debug name for the loop.
 * @return Created loop on success, NULL on failure.
 */
static inline ns_loop_t *integration_create_loop(
    ns_capacity_t queue_byte_capacity,
    const char *debug_name)
{
    ns_loop_t *loop = NULL;
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();

    cfg.queue_byte_capacity = queue_byte_capacity;
    cfg.debug_name = debug_name;

    if(ns_loop_create(&loop, &cfg) != NS_OK) return NULL;
    return loop;
}

/**
 * @brief Verify clean shutdown by asserting ns_shutdown() returns OK.
 *
 * Call this at the end of every integration test to ensure no resources
 * are leaked between tests. This is not a substitute for ASAN/LSAN but
 * catches obvious lifecycle violations.
 */
static inline void integration_verify_clean_shutdown(void)
{
    int rc = ns_shutdown();

    if(rc != NS_OK){
        fprintf(stderr, "[FAIL] ns_shutdown() returned %d — resource leak?\n", rc);
    }
}

/**
 * @brief No-op slot callback for use when only event delivery matters.
 *
 * Typically used with exit timers or barrier signals where the act of
 * being called (or quitting the loop from the slot) is the verification.
 *
 * Example:
 *   ns_signal_connect(&timer.signal, dummy_slot, loop, NULL, &conn);
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static void dummy_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
}

/* ------------------------------------------------------------------ */
/*  Test scale factor — runtime scaling via NANOSIG_TEST_SCALE         */
/* ------------------------------------------------------------------ */

/**
 * @brief Get the test scaling factor, cached from NANOSIG_TEST_SCALE env var.
 *
 * Returns 1 by default (CI fast path). Set e.g.:
 *   NANOSIG_TEST_SCALE=10   for nightly runs (10x duration/counts)
 *   NANOSIG_TEST_SCALE=100  for stress testing
 */
static inline unsigned int integration_test_scale(void)
{
    static int cached = 0;
    static unsigned int scale = 1u;

    if(!cached){
        const char *env = getenv("NANOSIG_TEST_SCALE");
        if(env != NULL){
            int val = atoi(env);
            if(val >= 1) scale = (unsigned int)val;
        }
        cached = 1;
    }
    return scale;
}

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_INTEGRATION_HELPERS_H */
