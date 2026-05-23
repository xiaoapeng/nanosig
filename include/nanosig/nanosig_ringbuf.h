/**
 * @file nanosig_ringbuf.h
 * @brief nanosig 字节环形缓冲区。
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
 * 存储区由调用方提供，ringbuf 不分配内存。
 */
typedef struct ns_ringbuf {
    uint8_t *storage;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
} ns_ringbuf_t;

/**
 * @brief 初始化 ringbuf。
 *
 * @param ringbuf ringbuf 对象。
 * @param storage 调用方提供的存储区。
 * @param capacity 存储区大小，单位为字节。
 * @return `NS_OK` 表示成功，失败返回负数状态码。
 */
int ns_ringbuf_init(ns_ringbuf_t *ringbuf, uint8_t *storage, size_t capacity);

/**
 * @brief 清空 ringbuf 中的可读数据。
 */
void ns_ringbuf_clear(ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 总容量。
 */
size_t ns_ringbuf_capacity(const ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 当前可读字节数。
 */
size_t ns_ringbuf_size(const ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 当前可写字节数。
 */
size_t ns_ringbuf_free_size(const ns_ringbuf_t *ringbuf);

/**
 * @brief 写入字节，空间不足时只写入可容纳部分。
 *
 * @return 实际写入字节数。
 */
size_t ns_ringbuf_write(ns_ringbuf_t *ringbuf, const uint8_t *data, size_t size);

/**
 * @brief 读取并移除字节，数据不足时只读取已有部分。
 *
 * @return 实际读取字节数。
 */
size_t ns_ringbuf_read(ns_ringbuf_t *ringbuf, uint8_t *data, size_t size);

/**
 * @brief 从指定偏移偷看字节，不移动读指针。
 *
 * @return 实际复制字节数。
 */
size_t ns_ringbuf_peek(const ns_ringbuf_t *ringbuf, size_t offset, uint8_t *data, size_t size);

/**
 * @brief 跳过并丢弃最多 `size` 字节。
 *
 * @return 实际跳过字节数。
 */
size_t ns_ringbuf_skip(ns_ringbuf_t *ringbuf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_RINGBUF_H */
