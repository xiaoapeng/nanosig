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

extern int ns_vprintf(const char *fmt, va_list args);
extern int ns_printf(const char *fmt, ...);

extern int ns_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

extern int ns_vsprintf(char *buf, const char *fmt, va_list args);

extern int ns_snprintf(char *buf, size_t size, const char *fmt, ...);
extern int ns_sprintf(char *buf, const char *fmt, ...);

extern int ns_stream_vprintf(struct ns_stream_base *stream, const char *fmt, va_list args);
extern int ns_stream_printf(struct ns_stream_base *stream, const char *fmt, ...);
extern void ns_stream_putc(struct ns_stream_base *stream, int c);
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
    stream->end = buf + size;
}

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_FORMATIO_H */
