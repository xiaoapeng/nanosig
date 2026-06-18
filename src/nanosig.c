/**
 * @file nanosig.c
 * @brief nanosig core lifecycle and loop runtime.
 * @date 2026-05-24
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig.h>
#include <nanosig/nanosig_mpsc_record_ring.h>

#include <platform/port.h>

#include "src/ns_broker.h"

struct ns_loop {
    ns_platform_wakeup_t *wakeup;
    ns_loop_config_t config;
    ns_mpsc_record_ring_t queue;
    atomic_int quit_requested;
    atomic_int running;
};

/**
 * @brief 入队的 slot 调用记录头。
 *
 * 紧跟此结构体之后是 payload 字节（payload_size 长度）。
 * 通过 MPSC record ring 的 scatter-gather 写入：第一个 part 是此头，
 * 第二个 part 是 payload 数据（可能为 0 字节）。
 */
typedef struct ns_slot_call {
    ns_slot_fn fn;
    void *user_data;
    size_t payload_size;
} ns_slot_call_t;

/* ns_connection is now defined in nanosig_signal.h */

static int ns_signal_lock(ns_signal_t *signal)
{
    if((signal == NULL) || (signal->mutex == NULL)) return NS_E_INVAL;
    return ns_platform_mutex_lock(signal->mutex);
}

static int ns_signal_unlock(ns_signal_t *signal)
{
    if((signal == NULL) || (signal->mutex == NULL)) return NS_E_INVAL;
    return ns_platform_mutex_unlock(signal->mutex);
}

static atomic_int g_ns_initialized = 0;

static int ns_is_power_of_two(size_t value)
{
    return (value != 0u) && ((value & (value - 1u)) == 0u);
}

static int ns_loop_config_validate(const ns_loop_config_t *config)
{
    if(config == NULL) return NS_E_INVAL;
    if(config->flags != 0u) return NS_E_INVAL;
    if(!ns_is_power_of_two((size_t)config->queue_byte_capacity)) return NS_E_INVAL;

    return NS_OK;
}

static int ns_runtime_is_initialized(void)
{
    return ns_atomic_load_explicit(&g_ns_initialized, ns_memory_order_acquire) != 0;
}

int ns_init(void)
{
    int rc;

    if(ns_runtime_is_initialized()) return NS_E_EXISTS;

    rc = ns_platform_init();
    if(rc != NS_OK) goto out;

    rc = ns_broker_global_init();
    if(rc != NS_OK) goto out_platform;

    ns_atomic_store_explicit(&g_ns_initialized, 1, ns_memory_order_release);
    return NS_OK;

out_platform:
    (void)ns_platform_shutdown();
out:
    return rc;
}

int ns_shutdown(void)
{
    if(!ns_runtime_is_initialized()) return NS_OK;

    ns_atomic_store_explicit(&g_ns_initialized, 0, ns_memory_order_release);

    ns_broker_global_shutdown();

    return ns_platform_shutdown();
}

int ns_is_initialized(int *out_initialized)
{
    if(out_initialized == NULL) return NS_E_INVAL;

    *out_initialized = ns_runtime_is_initialized();
    return NS_OK;
}

int ns_loop_create(ns_loop_t **out_loop, const ns_loop_config_t *config)
{
    ns_loop_t *loop;
    ns_loop_config_t local_config;
    size_t struct_size;
    size_t total_size;
    int rc;

    if(out_loop != NULL) *out_loop = NULL;

    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;

    local_config = (config != NULL) ? *config : NS_LOOP_CONFIG_DEFAULT();
    rc = ns_loop_config_validate(&local_config);
    if(rc != NS_OK) return rc;

    struct_size = ns_align_up(sizeof(*loop), NS_MPSC_RECORD_RING_ALIGNMENT);
    total_size = struct_size + (size_t)local_config.queue_byte_capacity;
    loop = (ns_loop_t *)ns_platform_alloc(total_size);
    if(loop == NULL) return NS_E_NOMEM;

    loop->wakeup = NULL;
    loop->config = local_config;
    ns_atomic_init(&loop->quit_requested, 0);
    ns_atomic_init(&loop->running, 0);

    rc = ns_mpsc_record_ring_init(
        &loop->queue,
        ((uint8_t *)loop) + struct_size,
        local_config.queue_byte_capacity);
    if(rc != NS_OK) goto out_free;

    rc = ns_platform_wakeup_create(&loop->wakeup, local_config.debug_name);
    if(rc != NS_OK) goto out_free;

    if(out_loop != NULL) *out_loop = loop;
    return NS_OK;

out_free:
    ns_platform_free(loop);
    return rc;
}

int ns_loop_destroy(ns_loop_t *loop)
{
    int rc;

    if(loop == NULL) return NS_E_INVAL;
    if(ns_atomic_load_explicit(&loop->running, ns_memory_order_acquire) != 0) return NS_E_INVAL;

    rc = ns_platform_wakeup_destroy(loop->wakeup);
    if(rc != NS_OK) return rc;

    ns_platform_free(loop);
    return NS_OK;
}

static void ns_loop_dispatch_pending(ns_loop_t *loop)
{
    void *record = NULL;
    size_t record_size = 0u;
    int rc;

    for(;;){
        rc = ns_mpsc_record_ring_try_acquire(&loop->queue, &record, &record_size);
        if(rc != NS_OK) break;

        if(record_size >= sizeof(ns_slot_call_t)){
            ns_slot_call_t *call = (ns_slot_call_t *)record;
            const void *payload = (call->payload_size > 0u)
                ? ((const uint8_t *)record + sizeof(ns_slot_call_t))
                : NULL;
            call->fn(call->user_data, payload);
        }

        (void)ns_mpsc_record_ring_release(&loop->queue, record);
    }
}

