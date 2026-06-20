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

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_TEST_MACROS_H */
