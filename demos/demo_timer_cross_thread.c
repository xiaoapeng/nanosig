/**
 * @file demo_timer_cross_thread.c
 * @brief Timer 跨线程 signal/slot 验收 demo。
 *
 * broker 线程每 100ms fire timer → emit 到 worker loop → worker 处理 →
 * 累计 10 次后发 done signal 回 main loop → main loop 退出。
 *
 * 完整链路：broker timer → worker dispatch → worker emit → main dispatch → quit。
 */

#include <nanosig/nanosig.h>

#include <assert.h>

ns_signal_t g_done;   /* worker → main：完成信号 */

static unsigned g_seen;

static void on_tick(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    ns_loop_t *main_loop = (ns_loop_t *)user_data;

    g_seen++;
    if(g_seen >= 10u){
        (void)ns_signal_emit(g_done, NS_NO_PAYLOAD);   /* 通知 main 线程 */
        (void)main_loop;
    }
}

static void on_done(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    ns_loop_t *main_loop = (ns_loop_t *)user_data;
    ns_loop_quit(main_loop);
}

int main(void)
{
    ns_loop_t *main_loop = NULL;
    ns_loop_t *worker_loop = NULL;
    ns_timer_t timer;
    ns_connection_t tick_conn;
    ns_connection_t done_conn;
    int rc;

    rc = ns_init();
    if(rc != NS_OK) return 1;

    /* init done signal */
    rc = ns_signal_init(&g_done, ns_no_payload_t);
    if(rc != NS_OK) goto out_shutdown;

    /* 创建两个 loop */
    ns_loop_config_t main_cfg = NS_LOOP_CONFIG_DEFAULT();
    main_cfg.debug_name = "main-loop";
    rc = ns_loop_init(&main_loop, &main_cfg);
    if(rc != NS_OK) goto out_done_signal;

    ns_loop_config_t worker_cfg = NS_LOOP_CONFIG_DEFAULT();
    worker_cfg.debug_name = "worker-loop";
    rc = ns_loop_init(&worker_loop, &worker_cfg);
    if(rc != NS_OK) goto out_main_loop;

    /* timer: 100ms repeat */
    rc = ns_timer_init(&timer, 100000u, NS_TIMER_ATTR_REPEAT);
    if(rc != NS_OK) goto out_worker_loop;

    /* connect timer.signal → worker_loop */
    rc = ns_signal_connect_typed(timer.signal, on_tick, ns_no_payload_t,
                                  worker_loop, main_loop, &tick_conn);
    if(rc != NS_OK) goto out_timer;

    /* connect done → main_loop */
    rc = ns_signal_connect_typed(g_done, on_done, ns_no_payload_t,
                                  main_loop, main_loop, &done_conn);
    if(rc != NS_OK) goto out_tick_conn;

    /* 启动 timer（broker 开始计时） */
    rc = ns_timer_start(&timer);
    if(rc != NS_OK) goto out_done_conn;

    /* 后台线程启动 worker loop */
    rc = ns_loop_start(worker_loop);
    if(rc != NS_OK) goto out_timer_started;

    /*
     * main loop 阻塞等待。
     * worker 累计 10 次 tick 后 emit done → on_done quit main loop → 返回。
     */
    (void)ns_loop_run(main_loop);

    assert(g_seen >= 10u);

out_timer_started:
    (void)ns_loop_stop(worker_loop);
out_done_conn:
    (void)ns_signal_disconnect(&done_conn);
out_tick_conn:
    (void)ns_timer_cancel(&timer);
    (void)ns_signal_disconnect(&tick_conn);
out_timer:
    (void)ns_timer_deinit(&timer);
out_worker_loop:
    (void)ns_loop_deinit(worker_loop);
out_main_loop:
    (void)ns_loop_deinit(main_loop);
out_done_signal:
    (void)ns_signal_deinit(&g_done);
out_shutdown:
    (void)ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
