/**
 * @file ns_broker.c
 * @brief nanosig event broker and watcher runtime.
 * @date 2026-06-14
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include "nanosig/internal/ns_broker.h"
#include "nanosig/internal/ns_timer_mgr.h"

#include <nanosig/nanosig.h>

#include <platform/port.h>

#ifdef NANOSIG_TEST
/* Test hook: non-NS_OK value injects waitset_wait failure.
 * Set before ns_init() to verify broker thread survives waitset errors. */
volatile int g_ns_test_waitset_wait_result = NS_OK;
#endif

#define NS_BROKER_COMPLETION_CAPACITY 16u

struct ns_event_broker {
    ns_platform_thread_t *thread;
    ns_platform_waitset_t *waitset;
    ns_platform_wakeup_t *wakeup;
    ns_platform_waitable_t wakeup_waitable;
    ns_list_node_t watcher_head;
    ns_platform_mutex_t *watcher_mutex;
    atomic_int quit_requested;
};

static ns_event_broker_t *g_broker = NULL;

static int ns_broker_runtime_ready(void)
{
    int initialized = 0;
    int rc = ns_is_initialized(&initialized);

    if(rc != NS_OK) return rc;
    return initialized ? NS_OK : NS_E_SHUTDOWN;
}

static int ns_watcher_events_are_valid(uint32_t events)
{
    const uint32_t valid_events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR;

    return (events & ~valid_events) == 0u;
}

static int ns_broker_node_is_initialized(const ns_list_node_t *node)
{
    return (node != NULL) && (node->next != NULL) && (node->prev != NULL);
}

static int ns_broker_node_is_linked(const ns_list_node_t *node)
{
    if(!ns_broker_node_is_initialized(node)) return 0;
    /* ns_list_init 设置 next = prev = node（自环）；非自环说明已挂入链表 */
    return !ns_list_empty((ns_list_node_t *)node);
}

static void ns_watcher_reset_empty(ns_watcher_t *watcher)
{
    watcher->signal.mutex = NULL;
    watcher->signal.payload_size = 0u;
    watcher->signal.slot_capacity = 0u;
    watcher->signal.debug_name = NULL;
    ns_list_init(&watcher->signal.slot_list);
    ns_waitable_init(&watcher->waitable);
    ns_list_init(&watcher->broker_node);
}

static int ns_watcher_is_initialized(const ns_watcher_t *watcher)
{
    if(watcher == NULL) return 0;
    if(watcher->signal.mutex == NULL) return 0;
    if(watcher->signal.payload_size != sizeof(ns_watcher_event_t)) return 0;
    if(!ns_broker_node_is_initialized(&watcher->broker_node)) return 0;
    return ns_watcher_events_are_valid(watcher->waitable.events);
}

static int ns_watcher_init_common(ns_watcher_t *watcher, uint32_t events, int edge_triggered)
{
    int rc;

    if(watcher == NULL) return NS_E_INVAL;
    if(!ns_watcher_events_are_valid(events)) return NS_E_INVAL;

    rc = ns_broker_runtime_ready();
    if(rc != NS_OK) return rc;

    rc = ns_signal_init_raw(&watcher->signal, sizeof(ns_watcher_event_t), 0u, "ns-watcher");
    if(rc != NS_OK) return rc;

    watcher->waitable.events = events;
    watcher->waitable.edge_triggered = edge_triggered ? 1 : 0;
    ns_list_init(&watcher->broker_node);
    return NS_OK;
}

int ns_watcher_init_fd(ns_watcher_t *watcher, int fd, uint32_t events, int edge_triggered)
{
    int rc;

    if(watcher == NULL) return NS_E_INVAL;
    ns_watcher_reset_empty(watcher);
    if(fd < 0) return NS_E_INVAL;

    rc = ns_watcher_init_common(watcher, events, edge_triggered);
    if(rc != NS_OK) return rc;

    watcher->waitable.fd = fd;
    return NS_OK;
}

int ns_watcher_init_handle(ns_watcher_t *watcher, void *handle, uint32_t events, int edge_triggered)
{
    int rc;

    if(watcher == NULL) return NS_E_INVAL;
    ns_watcher_reset_empty(watcher);
    if(handle == NULL) return NS_E_INVAL;

    rc = ns_watcher_init_common(watcher, events, edge_triggered);
    if(rc != NS_OK) return rc;

    watcher->waitable.handle = handle;
    return NS_OK;
}

