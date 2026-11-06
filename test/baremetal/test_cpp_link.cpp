#include <stdint.h>

#include "test_macros.h"

#include <nanosig_baremetal_api.h>

static uint64_t now_us = 1234567u;

static uint64_t clock_now_us(void)
{
    return now_us;
}

int main(void)
{
    ns_platform_time_us_t observed = 0u;

    ns_baremetal_set_now_us(clock_now_us);
    EXPECT_OK(ns_platform_init() == NS_OK);
    EXPECT_OK(ns_platform_clock_monotonic_us(&observed) == NS_OK);
    EXPECT_EQ(observed, now_us);

    now_us += 100u;
    EXPECT_OK(ns_platform_clock_monotonic_us(&observed) == NS_OK);
    EXPECT_EQ(observed, now_us);
    EXPECT_OK(ns_platform_shutdown() == NS_OK);
    ns_baremetal_set_now_us(NULL);
    return 0;
}
