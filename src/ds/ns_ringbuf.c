/**
 * @file ns_ringbuf.c
 * @brief nanosig 字节环形缓冲区实现（mirror 法）。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <string.h>

#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_atomic.h>
#include <nanosig/nanosig_ringbuf.h>
#include <nanosig/nanosig_status.h>

/* w / r 在 [0, 2*size) 的镜像空间中递增，pos % (2*size) 保持不越界 */
#define ns_ringbuf_fix(ringbuf, pos) ((pos) % ((ringbuf)->size * 2u))

/* 字节计数函数返回正数表示写入/读取字节数；0 表示未进展。
   draft_write 越界时返回 NS_E_RANGE（负值），与字节计数不冲突。 */

/* size 上限 UINT32_MAX/3 < INT32_MAX，因此 wl/rl 与 size 族返回值在转 int32_t 时
   不会因 size 越界而截断为负值。本断言将这一不变量固化在编译期。 */
NS_STATIC_ASSERT((UINT32_MAX / 3u) <= (uint32_t)INT32_MAX,
               "ringbuf size 上限必须 ≤ INT32_MAX，以保证 wl/rl 与 size 族返回值转 int32_t 不截断");

int ns_ringbuf_init(ns_ringbuf_t *ringbuf, uint8_t *buf, uint32_t size)
{
    if((ringbuf == NULL) || (buf == NULL) || (size == 0u)) return NS_E_INVAL;
    /*
     * 镜像法约束：w + wl < 3*size（w < 2*size, wl ≤ size），
     * 防止中间加法 uint32_t 溢出的条件是 3*size ≤ UINT32_MAX，
     * 故 size 上限为 UINT32_MAX / 3 ≈ 1.43 GiB。
     */
    if(size > (UINT32_MAX / 3u)) return NS_E_PARAM;

    ringbuf->buf = buf;
    ringbuf->size = size;
    ringbuf->r = 0u;
    ringbuf->w = 0u;
    return NS_OK;
}

int32_t ns_ringbuf_total_size(const ns_ringbuf_t *ringbuf)
{
    if(ringbuf == NULL) return -1;
    return (int32_t)ringbuf->size;
}

/*
 * 计算当前可读数据字节数（int32_t 非负成功值，与 nanosig_status.h 风格一致）。
 *
 * size 为 ringbuf 容量，w 和 r 为写/读指针（各自被 ns_ringbuf_fix
 * 约束在 [0, 2*size) 内）。返回 [0, size] 范围的可读字节数。
 *   w >= r → data = w - r
 *   w <  r → data = (2*size) - (r - w)
 * size 上限为 UINT32_MAX / 3（约 1.43 GiB），防止中间加法溢出 uint32_t。
 */
static int32_t ns_ringbuf_data_size(uint32_t size, uint32_t w, uint32_t r)
{
    if(w >= r) return (int32_t)(w - r);
    return (int32_t)((size * 2u) - (r - w));
}

int32_t ns_ringbuf_size(const ns_ringbuf_t *ringbuf)
{
    if(ringbuf == NULL) return -1;
    return ns_ringbuf_data_size(ringbuf->size, ringbuf->w, ringbuf->r);
}

int32_t ns_ringbuf_free_size(const ns_ringbuf_t *ringbuf)
{
    if(ringbuf == NULL) return -1;
    return (int32_t)(ringbuf->size - (uint32_t)ns_ringbuf_data_size(ringbuf->size, ringbuf->w, ringbuf->r));
}

