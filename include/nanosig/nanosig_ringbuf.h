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
 * `w` 与 `r` 之差的正负区分满/空。存储区由调用方提供，ringbuf 不分配内存。
 *
 * @note `size` 为 uint32_t，取值范围 [1, UINT32_MAX/3]：
 *       1. 上限 `UINT32_MAX/3`（约 1.43 GiB）：保证 `w + wl < 3*size` 不溢出 uint32_t。
 *       2. 下限 1：`size = 0` 无意义。
 *       3. 所有 int32_t 返回值（size/free_size/total_size/write/read 等）都是安全的：
 *          `UINT32_MAX/3 < INT32_MAX`，因此容量查询和实际副本量都不会被 int32_t 截断为负值。
 *       写入/读取函数的 `len` 参数为 uint32_t，单次操作上限由 `size` 决定。
 */
typedef struct ns_ringbuf {
    uint8_t *buf;
    uint32_t size;
    uint32_t w;
    uint32_t r;
} ns_ringbuf_t;

/**
 * @brief 初始化 ringbuf。
 *
 * @param ringbuf ringbuf 对象。
 * @param buf     调用方提供的存储区。
 * @param size    存储区大小，必须为正数且 ≤ `UINT32_MAX / 3`（约 1.43 GiB）。
 * @return `NS_OK` 表示成功；
 *         失败返回 `NS_E_INVAL`（NULL 指针或 size=0），
 *         或 `NS_E_PARAM`（size 超过镜像法上限）。
 */
extern int ns_ringbuf_init(ns_ringbuf_t *ringbuf, uint8_t *buf, uint32_t size);

/**
 * @brief 清空 ringbuf 中的可读数据。
 *
 * 将读指针（`r`）后移到写指针（`w`）位置，逻辑上丢弃所有数据。
 *
 * @warning 本函数访问写者的 `w` 并修改读者的 `r`，因而非原子操作。
 *          **必须由读者线程调用**
 *          写者线程调用 `clear` 会破坏单读单写安全假设。
 *
 * @param ringbuf ringbuf 对象。
 */
extern void ns_ringbuf_clear(ns_ringbuf_t *ringbuf);

/**
 * @brief 清空 ringbuf 并将 w/r 归零（单读单写不安全）。
 *
 * @param ringbuf ringbuf 对象。
 */
extern void ns_ringbuf_reset(ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 总容量。
 *
 * @param ringbuf ringbuf 对象。
 * @return ringbuf 总容量（> 0）；ringbuf 为 NULL 时返回 -1。
 */
extern int32_t ns_ringbuf_total_size(const ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 当前可读字节数。
 *
 * @param ringbuf ringbuf 对象。
 * @return 当前可读字节数；ringbuf 为 NULL 时返回 -1。
 */
extern int32_t ns_ringbuf_size(const ns_ringbuf_t *ringbuf);

/**
 * @brief 返回 ringbuf 当前可写字节数。
 *
 * @param ringbuf ringbuf 对象。
 * @return 当前可写字节数；ringbuf 为 NULL 时返回 -1。
 */
extern int32_t ns_ringbuf_free_size(const ns_ringbuf_t *ringbuf);

/**
 * @brief 写入字节，空间不足时只写入可容纳部分。
 *
 * @param ringbuf ringbuf 对象。
 * @param buf     数据源。
 * @param len     请求写入字节数。
 * @return 实际写入字节数。
 */
extern int32_t ns_ringbuf_write(ns_ringbuf_t *ringbuf, const uint8_t *buf, uint32_t len);

/**
 * @brief 预写入字节，不移动写指针。
 *
 * @param ringbuf ringbuf 对象。
 * @param offset  从当前写位置开始的偏移。
 *                offset 越界（超过当前空闲空间）时返回 `NS_E_RANGE`。
 * @param buf     数据源。
 * @param len     写入字节数。
 * @return 实际写入字节数（≥ 0）；参数错误或无空间返回 0；offset 越界返回 `NS_E_RANGE`。
 */
extern int32_t ns_ringbuf_draft_write(ns_ringbuf_t *ringbuf, uint32_t offset, const uint8_t *buf, uint32_t len);

/**
 * @brief 跳过并推进写指针。
 *
 * @param ringbuf ringbuf 对象。
 * @param len     请求跳过字节数。
 * @return 实际跳过的字节数。
 */
extern int32_t ns_ringbuf_write_skip(ns_ringbuf_t *ringbuf, uint32_t len);

/**
 * @brief 读取并移除字节，数据不足时只读取已有部分。
 *
 * @param ringbuf ringbuf 对象。
 * @param buf     数据目标缓冲区。
 * @param len     请求读取字节数。
 * @return 实际读取字节数。
 */
extern int32_t ns_ringbuf_read(ns_ringbuf_t *ringbuf, uint8_t *buf, uint32_t len);

/**
 * @brief 跳过并丢弃字节。
 *
 * @param ringbuf ringbuf 对象。
 * @param len     请求跳过字节数。
 * @return 实际跳过字节数。
 */
extern int32_t ns_ringbuf_read_skip(ns_ringbuf_t *ringbuf, uint32_t len);

/**
 * @brief 零拷贝优先偷看。
 *
 * 当数据未绕回时直接返回内部缓冲区指针（零拷贝）；
 * 当数据绕回时将数据复制到 `buf` 并返回 `buf`；
 * 当可读数据不足时返回 `NULL`。
 *
 * @note *len 输出语义因代码路径而异：
 *       - 非绕回路径（零拷贝）：*len 设置为从返回指针位置开始的连续可读字节数，
 *         可能大于请求值（调用者可用此值获取更大的连续段）。
 *       - 绕回路径（已拷贝到 buf）：*len 保持为输入时请求的 len 值不变。
 *
 * @param ringbuf ringbuf 对象。
 * @param offset  从读位置开始的偏移。
 * @param buf     调用方提供的缓冲区，绕回时用于复制。
 * @param len     [in] 需要读取的字节数；[out] 实际可访问的连续字节数。
 * @return 数据可用时返回内部指针或 `buf`；数据不足时返回 `NULL`。
 */
extern const uint8_t *ns_ringbuf_peek(ns_ringbuf_t *ringbuf, uint32_t offset, uint8_t *buf, uint32_t *len);

/**
 * @brief 偷看并复制到 buf。
 *
 * @param ringbuf ringbuf 对象。
 * @param offset  从读位置开始的偏移。
 * @param buf     数据目标缓冲区。
 * @param len     需要读取的字节数。
 * @return 实际复制字节数；数据不足时返回 0。
 */
extern int32_t ns_ringbuf_peek_copy(ns_ringbuf_t *ringbuf, uint32_t offset, uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_RINGBUF_H */
