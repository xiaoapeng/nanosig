/**
 * @file ns_mpsc_record_ring.c
 * @brief Variable-size MPSC record ring implementation.
 * @date 2026-05-30
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_mpsc_record_ring.h>

#include <stddef.h>
#include <string.h>
#include <stdbool.h>

typedef struct ns_mpsc_record_header {
    atomic_size_t meta;
} ns_mpsc_record_header_t;

_Static_assert(sizeof(ns_mpsc_record_header_t) == sizeof(size_t),
               "ns_mpsc_record_header_t must be exactly one size_t");

/*
 * meta 位布局（64 位）:
 *   [31: 0] total_size    (32 bit)
 *   [39:32] padding_size  ( 8 bit)
 *   [40   ] valid         ( 1 bit)
 *
 * meta 位布局（32 位）:
 *   [15: 0] total_size    (16 bit)
 *   [23:16] padding_size  ( 8 bit)
 *   [24   ] valid         ( 1 bit)
 */
#if __SIZEOF_SIZE_T__ == 8u
#define NS_MPSC_META_STRIDE_BITS   32u
#define NS_MPSC_META_STRIDE_MASK   0xFFFFFFFFULL
#define NS_MPSC_META_PAD_SHIFT     32u
#define NS_MPSC_META_PAD_MASK      0xFFULL
#define NS_MPSC_META_VALID_SHIFT   40u
#else
#define NS_MPSC_META_STRIDE_BITS   16u
#define NS_MPSC_META_STRIDE_MASK   0xFFFFUL
#define NS_MPSC_META_PAD_SHIFT     16u
#define NS_MPSC_META_PAD_MASK      0xFFUL
#define NS_MPSC_META_VALID_SHIFT   24u
#endif

static inline size_t ns_mpsc_meta_encode_valid(size_t total_size, size_t padding_size)
{
    return (total_size & NS_MPSC_META_STRIDE_MASK)
         | ((padding_size & NS_MPSC_META_PAD_MASK) << NS_MPSC_META_PAD_SHIFT)
         | ((size_t)1u << NS_MPSC_META_VALID_SHIFT);
}

static inline int ns_mpsc_meta_is_valid(size_t meta)
{
    return (meta >> NS_MPSC_META_VALID_SHIFT) & 1u;
}

static inline size_t ns_mpsc_meta_stride(size_t meta)
{
    return meta & NS_MPSC_META_STRIDE_MASK;
}

static inline size_t ns_mpsc_meta_padding(size_t meta)
{
    return (meta >> NS_MPSC_META_PAD_SHIFT) & NS_MPSC_META_PAD_MASK;
}

typedef struct ns_mpsc_record_push_plan {
    size_t record_size;
    size_t total_size;
    size_t padding_size;
    ns_mpsc_record_header_t *next_header;
} ns_mpsc_record_push_plan_t;

static size_t ns_mpsc_record_ring_min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static size_t ns_mpsc_record_ring_align_up(size_t value)
{
    return ns_align_up(value, NS_MPSC_RECORD_RING_ALIGNMENT);
}

static int ns_mpsc_record_ring_capacity_is_valid(size_t capacity)
{
    if(capacity == 0u) return 0;
    if((capacity & (capacity - 1u)) != 0u) return 0;
    if(capacity > (SIZE_MAX / 2u)) return 0;
    if(capacity < (sizeof(ns_mpsc_record_header_t) * 3u)) return 0;

    return 1;
}

static int ns_mpsc_record_ring_is_valid(const ns_mpsc_record_ring_t *ring)
{
    return (ring != NULL) &&
           (ring->storage != NULL) &&
           ns_mpsc_record_ring_capacity_is_valid(ring->capacity);
}

static int ns_mpsc_record_ring_storage_is_aligned(const void *storage)
{
    return (((uintptr_t)storage) & (NS_MPSC_RECORD_RING_ALIGNMENT - 1u)) == 0u;
}

static size_t ns_mpsc_record_ring_offset(const ns_mpsc_record_ring_t *ring, size_t pos)
{
    return pos & (ring->capacity - 1u);
}

static ns_mpsc_record_header_t *ns_mpsc_record_ring_header_at(ns_mpsc_record_ring_t *ring, size_t pos)
{
    return (ns_mpsc_record_header_t *)&ring->storage[ns_mpsc_record_ring_offset(ring, pos)];
}

