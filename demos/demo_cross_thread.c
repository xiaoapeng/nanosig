#include <nanosig/nanosig.h>

typedef struct work_payload {
    unsigned sequence;
    const char *source;
} work_payload_t;

NS_SIGNAL_DEFINE(work_ready, work_payload_t);
NS_SIGNAL_DEFINE(consumer_wakeup, ns_no_payload_t);

static ns_loop_t *consumer_loop = NULL;
static ns_connection_t *connection = NULL;
static ns_connection_t *wakeup_connection = NULL;
static unsigned last_sequence = 0u;
static unsigned wakeup_count = 0u;

static void on_work(void *user_data, const work_payload_t *payload)
{
    unsigned *last = (unsigned *)user_data;
    *last = payload->sequence;
}

static void on_wakeup(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    unsigned *count = (unsigned *)user_data;
    *count += 1u;
}

static int consumer_thread_main(void)
{
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    cfg.debug_name = "consumer-B";

    int rc = ns_loop_create(&consumer_loop, &cfg);
    if(rc != NS_OK) {
        return rc;
    }

    rc = ns_signal_connect_typed_to(work_ready, on_work, work_payload_t, consumer_loop, &last_sequence, &connection);
    if(rc != NS_OK) {
        goto out_loop;
    }

    rc = ns_signal_connect_typed_to(consumer_wakeup, on_wakeup, ns_no_payload_t, consumer_loop, &wakeup_count, &wakeup_connection);
    if(rc != NS_OK) {
        goto out_connection;
    }

    rc = ns_loop_run(consumer_loop);

    (void)ns_signal_disconnect(wakeup_connection);
    wakeup_connection = NULL;
out_connection:
    (void)ns_signal_disconnect(connection);
    connection = NULL;
out_loop:
    (void)ns_loop_destroy(consumer_loop);
    consumer_loop = NULL;
    return rc;
}

static int producer_thread_main(void)
{
    work_payload_t payload = {
        .sequence = 1u,
        .source = "thread-A"
    };

    int rc = ns_signal_emit(work_ready, &payload);
    if(rc != NS_OK) {
        return rc;
    }

    return ns_signal_emit(consumer_wakeup, NS_NO_PAYLOAD);
}

int main(void)
{
    int rc = ns_init();
    if(rc != NS_OK) {
        return 1;
    }

    /*
     * PD sketch:
     * - a real P7 demo will start consumer_thread_main on thread B;
     * - producer_thread_main runs on thread A and emits into B's connected loop;
     * - each thread owns at most one ns_loop_t.
     * - after both threads join, static signals are deinitialized before
     *   global shutdown.
     */
    (void)consumer_thread_main;
    (void)producer_thread_main;

    (void)ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
