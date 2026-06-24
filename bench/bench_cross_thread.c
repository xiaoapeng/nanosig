/**
 * @file bench_cross_thread.c
 * @brief 跨线程 emit → dispatch → slot 往返延迟 benchmark。
 *
 * 后台线程运行 receiver loop。主线程 emit 后等待 slot 回调。
 * 测量 "emit 入队 → wakeup → dispatch → slot 回调" 的往返时间。
 */

#include <nanosig/nanosig.h>
#include "bench_common.h"

#include <sched.h>

#define ITERATIONS 50000
#define WARMUP     1000

static ns_signal_t g_sig;
static ns_connection_t g_conn;
static volatile int g_done;

static void on_slot(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;
    (void)user_data;

    g_done = 1;
}

int main(void)
{
    ns_loop_t *loop = NULL;
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    bench_stats_t stats;
    uint64_t t0, t1;
    size_t i;

    cfg.debug_name = "cross-thread-bench";

    if(ns_init() != NS_OK){ fprintf(stderr, "ns_init failed\n"); return 1; }
    if(ns_signal_init(&g_sig, ns_no_payload_t) != NS_OK){ fprintf(stderr, "signal_init failed\n"); return 1; }
    if(ns_loop_init(&loop, &cfg) != NS_OK){ fprintf(stderr, "loop_create failed\n"); return 1; }
    if(ns_signal_connect(&g_sig, (ns_slot_fn)on_slot, loop, NULL, &g_conn) != NS_OK){ fprintf(stderr, "connect failed\n"); return 1; }

    if(bench_stats_init(&stats, "cross_thread_latency", ITERATIONS) != 0){ fprintf(stderr, "OOM\n"); return 1; }

    /* 启动后台 receiver loop */
    if(ns_loop_start(loop) != NS_OK){ fprintf(stderr, "loop_start failed\n"); return 1; }

    /* 预热 */
    for(i = 0u; i < WARMUP; i++){
        g_done = 0;
        (void)ns_signal_emit(g_sig, NS_NO_PAYLOAD);
        while(!g_done) sched_yield();
    }

    /* 正式测量 */
    for(i = 0u; i < ITERATIONS; i++){
        g_done = 0;
        t0 = bench_now_ns();
        (void)ns_signal_emit(g_sig, NS_NO_PAYLOAD);
        while(!g_done) sched_yield();
        t1 = bench_now_ns();
        bench_stats_record(&stats, t1 - t0);
    }

    bench_stats_report(&stats);

    (void)ns_loop_stop(loop);
    (void)ns_signal_disconnect(&g_conn);
    (void)ns_signal_deinit(&g_sig);
    (void)ns_loop_deinit(loop);
    (void)ns_shutdown();
    bench_stats_destroy(&stats);
    return 0;
}
