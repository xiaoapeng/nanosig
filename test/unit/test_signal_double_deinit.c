/**
 * @file test_signal_double_deinit.c
 * @brief Gap test: ns_signal_deinit called twice on the same signal must not crash.
 * @date 2026-06-28
 *
 * The second deinit is operating on a signal whose internal mutex has already
 * been freed. A naive implementation would either double-free the mutex or
 * dereference a dangling pointer. This test exercises both deinit paths and
 * asserts that calling deinit twice doesn't crash. The exact return code of
 * the second deinit is implementation-defined and intentionally not asserted.
 */

#include <nanosig/nanosig.h>
#include "test_macros.h"

static int test_signal_double_deinit_safe(void)
{
    ns_signal_t sig;
    int rc;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_signal_init(&sig, ns_no_payload_t) == NS_OK);

    EXPECT_OK(ns_signal_deinit(&sig) == NS_OK);

    /* Second deinit must not crash. Capture rc but don't assert on it;
     * the contract is "doesn't segfault, doesn't access freed memory". */
    rc = ns_signal_deinit(&sig);
    (void)rc;

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

int main(void)
{
    if(test_signal_double_deinit_safe() != 0) return 1;
    return 0;
}