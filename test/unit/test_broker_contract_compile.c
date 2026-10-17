#include <stdint.h>

#include <nanosig/nanosig.h>

/* handler 必填，签名为 void (*)(ns_watcher_t *, uint32_t triggered_events)；
   下面这个函数的类型必须能直接赋给 ns_watcher_handler_fn。 */
static void broker_contract_handler(ns_watcher_t *watcher, uint32_t triggered_events)
{
    (void)watcher;
    (void)triggered_events;
}

static void broker_contract_check_types(void)
{
    ns_event_broker_t *broker = NULL;
    ns_watcher_t watcher;
    ns_watcher_handler_fn handler_ref = broker_contract_handler;

    ns_waitable_init(&watcher.waitable);
    watcher.waitable.events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR;
    watcher.waitable.edge_triggered = 1;
    watcher.waitable.user_data = &watcher;
    watcher.waitable.primitive = (ns_waitable_handle_t)0;

    ns_list_init(&watcher.broker_node);
    watcher.handler = broker_contract_handler;

    /* 结构断言（AC-8）：waitable 首字段，handler 为最后字段，无内嵌 signal。 */
    NS_STATIC_ASSERT(ns_offsetof(ns_watcher_t, waitable) == 0u,
                     "ns_watcher_t.waitable must be the first member");
    NS_STATIC_ASSERT(ns_offsetof(ns_watcher_t, broker_node) > ns_offsetof(ns_watcher_t, waitable),
                     "ns_watcher_t.broker_node must follow waitable");
    NS_STATIC_ASSERT(ns_offsetof(ns_watcher_t, handler) > ns_offsetof(ns_watcher_t, broker_node),
                     "ns_watcher_t.handler must be the last member");

    (void)broker;
    (void)handler_ref;
    (void)ns_watcher_handle(&watcher);
    { ns_waitable_handle_t h = (ns_waitable_handle_t)0; (void)ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, broker_contract_handler); }
    { ns_waitable_handle_t h = (ns_waitable_handle_t)(uintptr_t)1u; (void)ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, broker_contract_handler); }
    (void)ns_watcher_deinit(&watcher);
    (void)ns_broker_add(&watcher);
    (void)ns_broker_remove(&watcher);
}

int main(void)
{
    broker_contract_check_types();
    return 0;
}
