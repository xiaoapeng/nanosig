/**
 * @file ns_formatio.h
 * @brief nanosig 格式化输出接口 — 移植自 eventhub_os eh_formatio.h。
 * @date 2026-07-11
 *
 * 提供轻量级格式化输出（printf/snprintf/vprintf）及可插拔 stream 抽象层。
 * 不依赖 libc 的 printf 族，自带浮点数、十六进制数组等格式化支持。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_FORMATIO_H
#define NANOSIG_FORMATIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Stream 抽象层                                                       */
/* ================================================================== */

enum ns_stream_type {
    NS_STREAM_TYPE_FUNCTION,
    NS_STREAM_TYPE_FUNCTION_NO_CACHE,
    NS_STREAM_TYPE_MEMORY,
    NS_STREAM_TYPE_MEMORY_UNBOUNDED,  /* 无界内存流（ns_sprintf）：不做上界检查 */
};

struct ns_stream_base {
    enum ns_stream_type type;
};

struct ns_stream_function {
    struct ns_stream_base base;
    void (*write)(void *stream, const uint8_t *buf, size_t size);
    uint8_t *cache;
    uint8_t *pos;
    uint8_t *end;
};

struct ns_stream_function_no_cache {
    struct ns_stream_base base;
    void (*write)(void *stream, const uint8_t *buf, size_t size);
    void (*finish)(void *stream);
};

struct ns_stream_memory {
    struct ns_stream_base base;
    uint8_t *buf;
    uint8_t *pos;
    uint8_t *end;
};

extern struct ns_stream_function _ns_stdout;

#define NS_STDOUT ((struct ns_stream_base *)&_ns_stdout)

/* ================================================================== */
/*  printf 族声明                                                        */
/* ================================================================== */

/**
 * @brief nanosig printf 族格式化输出。
 *
 * 支持 C99 printf 的常见转换（%d/%u/%x/%f/%s/%c/%p/%% 等），以及 nanosig 扩展
 * %q/%Q（数组）与 %b（二进制）。与 C99 `snprintf` 存在以下文档化差异：
 * - `%g`/`%G` 等价于 `%f`，不剥除尾随零、不按指数范围切换到 `%e`；
 * * 宽度/`*` 负精度被钳为 0（C99 规定"视同未指定"）；
 * - `%q`/`%Q` 的精度是**元素个数**（不是字节数），元素宽度由长度修饰符决定：
 *   `hh` = `unsigned char`、`h` = `unsigned short`、缺省 = `unsigned int`、
 *   `l` = `unsigned long`、`ll` = `unsigned long long`；每个元素输出 `2*sizeof(元素)`
 *   个十六进制字符（小端机器按高位字节在前），元素之间用一个空格分隔。精度为负时不输出；
 * - `%.0d`/`%.0x` 零值输出 `"0"`（C99 输出空字符串）；
 * - `%#x`/`%#o` 零值仍加前缀（C99 零值不加前缀）；
 * - 格式串宽度/精度由调用方保证在合理范围内，超大值可能触发有符号溢出 UB。
 *
 * 调用方应按上述差异使用，或按需避免受影响的转换。
 */
extern int ns_vprintf(const char *fmt, va_list args);
extern int ns_printf(const char *fmt, ...);

extern int ns_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

extern int ns_vsprintf(char *buf, const char *fmt, va_list args);

extern int ns_snprintf(char *buf, size_t size, const char *fmt, ...);
extern int ns_sprintf(char *buf, const char *fmt, ...);

extern int ns_stream_vprintf(struct ns_stream_base *stream, const char *fmt, va_list args);
extern int ns_stream_printf(struct ns_stream_base *stream, const char *fmt, ...);
extern void ns_stream_putc(struct ns_stream_base *stream, int c);

/**
 * @brief 向流写入以 NUL 结尾的字符串。
 *
 * 若 `s` 为 NULL，行为未定义（调用方应保证 `s` 非 NULL）。与 `vprintf_string`
 * 将 NULL 替换为 `"(null)"` 不同，本函数不做 NULL 替换，直接解引用 `s`。
 *
 * @param stream 输出流，不可为 NULL。
 * @param s 待写入的 NUL 结尾字符串，不可为 NULL。
 */
extern int ns_stream_puts(struct ns_stream_base *stream, const char *s);
extern void ns_stream_finish(struct ns_stream_base *stream);

/* ================================================================== */
/*  Stream 初始化内联函数                                                 */
/* ================================================================== */

static inline void ns_stream_function_init(
    struct ns_stream_function *stream,
    void (*write)(void *stream, const uint8_t *buf, size_t size),
    uint8_t *cache, size_t cache_size)
{
    stream->base.type = NS_STREAM_TYPE_FUNCTION;
    stream->write = write;
    stream->cache = cache;
    stream->pos = cache;
    stream->end = cache + cache_size;
}

static inline void ns_stream_function_no_cache_init(
    struct ns_stream_function_no_cache *stream,
    void (*write)(void *stream, const uint8_t *buf, size_t size),
    void (*finish)(void *stream))
{
    stream->base.type = NS_STREAM_TYPE_FUNCTION_NO_CACHE;
    stream->write = write;
    stream->finish = finish;
}

static inline void ns_stream_memory_init(
    struct ns_stream_memory *stream, uint8_t *buf, size_t size)
{
    stream->base.type = NS_STREAM_TYPE_MEMORY;
    stream->buf = buf;
    stream->pos = buf;
    stream->end = size ? buf + size : buf;  /* size==0 不做空指针加法 */
}

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_FORMATIO_H */