int32_t ns_ringbuf_write(ns_ringbuf_t *ringbuf, const uint8_t *buf, uint32_t len)
{
    uint32_t w;
    uint32_t r;
    uint32_t free_size;
    uint32_t wl;
    uint32_t write_size_first_max;

    if((ringbuf == NULL) || (buf == NULL) || (len == 0u)) return 0;

    w = ringbuf->w;
    r = ringbuf->r;
    free_size = ringbuf->size - (uint32_t)ns_ringbuf_data_size(ringbuf->size, w, r);
    wl = len > free_size ? free_size : len;
    if(wl == 0u) return 0;

    w = w % ringbuf->size;
    write_size_first_max = ringbuf->size - w;

    if(wl <= write_size_first_max){
        memcpy(ringbuf->buf + w, buf, (size_t)wl);
    } else {
        memcpy(ringbuf->buf + w, buf, (size_t)write_size_first_max);
        memcpy(ringbuf->buf, buf + write_size_first_max, (size_t)(wl - write_size_first_max));
    }

    /* release 屏障：保证数据写入完成后再发布写指针 w 的更新 */
    ns_memory_order_release_barrier();
    ringbuf->w = ns_ringbuf_fix(ringbuf, ringbuf->w + wl);
    return (int32_t)wl;
}

int32_t ns_ringbuf_draft_write(ns_ringbuf_t *ringbuf, uint32_t offset, const uint8_t *buf, uint32_t len)
{
    uint32_t w;
    uint32_t r;
    uint32_t free_size;
    uint32_t wl;
    uint32_t write_size_first_max;

    if((ringbuf == NULL) || (buf == NULL) || (len == 0u)) return 0;

    w = ringbuf->w;
    r = ringbuf->r;
    free_size = ringbuf->size - (uint32_t)ns_ringbuf_data_size(ringbuf->size, w, r);
    if(offset >= free_size) return (int32_t)NS_E_RANGE; /* offset 越界：映射到 NS_E_RANGE */

    free_size -= offset;
    wl = len > free_size ? free_size : len;
    if(wl == 0u) return 0;

    w = (w + offset) % ringbuf->size;
    write_size_first_max = ringbuf->size - w;

    if(wl <= write_size_first_max){
        memcpy(ringbuf->buf + w, buf, (size_t)wl);
    } else {
        memcpy(ringbuf->buf + w, buf, (size_t)write_size_first_max);
        memcpy(ringbuf->buf, buf + write_size_first_max, (size_t)(wl - write_size_first_max));
    }

    return (int32_t)wl;
}

int32_t ns_ringbuf_write_skip(ns_ringbuf_t *ringbuf, uint32_t len)
{
    uint32_t free_size;
    uint32_t wl;

    if((ringbuf == NULL) || (len == 0u)) return 0;

    free_size = ringbuf->size - (uint32_t)ns_ringbuf_data_size(ringbuf->size, ringbuf->w, ringbuf->r);
    wl = len > free_size ? free_size : len;
    if(wl == 0u) return 0;

    /* release 屏障：保证空间预留意图发布到写指针 w 之前所有相关写入已完成 */
    ns_memory_order_release_barrier();
    ringbuf->w = ns_ringbuf_fix(ringbuf, ringbuf->w + wl);
    return (int32_t)wl;
}

int32_t ns_ringbuf_read(ns_ringbuf_t *ringbuf, uint8_t *buf, uint32_t len)
{
    uint32_t r;
    uint32_t w;
    uint32_t data_size;
    uint32_t rl;
    uint32_t read_size_first_max;

    if((ringbuf == NULL) || (buf == NULL) || (len == 0u)) return 0;

    r = ringbuf->r;
    w = ringbuf->w;
    data_size = (uint32_t)ns_ringbuf_data_size(ringbuf->size, w, r);
    rl = len > data_size ? data_size : len;
    if(rl == 0u) return 0;

    r = r % ringbuf->size;
    read_size_first_max = ringbuf->size - r;

    if(rl <= read_size_first_max){
        memcpy(buf, ringbuf->buf + r, (size_t)rl);
    } else {
        memcpy(buf, ringbuf->buf + r, (size_t)read_size_first_max);
        memcpy(buf + read_size_first_max, ringbuf->buf, (size_t)(rl - read_size_first_max));
    }

    /* release 屏障：保证数据拷出完成后再发布读指针 r 的更新 */
    ns_memory_order_release_barrier();
    ringbuf->r = ns_ringbuf_fix(ringbuf, ringbuf->r + rl);
    return (int32_t)rl;
}

