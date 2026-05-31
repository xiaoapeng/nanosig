#include <nanosig/nanosig.h>

static void on_tick(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    unsigned *seen = (unsigned *)user_data;
    *seen += 1u;
}

int main(void)
{
    int rc = ns_init();
    if(rc != NS_OK) {
        return 1;
    }

    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    cfg.debug_name = "timer-target";

    ns_loop_t *target_loop = NULL;
    rc = ns_loop_create(&target_loop, &cfg);
    if(rc != NS_OK) {
        goto out_shutdown;
    }

    unsigned seen = 0u;
    ns_timer_t timer;
    ns_connection_t connection;

    rc = ns_timer_create(&timer, 100000u, NS_TIMER_ATTR_REPEAT);
    if(rc != NS_OK) {
        goto out_loop;
    }

    rc = ns_signal_connect_typed_to(timer.signal, on_tick, ns_no_payload_t, target_loop, &seen, &connection);
    if(rc != NS_OK) {
        goto out_timer;
    }

    rc = ns_timer_start(&timer);
    if(rc != NS_OK) {
        goto out_connection;
    }

    rc = ns_loop_run();

out_connection:
    (void)ns_timer_cancel(&timer);
    (void)ns_signal_disconnect(&connection);
out_timer:
    (void)ns_timer_destroy(&timer);
out_loop:
    (void)ns_loop_destroy();
out_shutdown:
    (void)ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