static void ns_mpsc_record_ring_mark_uncommitted(ns_mpsc_record_header_t *header)
{
    ns_atomic_store_explicit(&header->meta, (size_t)0u, ns_memory_order_release);
}


static size_t ns_mpsc_record_ring_used(size_t write_pos, size_t read_pos)
{
    return write_pos - read_pos;
}

static void ns_mpsc_record_ring_write_parts(
    ns_mpsc_record_ring_t *ring,
    size_t reserve_pos,
    const ns_mpsc_record_part_t *parts,
    size_t part_count)
{
    size_t idx = ns_mpsc_record_ring_offset(ring, reserve_pos + sizeof(ns_mpsc_record_header_t));
    size_t rem = ring->capacity - idx;
    size_t i;

    for(i = 0u; i < part_count; ++i){
        size_t written = 0u;
        const uint8_t *src = (const uint8_t *)parts[i].data;
        while(written < parts[i].size){
            size_t chunk = ns_mpsc_record_ring_min_size(parts[i].size - written, rem);
            memcpy(&ring->storage[idx], src, chunk);
            src     += chunk;
            written += chunk;
            idx     += chunk;
            rem     -= chunk;
            if(rem == 0u){
                idx = 0u;
                rem = ring->capacity;
            }
        }
    }
}


static void ns_mpsc_record_ring_copy_out(
    const ns_mpsc_record_ring_t *ring,
    size_t pos,
    void *out_data,
    size_t size)
{
    size_t offset;
    size_t first_size;
    size_t second_size;

    if(size == 0u) return;

    offset = pos & (ring->capacity - 1u);
    first_size = ns_mpsc_record_ring_min_size(size, ring->capacity - offset);
    second_size = size - first_size;

    if(first_size != 0u) memcpy(out_data, &ring->storage[offset], first_size);
    if(second_size != 0u) memcpy(&((uint8_t *)out_data)[first_size], &ring->storage[0], second_size);
}

static bool ns_mpsc_record_ring_plan_push(
    const ns_mpsc_record_ring_t *ring,
    size_t record_size,
    size_t write_pos,
    size_t read_pos,
    ns_mpsc_record_push_plan_t *out_plan)
{
    size_t total_size;
    size_t free_capacity = ring->capacity - ns_mpsc_record_ring_used(write_pos, read_pos);

    /* 容量是 header 大小的 2 倍以上且为 2 的幂，缓冲区由 header
       大小的槽位组成，所有位置天然对齐，数据天然支持绕回 */
    total_size = ns_mpsc_record_ring_align_up(sizeof(ns_mpsc_record_header_t) + record_size);

    if(free_capacity < total_size) return false;

    out_plan->record_size  = record_size;
    out_plan->total_size   = total_size;
    out_plan->padding_size = total_size - sizeof(ns_mpsc_record_header_t) - record_size;
    out_plan->next_header  = (ns_mpsc_record_header_t *)
                             (ring->storage + (write_pos & (ring->capacity - 1u)));
    return true;
}


/* ---- 公共接口实现 ---- */

/**
 * @brief 初始化 MPSC 记录环。
 *
 * 将 storage 的每个对齐槽位的 header 标记为 uncommitted（valid=0），
 * 保证消费者不会误读残留数据。reserve_pos / write_pos / read_pos
 * 均归零。
 *
 * @return NS_OK 或 NS_E_INVAL。
 */
int ns_mpsc_record_ring_init(ns_mpsc_record_ring_t *ring, void *storage, ns_capacity_t capacity)
{
    size_t capacity_value;
    size_t offset;

    if((ring == NULL) || (storage == NULL)) return NS_E_INVAL;
    if(!ns_mpsc_record_ring_storage_is_aligned(storage)) return NS_E_INVAL;

    capacity_value = (size_t)capacity;
    if(!ns_mpsc_record_ring_capacity_is_valid(capacity_value)) return NS_E_INVAL;

    ring->storage = (uint8_t *)storage;
    ring->capacity = capacity_value;
    ns_atomic_init(&ring->reserve_pos, 0u);
    ns_atomic_init(&ring->write_pos, 0u);
    ns_atomic_init(&ring->read_pos, 0u);

    for(offset = 0u; offset < capacity_value; offset += NS_MPSC_RECORD_RING_ALIGNMENT){
        ns_mpsc_record_header_t *header = (ns_mpsc_record_header_t *)&ring->storage[offset];
        ns_mpsc_record_ring_mark_uncommitted(header);
    }

    return NS_OK;
}

