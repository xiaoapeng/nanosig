/**
 * @file nanosig_ringbuf.h
 * @brief nanosig 字节环形缓冲区（mirror 法，单读单写无锁）。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_RINGBUF_H
#define NANOSIG_RINGBUF_H

#include <stddef.h>
#include <stdint.h>

#include <nanosig/nanosig_status.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 字节环形缓冲区。
 *
 * 使用 mirror 法：`w` 和 `r` 持续递增，实际缓冲区位置为 `pos % size`。
 * `w - r` 的正负区分满/空。存储区由调用方提供，ringbuf 不分配内存。
 * 容量 (`size`) 必须为正数。
 */
typedef struct ns_ringbuf {
    uint8_t *buf;
    int32_t  size;
    uint32_t w;
    uint32_t r;
} ns_ringbuf_t;

/**
 * @brief 初始化 ringbuf。
 *
 * @param ringbuf ringbuf 对象。
 * @param buf     调用方提供的存储区。
 * @param size    存储区大小，必须为正数。
 * @return `NS_OK` 表示成功，失败返回负数状态码。
 */
extern int ns_ringbuf_init(ns_ringbuf_t *ringbuf, uint8_t *buf, int32_t size);

/**
 * @brief 清空 ringbuf 中的可读数据（单读单写安全）。
 */
extern void ns_ringbuf_clear(ns_ringbuf_t *ringbuf);

/**
 * @brief 清空 ringbuf 并将 w/r 归零（单读单写不安全）。
 */
extern void ns_ringbuf_reset(ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 总容量。
 */
extern int32_t ns_ringbuf_total_size(const ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 当前可读字节数。
 */
extern int32_t ns_ringbuf_size(const ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 当前可写字节数。
 */
extern int32_t ns_ringbuf_free_size(const ns_ringbuf_t *ringbuf);

/**
 * @brief 写入字节，空间不足时只写入可容纳部分。
 *
 * @return 实际写入字节数。
 */
extern int32_t ns_ringbuf_write(ns_ringbuf_t *ringbuf, const uint8_t *buf, int32_t len);

/**
 * @brief 预写入字节，不移动写指针。
 *
 * @param ringbuf ringbuf 对象。
 * @param offset  从当前写位置开始的偏移。
 * @param buf     数据源。
 * @param len     写入字节数。
 * @return 实际写入字节数。
 */
extern int32_t ns_ringbuf_draft_write(ns_ringbuf_t *ringbuf, int32_t offset, const uint8_t *buf, int32_t len);

/**
 * @brief 跳过并推进写指针。
 *
 * @return 实际跳过的字节数。
 */
extern int32_t ns_ringbuf_write_skip(ns_ringbuf_t *ringbuf, int32_t len);

/**
 * @brief 读取并移除字节，数据不足时只读取已有部分。
 *
 * @return 实际读取字节数。
 */
extern int32_t ns_ringbuf_read(ns_ringbuf_t *ringbuf, uint8_t *buf, int32_t len);

/**
 * @brief 跳过并丢弃字节。
 *
 * @return 实际跳过字节数。
 */
extern int32_t ns_ringbuf_read_skip(ns_ringbuf_t *ringbuf, int32_t len);

/**
 * @brief 零拷贝优先偷看。
 *
 * 当数据未绕回时直接返回内部缓冲区指针（零拷贝）；
 * 当数据绕回时将数据复制到 `buf` 并返回 `buf`；
 * 当可读数据不足时返回 `NULL`。
 *
 * @param ringbuf ringbuf 对象。
 * @param offset  从读位置开始的偏移。
 * @param buf     调用方提供的缓冲区，绕回时用于复制。
 * @param len     [in] 需要读取的字节数；[out] 实际可访问的连续字节数。
 * @return 数据可用时返回内部指针或 `buf`；数据不足时返回 `NULL`。
 */
extern const uint8_t *ns_ringbuf_peek(ns_ringbuf_t *ringbuf, int32_t offset, uint8_t *buf, int32_t *len);

/**
 * @brief 偷看并复制到 buf。
 *
 * @return 实际复制字节数；数据不足时返回 0。
 */
extern int32_t ns_ringbuf_peek_copy(ns_ringbuf_t *ringbuf, int32_t offset, uint8_t *buf, int32_t len);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_RINGBUF_H */