int ns_watcher_deinit(ns_watcher_t *watcher)
{
    int rc;

    if(watcher == NULL) return NS_E_INVAL;
    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;
    if(ns_broker_node_is_linked(&watcher->broker_node)) return NS_E_EXISTS;

    rc = ns_signal_deinit_raw(&watcher->signal);
    ns_waitable_init(&watcher->waitable);
    ns_list_init(&watcher->broker_node);
    return rc;
}

ns_event_broker_t *ns_broker(void)
{
    return g_broker;
}

static void ns_broker_notify(void *ctx)
{
    ns_event_broker_t *broker = (ns_event_broker_t *)ctx;

    if((broker != NULL) && (broker->wakeup != NULL)){
        (void)ns_platform_wakeup_signal(broker->wakeup);
    }
}

static void ns_broker_emit_completion(const ns_platform_waitset_completion_t *completion)
{
    ns_watcher_t *watcher;
    ns_watcher_event_t event;

    if((completion == NULL) || (completion->waitable == NULL)) return;

    watcher = (ns_watcher_t *)completion->waitable->user_data;
    if(watcher == NULL) return;

    event.triggered_events = completion->triggered_events;
    (void)ns_signal_emit_raw(&watcher->signal, &event, sizeof(event));
}

static void ns_broker_run(void *arg)
{
    ns_event_broker_t *broker = (ns_event_broker_t *)arg;
    ns_platform_waitset_completion_t completions[NS_BROKER_COMPLETION_CAPACITY];

    while(ns_atomic_load_explicit(&broker->quit_requested, ns_memory_order_acquire) == 0){
        ns_platform_time_us_t timeout_us = NS_PLATFORM_WAIT_INFINITE_US;
        size_t count = 0u;
        size_t i;
        int rc;

        rc = ns_timer_mgr_next_timeout(&timeout_us);
        if(rc == NS_E_NO_TIMER){
            timeout_us = NS_PLATFORM_WAIT_INFINITE_US;
        } else if(rc != NS_OK){
            timeout_us = NS_PLATFORM_WAIT_INFINITE_US;
        }

        rc = ns_platform_waitset_wait(
            broker->waitset,
            timeout_us,
            completions,
            NS_BROKER_COMPLETION_CAPACITY,
            &count);
#ifdef NANOSIG_TEST
        if(g_ns_test_waitset_wait_result != NS_OK){
            rc = g_ns_test_waitset_wait_result;
            /* Inject one failure, then reset so subsequent iterations work normally */
            g_ns_test_waitset_wait_result = NS_OK;
        }
#endif
        if(rc == NS_OK){
            for(i = 0u; i < count; ++i){
                if(completions[i].waitable == &broker->wakeup_waitable){
                    ns_platform_wait_result_t wait_result;

                    (void)ns_platform_wakeup_wait(broker->wakeup, 0u, &wait_result);
                } else {
                    ns_broker_emit_completion(&completions[i]);
                }
            }
        }

        (void)ns_timer_mgr_fire_expired();
    }
}

int ns_broker_add(ns_event_broker_t *broker, ns_watcher_t *watcher)
{
    int rc;
    int unlock_rc;

    if((broker == NULL) || (watcher == NULL)) return NS_E_INVAL;
    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;

    rc = ns_platform_mutex_lock(broker->watcher_mutex);
    if(rc != NS_OK) return rc;

    if(ns_broker_node_is_linked(&watcher->broker_node)){
        unlock_rc = ns_platform_mutex_unlock(broker->watcher_mutex);
        return (unlock_rc == NS_OK) ? NS_E_EXISTS : unlock_rc;
    }

    watcher->waitable.user_data = watcher;
    rc = ns_platform_waitset_add(broker->waitset, &watcher->waitable);
    if(rc == NS_OK){
        ns_list_push_back(&broker->watcher_head, &watcher->broker_node);
    } else {
        watcher->waitable.user_data = NULL;
    }

    unlock_rc = ns_platform_mutex_unlock(broker->watcher_mutex);
    if((rc == NS_OK) && (unlock_rc != NS_OK)) rc = unlock_rc;
    if(rc == NS_OK) ns_broker_notify(broker);
    return rc;
}

int ns_broker_remove(ns_event_broker_t *broker, ns_watcher_t *watcher)
{
    int rc;
    int unlock_rc;

    if((broker == NULL) || (watcher == NULL)) return NS_E_INVAL;
    if(!ns_watcher_is_initialized(watcher)) return NS_E_INVAL;

    rc = ns_platform_mutex_lock(broker->watcher_mutex);
    if(rc != NS_OK) return rc;

    if(!ns_broker_node_is_linked(&watcher->broker_node)){
        unlock_rc = ns_platform_mutex_unlock(broker->watcher_mutex);
        return (unlock_rc == NS_OK) ? NS_E_INVAL : unlock_rc;
    }

    rc = ns_platform_waitset_remove(broker->waitset, &watcher->waitable);
    if(rc == NS_OK){
        watcher->waitable.user_data = NULL;
        ns_list_remove_init(&watcher->broker_node);
    }

    unlock_rc = ns_platform_mutex_unlock(broker->watcher_mutex);
    if((rc == NS_OK) && (unlock_rc != NS_OK)) rc = unlock_rc;
    if(rc == NS_OK) ns_broker_notify(broker);
    return rc;
}

