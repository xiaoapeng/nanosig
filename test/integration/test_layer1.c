#include "test_macros.h"
#include "integration_helpers.h"

#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

#include "scenario_cascade.c"
#include "scenario_watchdog.c"
#include "scenario_dual_loop.c"
#include "scenario_lifecycle_chaos.c"

int main(void)
{
    int rc;

    INTEGRATION_PHASE("Layer-1: Basic complex combinations");

    if((rc = scenario_cascade()) != 0)         return rc + 1;
    if((rc = scenario_watchdog()) != 0)        return rc + 2;
    if((rc = scenario_dual_loop()) != 0)       return rc + 3;
    if((rc = scenario_lifecycle_chaos()) != 0) return rc + 4;

    INTEGRATION_PHASE("Layer-1: ALL PASSED");
    return 0;
}
