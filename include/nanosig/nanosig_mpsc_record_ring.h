/**
 * @file nanosig_mpsc_record_ring.h
 * @brief Variable-size MPSC record ring.
 * @date 2026-05-30
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_MPSC_RECORD_RING_H
#define NANOSIG_MPSC_RECORD_RING_H

#include <stddef.h>
#include <stdint.h>

#include <nanosig/nanosig_atomic.h>
#include <nanosig/nanosig_status.h>
#include <nanosig/nanosig_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 记录槽位的对齐粒度（等于 sizeof(size_t)）。
 *
 * 所有 header 和记录数据均按此粒度对齐，保证原子操作的天然对齐。
 */
#define NS_MPSC_RECORD_RING_ALIGNMENT ((size_t)sizeof(size_t))

/**
 * @brief scatter-gather 记录片段描述符。
 *
 * 用于 try_pushv，描述一条记录中的一个连续内存块。多个片段按数组
 * 顺序拼接为完整的记录。
 */
typedef struct ns_mpsc_record_part {
    const void *data;
    size_t size;
} ns_mpsc_record_part_t;

/**
 * @brief MPSC 记录环主结构。
 *
 * 调用方应将其视为不透明类型，仅通过公开接口访问。
 * 字段说明：
 *   - storage      : 调用方提供的外部存储区域（不拥有生命周期）
 *   - capacity     : 存储区域的字节容量（2 的幂）
 *   - reserve_pos  : 生产者 CAS 竞争的预留位置（原子）
 *   - write_pos    : 已提交写入的位置（原子，release 语义）
 *   - read_pos     : 消费者已读取的位置（原子，release 语义）
 */
typedef struct ns_mpsc_record_ring {
    uint8_t *storage;
    size_t capacity;
    atomic_size_t reserve_pos;
    atomic_size_t write_pos;
    atomic_size_t read_pos;
} ns_mpsc_record_ring_t;

/**
 * @brief 初始化 MPSC 记录环。
 *
 * 将外部提供的 @p storage 区域初始化为一个 MPSC 环形缓冲区。调用方拥有
 * 存储的生命周期；环本身不分配内存，可嵌入任意内存区域（静态数组、
 * mmap、堆分配等）。
 *
 * @pre @p ring 和 @p storage 不为 NULL。
 * @pre @p storage 的起始地址必须对齐到 `NS_MPSC_RECORD_RING_ALIGNMENT`。
 * @pre @p capacity 必须是 2 的幂且不小于 `sizeof(size_t) * 3`。
 *
 * @param[out] ring       待初始化的环结构。
 * @param[in]  storage    调用方提供的存储区域。
 * @param[in]  capacity   存储区域的字节容量（枚举值或兼容的 2 的幂）。
 *
 * @retval NS_OK      初始化成功。
 * @retval NS_E_INVAL 参数不满足前置条件。
 */
extern int ns_mpsc_record_ring_init(
    ns_mpsc_record_ring_t *ring,
    void *storage,
    ns_capacity_t capacity);

/**
 * @brief 返回环的总字节容量。
 *
 * @param[in] ring 已初始化的环。
 *
 * @return 总容量（字节）；ring 无效时返回 0。
 */
extern size_t ns_mpsc_record_ring_capacity(const ns_mpsc_record_ring_t *ring);

/**
 * @brief 返回环当前的空闲字节数。
 *
 * 该值基于 reserve_pos 和 read_pos 计算，反映的是"尚可被生产者抢占的
 * 空间"，在多生产者并发推送期间可能短暂偏大（乐观估计）。
 *
 * @param[in] ring 已初始化的环。
 *
 * @return 空闲字节数；ring 无效时返回 0。返回 0 也可能表示环已满。
 */
extern size_t ns_mpsc_record_ring_free_capacity(const ns_mpsc_record_ring_t *ring);

