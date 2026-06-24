/**
 * @file bench_same_thread.c
 * @brief 同线程 emit → dispatch → slot 全链路延迟 benchmark。
 *
 * 每次 iteration：emit → ns_loop_run（slot 内 quit）→ 返回。
 * 测量 "emit 入队 → dispatch → slot 回调" 的完整时间。
 */

#include <nanosig/nanosig.h>
#include "bench_common.h"

#define ITERATIONS 100000
#define WARMUP     1000

static ns_signal_t g_sig;
static ns_connection_t g_conn;

static void on_slot(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    ns_loop_t *loop = (ns_loop_t *)user_data;
    ns_loop_quit(loop);
}

int main(void)
{
    ns_loop_t *loop = NULL;
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    bench_stats_t stats;
    uint64_t t0, t1;
    size_t i;

    cfg.debug_name = "same-thread-bench";

    if(ns_init() != NS_OK){ fprintf(stderr, "ns_init failed\n"); return 1; }
    if(ns_signal_init(&g_sig, ns_no_payload_t) != NS_OK){ fprintf(stderr, "signal_init failed\n"); return 1; }
    if(ns_loop_init(&loop, &cfg) != NS_OK){ fprintf(stderr, "loop_create failed\n"); return 1; }
    if(ns_signal_connect(&g_sig, (ns_slot_fn)on_slot, loop, loop, &g_conn) != NS_OK){ fprintf(stderr, "connect failed\n"); return 1; }

    if(bench_stats_init(&stats, "same_thread_latency", ITERATIONS) != 0){ fprintf(stderr, "OOM\n"); return 1; }

    /* 预热 */
    for(i = 0u; i < WARMUP; i++){
        (void)ns_signal_emit(g_sig, NS_NO_PAYLOAD);
        (void)ns_loop_run(loop);
    }

    /* 正式测量 */
    for(i = 0u; i < ITERATIONS; i++){
        t0 = bench_now_ns();
        (void)ns_signal_emit(g_sig, NS_NO_PAYLOAD);
        (void)ns_loop_run(loop);
        t1 = bench_now_ns();
        bench_stats_record(&stats, t1 - t0);
    }

    bench_stats_report(&stats);

    (void)ns_signal_disconnect(&g_conn);
    (void)ns_signal_deinit(&g_sig);
    (void)ns_loop_deinit(loop);
    (void)ns_shutdown();
    bench_stats_destroy(&stats);
    return 0;
}
