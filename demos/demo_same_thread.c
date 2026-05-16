#include <nanosig/nanosig.h>

typedef struct sample_payload {
    int value;
    const char *label;
} sample_payload_t;

NS_SIGNAL_DEFINE(sample_ready, sample_payload_t);
NS_SIGNAL_DEFINE(shutdown_requested, ns_no_payload_t);

static void on_sample(void *user_data, const sample_payload_t *payload)
{
    int *last_value = (int *)user_data;
    *last_value = payload->value;
}

static void on_shutdown(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    int *seen = (int *)user_data;
    *seen = 1;
}

int main(void)
{
    int rc = ns_init();
    if(rc != NS_OK) {
        return 1;
    }

    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    cfg.debug_name = "main-loop";

    ns_loop_t *loop = NULL;
    rc = ns_loop_create(&loop, &cfg);
    if(rc != NS_OK) {
        goto out_shutdown;
    }

    int last_value = 0;
    ns_connection_t *connection = NULL;
    rc = ns_signal_connect_typed(sample_ready, on_sample, sample_payload_t, &last_value, &connection);
    if(rc != NS_OK) {
        goto out_loop;
    }

    int shutdown_seen = 0;
    ns_connection_t *shutdown_connection = NULL;
    rc = ns_signal_connect_typed(shutdown_requested, on_shutdown, ns_no_payload_t, &shutdown_seen, &shutdown_connection);
    if(rc != NS_OK) {
        goto out_connection;
    }

    sample_payload_t payload = {
        .value = 42,
        .label = "same-thread"
    };
    rc = ns_signal_emit(sample_ready, &payload);
    if(rc != NS_OK) {
        goto out_shutdown_connection;
    }

    rc = ns_signal_emit(shutdown_requested, NS_NO_PAYLOAD);
    if(rc != NS_OK) {
        goto out_shutdown_connection;
    }

out_shutdown_connection:
    (void)ns_signal_disconnect(shutdown_connection);
out_connection:
    (void)ns_signal_disconnect(connection);
out_loop:
    (void)ns_loop_destroy(loop);
out_shutdown:
    (void)ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
