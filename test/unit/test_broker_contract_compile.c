#include <nanosig/nanosig.h>

#include <stdint.h>

static void broker_contract_check_types(void)
{
    ns_event_broker_t *broker = ns_broker();
    ns_watcher_t watcher;
    ns_watcher_event_t event;

    watcher.waitable = ns_waitable_init();
    watcher.waitable.events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR;
    watcher.waitable.edge_triggered = 1;
    watcher.waitable.user_data = &watcher;
    watcher.waitable.fd = 0;
    watcher.waitable.handle = (void *)0;
    watcher.waitable.event_bit = 0;

    event.triggered_events = NS_WAITABLE_EVENT_IN;

    (void)broker;
    (void)event;
    (void)ns_watcher_init_fd(&watcher, 0, NS_WAITABLE_EVENT_IN, 0);
    (void)ns_watcher_init_handle(&watcher, (void *)(uintptr_t)1u, NS_WAITABLE_EVENT_IN, 0);
    (void)ns_watcher_deinit(&watcher);
    (void)ns_broker_add(broker, &watcher);
    (void)ns_broker_remove(broker, &watcher);
}

int main(void)
{
    broker_contract_check_types();
    return 0;
}