/**
 * @brief 返回环的总字节容量。
 *
 * @return 容量值；ring 无效时返回 0。
 */
size_t ns_mpsc_record_ring_capacity(const ns_mpsc_record_ring_t *ring)
{
    if(!ns_mpsc_record_ring_is_valid(ring)) return 0u;

    return ring->capacity;
}

/**
 * @brief 返回当前空闲字节数。
 *
 * 基于 reserve_pos（生产者侧）和 read_pos（消费者侧）计算。
 * 在多生产者并发场景下，reserve_pos 可能超前于 write_pos，因此返回值
 * 是乐观估计，可能短暂偏大。
 *
 * @return 空闲字节数；ring 无效时返回 0。0 也可能表示环已满。
 */
size_t ns_mpsc_record_ring_free_capacity(const ns_mpsc_record_ring_t *ring)
{
    size_t write_pos;
    size_t read_pos;
    size_t used;

    if(!ns_mpsc_record_ring_is_valid(ring)) return 0u;

    write_pos = ns_atomic_load_explicit(&ring->reserve_pos, ns_memory_order_relaxed);
    read_pos = ns_atomic_load_explicit(&ring->read_pos, ns_memory_order_relaxed);

    used = ns_mpsc_record_ring_used(write_pos, read_pos);
    if(used > ring->capacity) return 0u;
    return ring->capacity - used;
}

/**
 * @brief 返回单条记录可容纳的最大有效负载大小。
 *
 * 保证至少一条最大记录可以完整写入而不与消费者 read_pos 重叠。
 *
 * @return 最大有效负载字节数；ring 无效或容量过小时返回 0。
 */
size_t ns_mpsc_record_ring_max_record_size(const ns_mpsc_record_ring_t *ring)
{
    size_t header_total;

    if(!ns_mpsc_record_ring_is_valid(ring)) return 0u;

    header_total = ns_mpsc_record_ring_align_up(sizeof(ns_mpsc_record_header_t));
    if(ring->capacity <= header_total) return 0u;

    return (ring->capacity / 2u) - header_total;
}

/**
 * @brief 尝试推送一条记录（单块便捷入口）。
 *
 * 将 record_size 为 0 的推送视为合法（写入零长度记录）。
 * 内部委托给 try_pushv。
 *
 * @return NS_OK / NS_E_INVAL / NS_E_QUEUE_FULL。
 */
int ns_mpsc_record_ring_try_push(
    ns_mpsc_record_ring_t *ring,
    const void *record,
    size_t record_size)
{
    ns_mpsc_record_part_t part;

    part.data = record;
    part.size = record_size;
    return ns_mpsc_record_ring_try_pushv(ring, &part, 1u);
}

/**
 * @brief 尝试推送一条记录（scatter-gather 向量入口）。
 *
 * 核心推送路径。多个生产者通过 CAS 抢占 reserve_pos 获取槽位，然后：
 *   1. 标记 header 为 uncommitted（valid=0）
 *   2. 步进 write_pos（release），释放下一个槽位给后续生产者
 *   3. memcpy 数据（支持绕回）
 *   4. release-store valid=1 提交记录
 *
 * 步骤顺序是并发正确性的关键，不可调换。
 *
 * @return NS_OK / NS_E_INVAL / NS_E_QUEUE_FULL。
 */
