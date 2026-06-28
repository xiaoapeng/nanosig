/**
 * @file test_signal_null_slot.c
 * @brief Gap test: ns_signal_connect with slot_fn == NULL must not segfault.
 * @date 2026-06-28
 *
 * Without this guard, calling ns_signal_connect with a NULL slot function
 * would crash inside the next emit() because the broker would dereference
 * a null function pointer.
 */

#include <nanosig/nanosig.h>
#include "test_macros.h"

static int test_signal_connect_null_slot(void)
{
    ns_signal_t sig;
    ns_loop_t *loop = NULL;
    ns_connection_t conn;
    int rc;

    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_signal_init(&sig, ns_no_payload_t) == NS_OK);
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);

    /* Connecting with NULL slot_fn must fail cleanly. The implementation
     * is free to pick any negative status code, but it must not return
     * NS_OK and must not segfault. */
    rc = ns_signal_connect(&sig, NULL, loop, NULL, &conn);
    if(rc == NS_OK){
        fprintf(stderr, "ns_signal_connect(NULL slot) unexpectedly returned NS_OK\n");
        return 1;
    }

    EXPECT_OK(ns_signal_disconnect_all(&sig) == NS_OK);
    EXPECT_OK(ns_signal_deinit(&sig) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

int main(void)
{
    if(test_signal_connect_null_slot() != 0) return 1;
    return 0;
}