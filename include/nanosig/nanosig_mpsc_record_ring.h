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
 *
 * 桌面平台（NS_MPSC_CACHELINE_ALIGNED）使用手动 padding 将
 * reserve_pos/write_pos（生产者写入）与 read_pos（消费者写入）
 * 分离到不同的 64 字节 cache line，避免 false sharing。
 */
typedef struct ns_mpsc_record_ring  {
#ifdef NS_MPSC_CACHELINE_ALIGNED
    /* 生产者侧：reserve_pos, write_pos 各占独立 cache line */
    atomic_size_t reserve_pos;
    char          _cl0[56];
    atomic_size_t write_pos;
    char          _cl1[56];
    /* 消费者侧 */
    atomic_size_t read_pos;
    char          _cl2[56];
    /* 只读 */
    uint8_t      *storage;
    size_t        capacity;
#else
    atomic_size_t reserve_pos;
    atomic_size_t write_pos;
    atomic_size_t read_pos;
    uint8_t      *storage;
    size_t        capacity;
#endif
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
 * @param[in]     part_count 片段数量；允许为 0（推送零大小记录）。
 *
 * @retval NS_OK           推送成功。
 * @retval NS_E_INVAL      参数非法（ring/parts 为 NULL、片段 data 为 NULL
 *                          且 size > 0、或总大小溢出）。
 * @retval NS_E_QUEUE_FULL 空间不足，记录未写入。
 */
extern int ns_mpsc_record_ring_try_pushv(
    ns_mpsc_record_ring_t *ring,
    const ns_mpsc_record_part_t *parts,
    size_t part_count);

/**
 * @brief 尝试借出一条连续记录（仅消费者线程调用）。
 *
 * 非阻塞；从环头部借出最早提交的一条真实记录。返回的记录指针直接指向
 * 环内部存储，调用方必须在读取完成后调用 `ns_mpsc_record_ring_release`。
 * 未 release 前不可再次 acquire。真实记录整体连续；内部 wrap marker 会被
 * 消费者自动跳过，不会暴露给调用方。
 *
 * 当记录的 payload 大小为 0 时，`*out_size` 为 0，`*out_record` 指向
 * payload 起始地址（header 之后），但解引用无意义。调用方应以 `*out_size`
 * 判断是否有可读数据。
 *
 * 线程安全：仅限单个消费者线程调用，不可被多个线程并发 acquire/release。
 *
 * @param[in,out] ring       已初始化的环。
 * @param[out]    out_record 写入记录数据指针。
 * @param[out]    out_size   写入记录数据字节数。
 *
 * @retval NS_OK        成功借出一条记录。
 * @retval NS_E_EMPTY   队列为空或头部记录尚未提交。
 * @retval NS_E_INVAL   参数非法或检测到元数据损坏（stride 越界）。
 * @retval NS_E_CORRUPT 内部一致性检查失败（used > capacity），记录未借出。
 */
extern int ns_mpsc_record_ring_try_acquire(
    ns_mpsc_record_ring_t *ring,
    void **out_record,
    size_t *out_size);

/**
 * @brief 释放最近 acquire 的记录。
 *
 * 释放成功后，环空间重新对生产者可用。@p record 必须是当前头部真实记录
 * 的数据指针，也就是最近一次成功 `ns_mpsc_record_ring_try_acquire` 返回的
 * 指针。
 *
 * @param[in,out] ring   已初始化的环。
 * @param[in]     record 由 try_acquire 返回的记录数据指针。
 *
 * @retval NS_OK        释放成功。
 * @retval NS_E_INVAL   参数非法、record 不是当前头部记录或元数据非法。
 * @retval NS_E_CORRUPT 内部一致性检查失败（used > capacity），记录未释放。
 */
extern int ns_mpsc_record_ring_release(
    ns_mpsc_record_ring_t *ring,
    void *record);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_MPSC_RECORD_RING_H */