int ns_mpsc_record_ring_try_pushv(
    ns_mpsc_record_ring_t *ring,
    const ns_mpsc_record_part_t *parts,
    size_t part_count)
{
    ns_mpsc_record_push_plan_t plan;
    size_t record_size = 0u;
    size_t reserve_pos;
    size_t read_pos;
    size_t i;

    if(!ns_mpsc_record_ring_is_valid(ring) || (parts == NULL)) return NS_E_INVAL;
    if(part_count == 0u) return ns_mpsc_record_ring_try_push(ring, NULL, 0u);

    for(i = 0u; i < part_count; ++i){
        size_t prev;
        if(parts[i].data == NULL && parts[i].size != 0u)
            return NS_E_INVAL;
        prev = record_size;
        record_size += parts[i].size;
        if(record_size < prev) return NS_E_INVAL;
    }

    for(;;){
        size_t desired;
        size_t expected;
        size_t write_pos;

        write_pos = ns_atomic_load_explicit(&ring->write_pos, ns_memory_order_acquire);
        read_pos = ns_atomic_load_explicit(&ring->read_pos, ns_memory_order_acquire);

        if(!ns_mpsc_record_ring_plan_push(ring, record_size, write_pos, read_pos, &plan)){
            return NS_E_QUEUE_FULL;
        }

        /* expected = write_pos：只有 reserve_pos == write_pos 时才能抢占 */
        expected = write_pos;
        desired = write_pos + plan.total_size;

        if(ns_atomic_compare_exchange_weak_explicit(
               &ring->reserve_pos,
               &expected,
               desired,
               ns_memory_order_acq_rel,
               ns_memory_order_relaxed)){
            reserve_pos = write_pos;
            break;
        }
    }

    {
        ns_mpsc_record_header_t *header = plan.next_header;
        size_t meta;

        /* 下面的顺序是高性能铁律，不能动 */
        /* 1. 标记 meta 为无效（valid=0），告知消费者此槽位尚未就绪 */
        ns_mpsc_record_ring_mark_uncommitted(header);

        /* 2. 步进 write_pos（release），允许后续生产者抢占下一个槽位 */
        ns_atomic_store_explicit(&ring->write_pos, reserve_pos + plan.total_size, ns_memory_order_release);

        /* 3. memcpy 数据（绕回边界只算一次，padding 无需写零） */
        ns_mpsc_record_ring_write_parts(ring, reserve_pos, parts, part_count);

        /* 4. release-store valid=1，提交整条记录 */
        meta = ns_mpsc_meta_encode_valid(plan.total_size, plan.padding_size);
        ns_atomic_store_explicit(&header->meta, meta, ns_memory_order_release);
    }

    return NS_OK;
}

/**
 * @brief 尝试弹出一条记录（单消费者）。
 *
 * 检查 read_pos 处的 header：若 valid=0（生产者尚未提交），返回
 * NS_E_EMPTY。若 out_capacity 不足，返回 NS_E_NOMEM 并通过
 * out_record_size 写入所需大小，供调用方扩容后重试。
 *
 * 弹出成功后清理 valid 标记并步进 read_pos（release）。
 *
 * @return NS_OK / NS_E_EMPTY / NS_E_INVAL / NS_E_NOMEM / NS_E_CORRUPT。
 */
int ns_mpsc_record_ring_try_pop(
    ns_mpsc_record_ring_t *ring,
    void *out_record,
    size_t out_capacity,
    size_t *out_record_size)
{
    size_t read_pos;
    size_t write_pos;
    size_t used;

    if(out_record_size != NULL) *out_record_size = 0u;

    if(!ns_mpsc_record_ring_is_valid(ring) || (out_record == NULL) || (out_record_size == NULL)) return NS_E_INVAL;

    read_pos = ns_atomic_load_explicit(&ring->read_pos, ns_memory_order_relaxed);
    write_pos = ns_atomic_load_explicit(&ring->write_pos, ns_memory_order_acquire);
    used = ns_mpsc_record_ring_used(write_pos, read_pos);
    if(used > ring->capacity) return NS_E_CORRUPT;
    if(used == 0u) return NS_E_EMPTY;

    {
        ns_mpsc_record_header_t *header = ns_mpsc_record_ring_header_at(ring, read_pos);
        size_t meta = ns_atomic_load_explicit(&header->meta, ns_memory_order_acquire);
        size_t stride;
        size_t payload_size;

        if(!ns_mpsc_meta_is_valid(meta)){
            return NS_E_EMPTY;
        }

        stride = ns_mpsc_meta_stride(meta);
        payload_size = stride - sizeof(*header) - ns_mpsc_meta_padding(meta);

        if(stride > used || stride > ring->capacity){
            return NS_E_INVAL;
        }

        if(payload_size > out_capacity){
            *out_record_size = payload_size;
            return NS_E_NOMEM;
        }

        if(payload_size != 0u){
            ns_mpsc_record_ring_copy_out(
                ring,
                read_pos + sizeof(*header),
                out_record,
                payload_size);
        }

        *out_record_size = payload_size;

        /* 清理 valid，步进 read_pos */
        ns_atomic_store_explicit(&header->meta, (size_t)0u, ns_memory_order_relaxed);
        ns_atomic_store_explicit(&ring->read_pos, read_pos + stride, ns_memory_order_release);
        return NS_OK;
    }
}
