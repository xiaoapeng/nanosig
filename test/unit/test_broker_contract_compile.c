#include <stdint.h>

#include <nanosig/nanosig.h>
static void broker_contract_check_types(void)
{
    ns_event_broker_t *broker = NULL;
    ns_watcher_t watcher;
    ns_watcher_event_t event;

    ns_waitable_init(&watcher.waitable);
    watcher.waitable.events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR;
    watcher.waitable.edge_triggered = 1;
    watcher.waitable.user_data = &watcher;
    watcher.waitable.primitive.fd = 0;
    watcher.waitable.primitive.handle = (void *)0;
    watcher.waitable.primitive.event_bit = 0;

    event.triggered_events = NS_WAITABLE_EVENT_IN;
    event.consume_handle = NULL;

    (void)broker;
    (void)event;
    { ns_waitable_handle_t h = {.fd = 0}; (void)ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, NULL); }
    { ns_waitable_handle_t h = {.handle = (void *)(uintptr_t)1u}; (void)ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, NULL); }
    (void)ns_watcher_deinit(&watcher);
    (void)ns_broker_add(&watcher);
    (void)ns_broker_remove(&watcher);
}

int main(void)
{
    broker_contract_check_types();
    return 0;
}
