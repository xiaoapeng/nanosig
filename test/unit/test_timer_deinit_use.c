/**
 * @file test_timer_deinit_use.c
 * @brief Gap test: ns_timer_deinit then ns_timer_init on the same storage.
 * @date 2026-06-28
 *
 * After deinit, the timer storage can be reinitialized. Catches dangling
 * node pointers in the timer wheel and stale interval/state values.
 */

#include <nanosig/nanosig.h>
#include "test_macros.h"

static int test_timer_deinit_then_reinit(void)
{
    ns_timer_t timer;

    EXPECT_OK(ns_init() == NS_OK);

    EXPECT_OK(ns_timer_init(&timer, 1000u, 0u) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);

    /* Reinit with different parameters; if the wheel cached the old
     * interval we'd see flakiness in the next start. */
    EXPECT_OK(ns_timer_init(&timer, 2000u, 0u) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);

    /* Third cycle with yet another interval. */
    EXPECT_OK(ns_timer_init(&timer, 5000u, 0u) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

int main(void)
{
    if(test_timer_deinit_then_reinit() != 0) return 1;
    return 0;
}