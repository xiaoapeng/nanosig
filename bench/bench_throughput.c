/**
 * @file bench_throughput.c
 * @brief 持续 emit 吞吐 benchmark。
 *
 * 1 秒内向后台 receiver loop 持续 emit，统计 slot 回调被调用的次数。
 */

#include <nanosig/nanosig.h>
#include "bench_common.h"

#include <sched.h>

#define DURATION_US 1000000u  /* 1 秒 */

static ns_signal_t g_sig;
static ns_connection_t g_conn;
static volatile int g_count;

static void on_slot(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;
    (void)user_data;

    g_count++;
}

int main(void)
{
    ns_loop_t *loop = NULL;
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    uint64_t deadline, start;
    size_t emitted = 0u;

    cfg.debug_name = "throughput-bench";

    if(ns_init() != NS_OK){ fprintf(stderr, "ns_init failed\n"); return 1; }
    if(ns_signal_init(&g_sig, ns_no_payload_t) != NS_OK){ fprintf(stderr, "signal_init failed\n"); return 1; }
    if(ns_loop_init(&loop, &cfg) != NS_OK){ fprintf(stderr, "loop_create failed\n"); return 1; }
    if(ns_signal_connect(&g_sig, (ns_slot_fn)on_slot, loop, NULL, &g_conn) != NS_OK){ fprintf(stderr, "connect failed\n"); return 1; }
    if(ns_loop_start(loop) != NS_OK){ fprintf(stderr, "loop_start failed\n"); return 1; }

    /* 预热 */
    g_count = 0;
    {
        size_t i;
        for(i = 0u; i < 1000u; i++){
            (void)ns_signal_emit(g_sig, NS_NO_PAYLOAD);
        }
        while(g_count < 1000) sched_yield();
    }

    /* 1 秒持续 emit */
    g_count = 0;
    start = bench_now_ns();
    deadline = start + DURATION_US * 1000u;

    while(bench_now_ns() < deadline){
        (void)ns_signal_emit(g_sig, NS_NO_PAYLOAD);
        emitted++;
    }

    /* 等所有已入队的 drain 完 */
    while((int)emitted > g_count && bench_now_ns() < deadline + 500000000u){
        sched_yield();
    }

    fprintf(stdout, "\n=== nanosig bench: throughput ===\n");
    fprintf(stdout, "  duration:     %u us\n", (unsigned)DURATION_US);
    fprintf(stdout, "  emitted:      %zu\n", emitted);
    fprintf(stdout, "  processed:    %d\n", g_count);
    fprintf(stdout, "  emit rate:    %.0f emits/sec\n", (double)emitted * 1000000.0 / (double)DURATION_US);
    fprintf(stdout, "  drain rate:   %d emits/sec\n", g_count);
    fflush(stdout);

    (void)ns_loop_stop(loop);
    (void)ns_signal_disconnect(&g_conn);
    (void)ns_signal_deinit(&g_sig);
    (void)ns_loop_deinit(loop);
    (void)ns_shutdown();
    return 0;
}
