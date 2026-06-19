/**
 * @file demo_cross_thread.c
 * @brief 跨线程 signal/slot 验收 demo。
 *
 * main 线程发任务给 worker 线程，worker 处理完后通过另一个 signal 发回 main。
 * 双向信号流：main → worker_loop → worker 处理 → emit done → main_loop → main 验证。
 *
 * ns_loop_start 启动 worker 的后台线程；main 线程自己跑 ns_loop_run 阻塞等待结果。
 */

#include <nanosig/nanosig.h>

#include <assert.h>

typedef struct work_payload {
    unsigned sequence;
    const char *source;
} work_payload_t;

ns_signal_t g_work_ready;   /* main → worker：任务信号 */
ns_signal_t g_done;         /* worker → main：完成信号 */

static unsigned g_last_sequence;

static void on_work(void *user_data, const work_payload_t *payload)
{
    (void)user_data;

    g_last_sequence = payload->sequence;
    (void)ns_signal_emit(g_done, NS_NO_PAYLOAD);   /* 通知 main 线程任务已完成 */
}

static void on_done(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    ns_loop_t *main_loop = (ns_loop_t *)user_data;
    ns_loop_quit(main_loop);   /* main_loop 可以返回了 */
}

int main(void)
{
    ns_loop_t *main_loop = NULL;
    ns_loop_t *worker_loop = NULL;
    ns_connection_t work_conn;
    ns_connection_t done_conn;
    int rc;

    rc = ns_init();
    if(rc != NS_OK) return 1;

    /* 初始化两个 signal */
    rc = ns_signal_init(&g_work_ready, work_payload_t);
    if(rc != NS_OK) goto out_shutdown;

    rc = ns_signal_init(&g_done, ns_no_payload_t);
    if(rc != NS_OK) goto out_work_signal;

    /* 创建两个 loop */
    ns_loop_config_t main_cfg = NS_LOOP_CONFIG_DEFAULT();
    main_cfg.debug_name = "main-loop";
    rc = ns_loop_create(&main_loop, &main_cfg);
    if(rc != NS_OK) goto out_done_signal;

    ns_loop_config_t worker_cfg = NS_LOOP_CONFIG_DEFAULT();
    worker_cfg.debug_name = "worker-loop";
    rc = ns_loop_create(&worker_loop, &worker_cfg);
    if(rc != NS_OK) goto out_main_loop;

    /* connect work_ready → worker_loop */
    rc = ns_signal_connect_typed(g_work_ready, on_work, work_payload_t, worker_loop, NULL, &work_conn);
    if(rc != NS_OK) goto out_worker_loop;

    /* connect done → main_loop */
    rc = ns_signal_connect_typed(g_done, on_done, ns_no_payload_t, main_loop, main_loop, &done_conn);
    if(rc != NS_OK) goto out_work_conn;

    /* 后台线程启动 worker loop */
    rc = ns_loop_start(worker_loop);
    if(rc != NS_OK) goto out_done_conn;

    /* 发任务给 worker（入 worker 的 MPSC 队列，非阻塞） */
    work_payload_t payload = {
        .sequence = 1u,
        .source = "main"
    };
    rc = ns_signal_emit(g_work_ready, &payload);
    if(rc != NS_OK) goto out_worker_started;

    /*
     * main 线程阻塞等结果。
     * worker 处理后 emit done → 入 main 队列 → on_done quit 此 loop → 返回。
     */
    ns_loop_run(main_loop);

    assert(g_last_sequence == 1u);

out_worker_started:
    (void)ns_loop_stop(worker_loop);
out_done_conn:
    (void)ns_signal_disconnect(&done_conn);
out_work_conn:
    (void)ns_signal_disconnect(&work_conn);
out_worker_loop:
    (void)ns_loop_destroy(worker_loop);
out_main_loop:
    (void)ns_loop_destroy(main_loop);
out_done_signal:
    (void)ns_signal_deinit(g_done);
out_work_signal:
    (void)ns_signal_deinit(g_work_ready);
out_shutdown:
    (void)ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