int32_t ns_ringbuf_read_skip(ns_ringbuf_t *ringbuf, uint32_t len)
{
    uint32_t data_size;
    uint32_t rl;

    if((ringbuf == NULL) || (len == 0u)) return 0;

    data_size = (uint32_t)ns_ringbuf_data_size(ringbuf->size, ringbuf->w, ringbuf->r);
    rl = len > data_size ? data_size : len;
    if(rl == 0u) return 0;

    /* release 屏障：与 read 对称，发布读指针 r 前确保相关读侧操作可见 */
    ns_memory_order_release_barrier();
    ringbuf->r = ns_ringbuf_fix(ringbuf, ringbuf->r + rl);
    return (int32_t)rl;
}

const uint8_t *ns_ringbuf_peek(ns_ringbuf_t *ringbuf, uint32_t offset, uint8_t *buf, uint32_t *len)
{
    uint32_t r;
    uint32_t w;
    uint32_t data_size;
    uint32_t read_size_first_max;
    uint32_t rl;

    if((ringbuf == NULL) || (buf == NULL) || (len == NULL)) return NULL;

    w = ringbuf->w;
    r = ringbuf->r;
    data_size = (uint32_t)ns_ringbuf_data_size(ringbuf->size, w, r);
    if(offset >= data_size) return NULL;

    data_size -= offset;
    rl = *len;
    if(data_size < rl) return NULL;

    r = (r + offset) % ringbuf->size;
    read_size_first_max = ringbuf->size - r;

    if(rl <= read_size_first_max){
        /* 非绕回路径：*len 设为实际连续段大小（可能大于请求值） */
        *len = data_size > read_size_first_max ? read_size_first_max : data_size;
        return ringbuf->buf + r;
    } else {
        /* 绕回路径：*len 保持请求值 */
        memcpy(buf, ringbuf->buf + r, (size_t)read_size_first_max);
        memcpy(buf + read_size_first_max, ringbuf->buf, (size_t)(rl - read_size_first_max));
        return buf;
    }
}

int32_t ns_ringbuf_peek_copy(ns_ringbuf_t *ringbuf, uint32_t offset, uint8_t *buf, uint32_t len)
{
    uint32_t r;
    uint32_t w;
    uint32_t data_size;
    uint32_t rl;
    uint32_t read_size_first_max;

    if((ringbuf == NULL) || (buf == NULL) || (len == 0u)) return 0;

    w = ringbuf->w;
    r = ringbuf->r;
    data_size = (uint32_t)ns_ringbuf_data_size(ringbuf->size, w, r);
    if(offset >= data_size) return 0;

    data_size -= offset;
    rl = len;
    if(data_size < rl) return 0;

    r = (r + offset) % ringbuf->size;
    read_size_first_max = ringbuf->size - r;

    if(rl <= read_size_first_max){
        memcpy(buf, ringbuf->buf + r, (size_t)rl);
    } else {
        memcpy(buf, ringbuf->buf + r, (size_t)read_size_first_max);
        memcpy(buf + read_size_first_max, ringbuf->buf, (size_t)(rl - read_size_first_max));
    }

    return (int32_t)rl;
}

void ns_ringbuf_clear(ns_ringbuf_t *ringbuf)
{
    if(ringbuf == NULL) return;
    /* release 屏障：保证发布读指针 r 追赶写指针 w 之前，所有相关读侧操作已完成 */
    ns_memory_order_release_barrier();
    ringbuf->r = ringbuf->w;
}

void ns_ringbuf_reset(ns_ringbuf_t *ringbuf)
{
    if(ringbuf == NULL) return;
    ringbuf->r = 0u;
    ringbuf->w = 0u;
}
