/**
 * @file test_broker_concurrent_remove.c
 * @brief Gap test: ns_broker_add/remove in a tight loop must not corrupt broker state.
 * @date 2026-06-28
 *
 * The broker walks its watcher list when dispatching events. If add() and
 * remove() are not properly serialized internally, a quick succession of
 * add → remove pairs could leave dangling list nodes, leak waitables, or
 * crash on the next dispatch.
 *
 * This test does not attempt to race against the dispatch thread (that would
 * require a loop running); it instead exercises the broker's add/remove
 * locking in isolation across many iterations.
 */

#include <stdio.h>

#include <nanosig/nanosig.h>
#include "test_macros.h"
#include "test_helpers.h"

#define STRESS_ITERATIONS 1000u

static int test_broker_add_remove_stress(void)
{
    ns_watcher_t watcher;
    ns_waitable_handle_t h;
    unsigned i;

    EXPECT_OK(ns_init() == NS_OK);

    watcher.waitable = test_create_raw_waitable();
    if(!test_raw_waitable_is_valid(watcher.waitable)){
        fprintf(stderr, "raw waitable creation failed\n");
        return 1;
    }
    h = NS_WAITABLE_GET(&watcher.waitable);
    EXPECT_OK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, NULL) == NS_OK);

    /* Repeatedly add and remove. ns_broker_add returns NS_E_EXISTS after
     * first successful add; we expect either NS_OK or NS_E_EXISTS, not
     * anything weirder (leak, corruption, crash). */
    for(i = 0u; i < STRESS_ITERATIONS; ++i){
        int rc;

        rc = ns_broker_add(&watcher);
        if((rc != NS_OK) && (rc != NS_E_EXISTS)){
            fprintf(stderr, "ns_broker_add returned unexpected rc=%d at iter %u\n",
                    rc, i);
            return 1;
        }

        rc = ns_broker_remove(&watcher);
        if(rc != NS_OK){
            fprintf(stderr, "ns_broker_remove returned rc=%d at iter %u\n", rc, i);
            return 1;
        }
    }

    EXPECT_OK(ns_watcher_deinit(&watcher) == NS_OK);
    test_destroy_raw_waitable(watcher.waitable);

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

int main(void)
{
    if(test_broker_add_remove_stress() != 0) return 1;
    return 0;
}