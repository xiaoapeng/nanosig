/**
 * @file test_macros.h
 * @brief Shared test macros for nanosig tests.
 * @date 2026-06-20
 *
 * Extracted from test_signal.c / test_timer.c / test_broker.c to eliminate
 * code duplication across all unit and integration test files.
 *
 * EXPECT_EQ uses %lld + (long long) to safely handle all integer sizes
 * on 64-bit platforms without truncation.
 */

#ifndef NANOSIG_TEST_MACROS_H
#define NANOSIG_TEST_MACROS_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int expect_true(int condition)
{
    return condition ? 0 : 1;
}

#define EXPECT_OK(expr) \
    do { \
        if(expect_true((expr)) != 0){ \
            fprintf(stderr, "EXPECT failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while(0)

#define EXPECT_EQ(a, b) \
    do { \
        if(expect_true((a) == (b)) != 0){ \
            fprintf(stderr, "EXPECT failed at %s:%d: %s == %s (%lld != %lld)\n", \
                __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
            return 1; \
        } \
    } while(0)

/**
 * @brief Non-fatal EXPECT_OK — accumulates error count instead of returning.
 *
 * Usage:
 *   int test_foo(void) {
 *       int err = 0;
 *       EXPECT_OK_CONT(ns_init() == NS_OK, err);
 *       EXPECT_OK_CONT(ns_shutdown() == NS_OK, err);
 *       return err > 0;   // report as failure if any assertion failed
 *   }
 */
#define EXPECT_OK_CONT(expr, err_count) \
    do { \
        if(expect_true((expr)) != 0){ \
            fprintf(stderr, "EXPECT failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            (err_count)++; \
        } \
    } while(0)

/**
 * @brief Non-fatal EXPECT_EQ — accumulates error count instead of returning.
 *
 * Usage:
 *   int test_foo(void) {
 *       int err = 0;
 *       EXPECT_EQ_CONT(actual, expected, err);
 *       return err > 0;
 *   }
 */
#define EXPECT_EQ_CONT(a, b, err_count) \
    do { \
        if(expect_true((a) == (b)) != 0){ \
            fprintf(stderr, "EXPECT failed at %s:%d: %s == %s (%lld != %lld)\n", \
                __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
            (err_count)++; \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_TEST_MACROS_H */