/**
 * @brief 返回单条记录可容纳的最大有效负载字节数。
 *
 * 计算公式：`(capacity / 2) - align_up(sizeof(header))`。此值保证至少
 * 一条最大记录可以写入而不与消费者的 read_pos 重叠。
 *
 * @param[in] ring 已初始化的环。
 *
 * @return 最大有效负载大小（字节）；ring 无效或容量过小时返回 0。
 */
extern size_t ns_mpsc_record_ring_max_record_size(const ns_mpsc_record_ring_t *ring);

/**
 * @brief 尝试推送一条记录（单块）。
 *
 * 非阻塞；空间不足时立即返回 `NS_E_QUEUE_FULL`，不自旋。该函数是
 * `ns_mpsc_record_ring_try_pushv` 的便捷封装，等价于传入单个 part。
 *
 * 线程安全：可被多个生产者并发调用。
 *
 * @param[in,out] ring        已初始化的环。
 * @param[in]     record      指向记录数据的指针；record_size 为 0 时可为 NULL。
 * @param[in]     record_size 记录的字节大小（允许为 0）。
 *
 * @retval NS_OK           推送成功。
 * @retval NS_E_INVAL      参数非法（ring 无效，或 record 为 NULL 且 size > 0）。
 * @retval NS_E_QUEUE_FULL 空间不足，记录未写入。
 */
extern int ns_mpsc_record_ring_try_push(
    ns_mpsc_record_ring_t *ring,
    const void *record,
    size_t record_size);

/**
 * @brief 尝试推送一条记录（scatter-gather 向量）。
 *
 * 将 @p parts 数组中的多个片段拼接为一条连续记录写入环中。各片段按
 * 数组顺序依次拼接，最终记录大小为所有片段大小之和。
 *
 * 非阻塞；空间不足时立即返回 `NS_E_QUEUE_FULL`。
 *
 * 线程安全：可被多个生产者并发调用。
 *
 * @param[in,out] ring       已初始化的环。
 * @param[in]     parts      记录片段数组。
 * @param[in]     part_count 片段数量；必须大于 0。
 *
 * @retval NS_OK           推送成功。
 * @retval NS_E_INVAL      参数非法（ring/parts 为 NULL、part_count 为 0、
 *                          片段 data 为 NULL 且 size > 0、或总大小溢出）。
 * @retval NS_E_QUEUE_FULL 空间不足，记录未写入。
 */
extern int ns_mpsc_record_ring_try_pushv(
    ns_mpsc_record_ring_t *ring,
    const ns_mpsc_record_part_t *parts,
    size_t part_count);

/**
 * @brief 尝试弹出一条记录（仅消费者线程调用）。
 *
 * 非阻塞；从环头部读取最早提交的一条记录。如果头部记录尚未被生产者
 * 完全提交（valid 标记未置位），视为队列空并返回 `NS_E_EMPTY`。
 *
 * 线程安全：仅限单个消费者线程调用，不可被多个线程并发 pop。
 *
 * @param[in,out] ring            已初始化的环。
 * @param[out]    out_record      接收记录数据的缓冲区。
 * @param[in]     out_capacity    缓冲区字节大小。
 * @param[out]    out_record_size 写入实际记录大小（可为 NULL）。当缓冲区
 *                                不足（返回 NS_E_NOMEM）时，写入所需大小
 *                                作为 hint，供调用方扩容后重试。
 *
 * @retval NS_OK       成功弹出一条记录。
 * @retval NS_E_EMPTY  队列为空或头部记录尚未提交。
 * @retval NS_E_INVAL  参数非法或检测到元数据损坏（stride 越界）。
 * @retval NS_E_NOMEM  缓冲区不足；*out_record_size 写入所需大小。
 * @retval NS_E_CORRUPT 内部一致性检查失败（used > capacity），记录未弹出。
 */
extern int ns_mpsc_record_ring_try_pop(
    ns_mpsc_record_ring_t *ring,
    void *out_record,
    size_t out_capacity,
    size_t *out_record_size);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_MPSC_RECORD_RING_H */
