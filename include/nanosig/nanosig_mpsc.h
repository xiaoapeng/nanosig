/**
 * @file nanosig_mpsc.h
 * @brief nanosig 固定容量多生产者单消费者队列。
 * @date 2026-05-24
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_MPSC_H
#define NANOSIG_MPSC_H

#include <stddef.h>
#include <stdint.h>

#include <nanosig/nanosig_atomic.h>
#include <nanosig/nanosig_status.h>
#include <nanosig/nanosig_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ns_mpsc_slot {
    atomic_size_t sequence;
} ns_mpsc_slot_t;

/**
 * @brief 固定容量多生产者单消费者队列。
 *
 * `ns_mpsc_init` 传入的槽位数组和数据存储区都由调用方持有，队列自身不分配内存。
 */
typedef struct ns_mpsc_queue {
    ns_mpsc_slot_t *slots;
    uint8_t *storage;
    size_t mark;
    size_t item_size;
    atomic_size_t enqueue_pos;
    atomic_size_t dequeue_pos;
} ns_mpsc_queue_t;

typedef ns_mpsc_queue_t ns_mpsc_t;

/**
 * @brief 返回队列总容量，单位为元素个数。
 *
 * 当 `queue` 为 `NULL` 或尚未初始化时返回 `0`。
 */
static inline size_t ns_mpsc_capacity(const ns_mpsc_t *queue)
{
    if((queue == NULL) || (queue->slots == NULL) || (queue->storage == NULL) || (queue->item_size == 0u)) return 0u;

    return queue->mark + 1u;
}

/**
 * @brief 返回当前剩余容量快照，单位为元素个数。
 *
 * 返回值仅用于观测，不是同步原语。在生产者或消费者并发推进时，该值可能已经过时，
 * 但始终会被约束在 `[0, ns_mpsc_capacity(queue)]` 范围内。
 */
extern size_t ns_mpsc_free_capacity(const ns_mpsc_t *queue);

/**
 * @brief 基于调用方提供的存储区初始化 MPSC 队列。
 *
 * `capacity` 必须是 `ns_capacity_t` 中的二次幂容量值。`slots` 必须恰好包含
 * `capacity` 个槽位，`storage` 必须至少指向 `item_size * capacity` 字节的
 * 连续存储区。
 *
 * @param queue 待初始化的队列对象。
 * @param slots 调用方提供并持有的槽位元数据数组。
 * @param storage 调用方提供并持有的定长元素存储区。
 * @param capacity 队列容量，单位为元素个数。
 * @param item_size 每个元素的固定字节大小。
 * @return 成功返回 `NS_OK`，失败返回负数状态码。
 */
extern int ns_mpsc_init(
    ns_mpsc_t *queue,
    ns_mpsc_slot_t *slots,
    void *storage,
    ns_capacity_t capacity,
    size_t item_size);

/**
 * @brief 尝试向队列压入一个元素。
 *
 * 该函数允许多个生产者线程并发调用。调用要么完整复制一个元素，要么返回错误且
 * 不修改队列中的元素存储区。
 *
 * @param queue 已初始化的队列。
 * @param item 指向待复制元素字节内容的指针。
 * @return 成功返回 `NS_OK`；当前没有可用槽位时返回 `NS_E_QUEUE_FULL`；
 *         参数无效时返回 `NS_E_INVAL`。
 */
extern int ns_mpsc_try_push(ns_mpsc_t *queue, const void *item);

/**
 * @brief 尝试从队列弹出一个元素。
 *
 * 对同一个队列，严格只允许一个消费者线程调用该函数。
 *
 * @param queue    已初始化的队列。
 * @param out_item 当存在可读元素时，用于接收一个元素的输出缓冲区。
 *
 * @retval NS_OK      成功弹出一个元素。
 * @retval NS_E_EMPTY 队列为空。
 * @retval NS_E_INVAL 参数无效。
 */
extern int ns_mpsc_try_pop(ns_mpsc_t *queue, void *out_item);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_MPSC_H */