int ns_loop_run(ns_loop_t *loop)
{
    ns_platform_wait_result_t wait_result = NS_PLATFORM_WAIT_TIMEOUT;
    int expected_running = 0;
    int rc;

    if(loop == NULL) return NS_E_INVAL;
    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;

    if(!ns_atomic_compare_exchange_strong(&loop->running, &expected_running, 1)) return NS_E_EXISTS;

    for(;;){
        ns_loop_dispatch_pending(loop);

        if(ns_atomic_load_explicit(&loop->quit_requested, ns_memory_order_acquire) != 0) break;

        rc = ns_platform_wakeup_wait(loop->wakeup, NS_PLATFORM_WAIT_INFINITE_US, &wait_result);
        if(rc != NS_OK) goto out;

        (void)wait_result;
    }

    ns_atomic_store_explicit(&loop->quit_requested, 0, ns_memory_order_release);
    rc = NS_OK;

out:
    ns_atomic_store_explicit(&loop->running, 0, ns_memory_order_release);
    return rc;
}

int ns_loop_quit(ns_loop_t *loop)
{
    if(loop == NULL) return NS_E_INVAL;
    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;

    ns_atomic_store_explicit(&loop->quit_requested, 1, ns_memory_order_release);
    return ns_platform_wakeup_signal(loop->wakeup);
}

/* ------------------------------------------------------------------ */
/*  signal / slot runtime                                              */
/* ------------------------------------------------------------------ */

int ns_signal_init_raw(ns_signal_t *signal, size_t payload_size, size_t slot_capacity, const char *debug_name)
{
    int rc;

    if(signal == NULL) return NS_E_INVAL;

    signal->payload_size = payload_size;
    signal->slot_capacity = slot_capacity;
    signal->debug_name = debug_name;
    ns_list_init(&signal->slot_list);
    signal->mutex = NULL;

    rc = ns_platform_mutex_create(&signal->mutex, debug_name ? debug_name : "ns-signal");
    if(rc != NS_OK) return rc;

    return NS_OK;
}

int ns_signal_deinit_raw(ns_signal_t *signal)
{
    int rc;

    if(signal == NULL) return NS_E_INVAL;
    if(signal->mutex == NULL) return NS_OK;

    rc = ns_platform_mutex_destroy(signal->mutex);
    signal->mutex = NULL;
    return rc;
}

int ns_signal_connect(
    ns_signal_t *signal,
    ns_slot_fn slot_fn,
    ns_loop_t *target_loop,
    void *user_data,
    ns_connection_t *connection)
{
    int rc;

    if((signal == NULL) || (slot_fn == NULL) || (target_loop == NULL) || (connection == NULL)) return NS_E_INVAL;

    connection->signal = signal;
    connection->slot_fn = slot_fn;
    connection->user_data = user_data;
    connection->target_loop = target_loop;
    ns_list_init(&connection->signal_node);

    rc = ns_signal_lock(signal);
    if(rc != NS_OK) return rc;

    ns_list_push_back(&signal->slot_list, &connection->signal_node);

    rc = ns_signal_unlock(signal);
    return rc;
}

int ns_signal_disconnect(ns_connection_t *connection)
{
    int rc;

    if(connection == NULL) return NS_E_INVAL;

    rc = ns_signal_lock(connection->signal);
    if(rc != NS_OK) return rc;

    ns_list_remove_init(&connection->signal_node);

    rc = ns_signal_unlock(connection->signal);
    return rc;
}

int ns_signal_disconnect_all(ns_signal_t *signal)
{
    ns_list_node_t *node;
    int rc;

    if(signal == NULL) return NS_E_INVAL;

    rc = ns_signal_lock(signal);
    if(rc != NS_OK) return rc;

    while(!ns_list_empty(&signal->slot_list)){
        node = ns_list_pop_front(&signal->slot_list);
        ns_list_init(node);
    }

    rc = ns_signal_unlock(signal);
    return rc;
}

int ns_signal_emit_raw(ns_signal_t *signal, const void *payload, size_t payload_size)
{
    ns_list_node_t *node;
    ns_slot_call_t call_header;
    ns_mpsc_record_part_t parts[2];
    int rc;

    if(signal == NULL) return NS_E_INVAL;
    if(signal->payload_size != payload_size) return NS_E_INVAL;
    if((payload_size > 0u) && (payload == NULL)) return NS_E_INVAL;

    rc = ns_signal_lock(signal);
    if(rc != NS_OK) return rc;

    parts[1].data = payload;
    parts[1].size = payload_size;

    ns_list_for_each(node, &signal->slot_list){
        ns_connection_t *conn = NS_CONTAINER_OF(node, ns_connection_t, signal_node);

        call_header.fn = conn->slot_fn;
        call_header.user_data = conn->user_data;
        call_header.payload_size = payload_size;

        parts[0].data = &call_header;
        parts[0].size = sizeof(call_header);

        rc = ns_mpsc_record_ring_try_pushv(&conn->target_loop->queue, parts, 2);
        if(rc != NS_OK){
            (void)ns_signal_unlock(signal);
            return rc;
        }

        (void)ns_platform_wakeup_signal(conn->target_loop->wakeup);
    }

    rc = ns_signal_unlock(signal);
    return rc;
}
