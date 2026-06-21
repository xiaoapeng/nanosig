#include "test_macros.h"
#include "integration_helpers.h"

#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

#include "scenario_timer_storm.c"
#include "scenario_random_chaos.c"
#include "scenario_signal_storm.c"
#include "scenario_reentrant_race.c"

int main(void)
{
    int rc;

    INTEGRATION_PHASE("Layer-2: Stress tests");

    if((rc = scenario_timer_storm()) != 0)    return rc + 1;
    if((rc = scenario_random_chaos()) != 0)   return rc + 2;
    if((rc = scenario_signal_storm()) != 0)   return rc + 3;
    if((rc = scenario_reentrant_race()) != 0) return rc + 4;

    INTEGRATION_PHASE("Layer-2: ALL PASSED");
    return 0;
}
