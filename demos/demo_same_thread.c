/**
 * @file demo_same_thread.c
 * @brief 同线程 signal/slot 验收 demo。
 *
 * emit 和 slot 在同一线程执行：emit 入队 → ns_loop_run drain → slot 回调处理。
 * ns_loop_run 在最后一个 slot 回调中调用 ns_loop_quit 后返回。
 */

#include <nanosig/nanosig.h>

#include <assert.h>

typedef struct sample_payload {
    int value;
    const char *label;
} sample_payload_t;

typedef struct demo_ctx {
    ns_loop_t *loop;
    int        last_value;
    int        shutdown_seen;
} demo_ctx_t;

ns_signal_t sample_ready;
ns_signal_t shutdown_requested;

static void on_sample(void *user_data, const sample_payload_t *payload)
{
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    ctx->last_value = payload->value;
}

static void on_shutdown(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    ctx->shutdown_seen = 1;
    ns_loop_quit(ctx->loop);
}

int main(void)
{
    demo_ctx_t ctx;
    int rc;

    ctx.loop = NULL;
    ctx.last_value = 0;
    ctx.shutdown_seen = 0;

    rc = ns_init();
    if(rc != NS_OK) return 1;

    rc = ns_signal_init(&sample_ready, sample_payload_t);
    if(rc != NS_OK) goto out_shutdown;

    rc = ns_signal_init(&shutdown_requested, ns_no_payload_t);
    if(rc != NS_OK) goto out_sample_signal;

    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    cfg.debug_name = "main-loop";

    ns_loop_t *loop = NULL;
    rc = ns_loop_init(&loop, &cfg);
    if(rc != NS_OK) goto out_shutdown_signal;

    ctx.loop = loop;

    ns_connection_t connection;
    rc = ns_signal_connect_typed(sample_ready, on_sample, sample_payload_t, loop, &ctx, &connection);
    if(rc != NS_OK) goto out_loop;

    ns_connection_t shutdown_connection;
    rc = ns_signal_connect_typed(shutdown_requested, on_shutdown, ns_no_payload_t, loop, &ctx, &shutdown_connection);
    if(rc != NS_OK) goto out_connection;

    /* emit 入队，ns_loop_run drain 时按顺序执行 slot */
    sample_payload_t payload = {
        .value = 42,
        .label = "same-thread"
    };
    rc = ns_signal_emit(sample_ready, &payload);
    if(rc != NS_OK) goto out_shutdown_connection;

    rc = ns_signal_emit(shutdown_requested, NS_NO_PAYLOAD);
    if(rc != NS_OK) goto out_shutdown_connection;

    /* drain：on_sample(ctx.last_value=42) → on_shutdown(seen=1, quit) → return */
    rc = ns_loop_run(loop);

    assert(ctx.last_value == 42);
    assert(ctx.shutdown_seen == 1);

out_shutdown_connection:
    (void)ns_signal_disconnect(&shutdown_connection);
out_connection:
    (void)ns_signal_disconnect(&connection);
out_loop:
    (void)ns_loop_deinit(loop);
out_shutdown_signal:
    (void)ns_signal_deinit(&shutdown_requested);
out_sample_signal:
    (void)ns_signal_deinit(&sample_ready);
out_shutdown:
    (void)ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
