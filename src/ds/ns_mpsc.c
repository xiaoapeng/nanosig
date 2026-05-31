/**
 * @file ns_mpsc.c
 * @brief 固定容量 MPSC 队列实现。
 * @date 2026-05-24
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_mpsc.h>

#include <string.h>

static inline int ns_mpsc_is_valid(const ns_mpsc_queue_t *queue)
{
    return (queue != NULL) &&
           (queue->slots != NULL) &&
           (queue->storage != NULL) &&
           (queue->item_size != 0u);
}

static inline int ns_mpsc_capacity_is_valid(size_t capacity)
{
    if(capacity == 0u) return 0;
    if((capacity & (capacity - 1u)) != 0u) return 0;
    if(capacity > (SIZE_MAX / 2u)) return 0;

    return 1;
}

static inline size_t ns_mpsc_slot_index(const ns_mpsc_queue_t *queue, size_t pos)
{
    return pos & queue->mark;
}

static inline uint8_t *ns_mpsc_item_storage(ns_mpsc_queue_t *queue, size_t index)
{
    return &queue->storage[index * queue->item_size];
}

static inline int ns_mpsc_sequence_before(size_t sequence, size_t pos)
{
    size_t diff = pos - sequence;

    return (diff != 0u) && (diff <= (SIZE_MAX / 2u));
}

size_t ns_mpsc_free_capacity(const ns_mpsc_t *queue)
{
    size_t capacity;
    size_t enqueue_pos;
    size_t dequeue_pos;
    size_t used;

    capacity = ns_mpsc_capacity(queue);
    if(capacity == 0u) return 0u;

    enqueue_pos = ns_atomic_load_explicit(&queue->enqueue_pos, ns_memory_order_relaxed);
    dequeue_pos = ns_atomic_load_explicit(&queue->dequeue_pos, ns_memory_order_relaxed);

    used = enqueue_pos - dequeue_pos;
    if(used >= capacity) return used == capacity ? 0u : capacity;

    return capacity - used;
}

int ns_mpsc_init(
    ns_mpsc_queue_t *queue,
    ns_mpsc_slot_t *slots,
    void *storage,
    ns_capacity_t capacity,
    size_t item_size)
{
    size_t capacity_value;
    size_t i;

    if((queue == NULL) || (slots == NULL) || (storage == NULL)) return NS_E_INVAL;
    capacity_value = (size_t)capacity;
    if(!ns_mpsc_capacity_is_valid(capacity_value) || (item_size == 0u)) return NS_E_INVAL;

    queue->slots = slots;
    queue->storage = (uint8_t *)storage;
    queue->mark = capacity_value - 1u;
    queue->item_size = item_size;
    ns_atomic_init(&queue->enqueue_pos, 0u);
    ns_atomic_init(&queue->dequeue_pos, 0u);

    for(i = 0u; i < capacity_value; ++i){
        ns_atomic_init(&slots[i].sequence, i);
    }

    return NS_OK;
}

int ns_mpsc_try_push(ns_mpsc_queue_t *queue, const void *item)
{
    size_t pos;
    size_t index;
    ns_mpsc_slot_t *slot;

    if(!ns_mpsc_is_valid(queue) || (item == NULL)) return NS_E_INVAL;

    pos = ns_atomic_load_explicit(&queue->enqueue_pos, ns_memory_order_relaxed);

    for(;;){
        size_t seq;

        index = ns_mpsc_slot_index(queue, pos);
        slot = &queue->slots[index];
        seq = ns_atomic_load_explicit(&slot->sequence, ns_memory_order_acquire);

        if(seq == pos){
            size_t expected = pos;

            if(ns_atomic_compare_exchange_weak_explicit(
                   &queue->enqueue_pos,
                   &expected,
                   pos + 1u,
                   ns_memory_order_relaxed,
                   ns_memory_order_relaxed)){
                break;
            }

            pos = expected;
            continue;
        }

        if(ns_mpsc_sequence_before(seq, pos)) return NS_E_QUEUE_FULL;

        pos = ns_atomic_load_explicit(&queue->enqueue_pos, ns_memory_order_relaxed);
    }

    memcpy(ns_mpsc_item_storage(queue, index), item, queue->item_size);
    ns_atomic_store_explicit(&slot->sequence, pos + 1u, ns_memory_order_release);
    return NS_OK;
}

int ns_mpsc_try_pop(ns_mpsc_queue_t *queue, void *out_item)
{
    size_t pos;
    size_t index;
    ns_mpsc_slot_t *slot;
    size_t seq;

    if(!ns_mpsc_is_valid(queue) || (out_item == NULL)) return NS_E_INVAL;

    pos = ns_atomic_load_explicit(&queue->dequeue_pos, ns_memory_order_relaxed);
    index = ns_mpsc_slot_index(queue, pos);
    slot = &queue->slots[index];
    seq = ns_atomic_load_explicit(&slot->sequence, ns_memory_order_acquire);

    if(seq != (pos + 1u)) return NS_E_EMPTY;

    memcpy(out_item, ns_mpsc_item_storage(queue, index), queue->item_size);
    ns_atomic_store_explicit(&queue->dequeue_pos, pos + 1u, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&slot->sequence, pos + queue->mark + 1u, ns_memory_order_release);
    return NS_OK;
}
