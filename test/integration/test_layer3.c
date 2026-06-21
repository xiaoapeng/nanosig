#include "test_macros.h"
#include "integration_helpers.h"

#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

#include "scenario_multi_route.c"
#include "scenario_timer_precision.c"
#include "scenario_cocktail.c"
#include "scenario_idle.c"
#include "scenario_lifecycle_marathon.c"
#include "scenario_cleanup_verify.c"

int main(void)
{
    int rc;

    INTEGRATION_PHASE("Layer-3: Behavior verification");

    if((rc = scenario_multi_route()) != 0)            return rc + 1;
    if((rc = scenario_timer_precision()) != 0)        return rc + 2;
    if((rc = scenario_cocktail()) != 0)                return rc + 3;
    if((rc = scenario_idle()) != 0)                    return rc + 4;
    if((rc = scenario_lifecycle_marathon()) != 0)      return rc + 5;
    if((rc = scenario_cleanup_verify()) != 0)           return rc + 6;

    INTEGRATION_PHASE("Layer-3: ALL PASSED");
    return 0;
}
