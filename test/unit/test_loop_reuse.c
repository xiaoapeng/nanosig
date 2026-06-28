/**
 * @file test_loop_reuse.c
 * @brief Gap test: ns_loop_init → ns_loop_deinit → ns_loop_init on the same slot.
 * @date 2026-06-28
 *
 * The loop handle holds internal OS resources (epoll fd / kqueue fd /
 * IOCP port). After deinit, the storage can be reused for a fresh loop.
 * This test catches state residue bugs (stale platform handles, dangling
 * atomic counters, leftover timer wheel nodes).
 */

#include <nanosig/nanosig.h>
#include "test_macros.h"

static int test_loop_init_deinit_init_reuse(void)
{
    ns_loop_t *loop = NULL;

    EXPECT_OK(ns_init() == NS_OK);

    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

    /* Reuse the storage slot for a fresh loop. */
    loop = NULL;
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

    /* Three-cycle check for deep state accumulation. */
    loop = NULL;
    EXPECT_OK(ns_loop_init(&loop, NULL) == NS_OK);
    EXPECT_OK(ns_loop_deinit(loop) == NS_OK);

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

int main(void)
{
    if(test_loop_init_deinit_init_reuse() != 0) return 1;
    return 0;
}