static void ns_broker_remove_all_watchers(ns_event_broker_t *broker)
{
    if(broker == NULL) return;

    if(ns_platform_mutex_lock(broker->watcher_mutex) == NS_OK){
        while(!ns_list_empty(&broker->watcher_head)){
            ns_list_node_t *node = ns_list_front(&broker->watcher_head);
            ns_watcher_t *watcher = ns_list_entry(node, ns_watcher_t, broker_node);

            (void)ns_platform_waitset_remove(broker->waitset, &watcher->waitable);
            watcher->waitable.user_data = NULL;
            ns_list_remove_init(&watcher->broker_node);
        }
        (void)ns_platform_mutex_unlock(broker->watcher_mutex);
    }
}

int ns_broker_global_init(void)
{
    ns_event_broker_t *broker;
    int rc;

    if(g_broker != NULL) return NS_E_EXISTS;

    broker = (ns_event_broker_t *)ns_platform_alloc(sizeof(*broker));
    if(broker == NULL) return NS_E_NOMEM;

    broker->thread = NULL;
    broker->waitset = NULL;
    broker->wakeup = NULL;
    ns_waitable_init(&broker->wakeup_waitable);
    broker->watcher_mutex = NULL;
    ns_list_init(&broker->watcher_head);
    ns_atomic_init(&broker->quit_requested, 0);

    rc = ns_platform_wakeup_create(&broker->wakeup, "nanosig-broker");
    if(rc != NS_OK) goto out_free;

    rc = ns_platform_waitset_create(&broker->waitset);
    if(rc != NS_OK) goto out_wakeup;

    broker->wakeup_waitable = ns_platform_wakeup_get_waitable(broker->wakeup);
    broker->wakeup_waitable.events = NS_WAITABLE_EVENT_IN;
    broker->wakeup_waitable.user_data = NULL;
    rc = ns_platform_waitset_add(broker->waitset, &broker->wakeup_waitable);
    if(rc != NS_OK) goto out_waitset;

    rc = ns_platform_mutex_create(&broker->watcher_mutex, "nanosig-broker-watchers");
    if(rc != NS_OK) goto out_wakeup_waitable;

    rc = ns_timer_mgr_global_init(ns_broker_notify, broker);
    if(rc != NS_OK) goto out_mutex;

    rc = ns_platform_thread_create(&broker->thread, ns_broker_run, broker, "nanosig-broker");
    if(rc != NS_OK) goto out_timer_mgr;

    g_broker = broker;
    return NS_OK;

out_timer_mgr:
    ns_timer_mgr_global_shutdown();
out_mutex:
    (void)ns_platform_mutex_destroy(broker->watcher_mutex);
    broker->watcher_mutex = NULL;
out_wakeup_waitable:
    (void)ns_platform_waitset_remove(broker->waitset, &broker->wakeup_waitable);
out_waitset:
    (void)ns_platform_waitset_destroy(broker->waitset);
    broker->waitset = NULL;
out_wakeup:
    (void)ns_platform_wakeup_destroy(broker->wakeup);
    broker->wakeup = NULL;
out_free:
    ns_platform_free(broker);
    return rc;
}

void ns_broker_global_shutdown(void)
{
    ns_event_broker_t *broker = g_broker;

    if(broker == NULL) return;

    ns_atomic_store_explicit(&broker->quit_requested, 1, ns_memory_order_release);
    (void)ns_platform_wakeup_signal(broker->wakeup);
    if(broker->thread != NULL){
        (void)ns_platform_thread_join(broker->thread);
        broker->thread = NULL;
    }

    g_broker = NULL;
    ns_timer_mgr_global_shutdown();

    ns_broker_remove_all_watchers(broker);

    (void)ns_platform_waitset_remove(broker->waitset, &broker->wakeup_waitable);
    (void)ns_platform_waitset_destroy(broker->waitset);
    (void)ns_platform_wakeup_destroy(broker->wakeup);
    (void)ns_platform_mutex_destroy(broker->watcher_mutex);
    ns_platform_free(broker);
}
