/**
 * @file scenario_cleanup_verify.c
 * @brief Minimal cleanup verification scenario.
 * @date 2026-06-20
 *
 * Creates a minimal broker/loop instance and verifies ns_shutdown()
 * returns NS_OK, confirming all resources are properly released.
 * This is a standalone sanity check; integration_verify_clean_shutdown()
 * is already called at the end of every integration test.
 *
 * #included into test_layer3.c
 */

#include "test_macros.h"
#include "integration_helpers.h"

static int scenario_cleanup_verify(void)
{
    INTEGRATION_PHASE("cleanup: init and immediate shutdown");
    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    /* Re-init to verify second cycle is also clean */
    INTEGRATION_PHASE("cleanup: second init/shutdown cycle");
    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);

    INTEGRATION_PASS("cleanup: double init/shutdown cycle clean");
    return 0;
}

#ifdef SCENARIO_MAIN
int main(void) { return scenario_cleanup_verify(); }
#endif
