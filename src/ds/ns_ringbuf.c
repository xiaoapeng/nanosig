/**
 * @file ns_ringbuf.c
 * @brief nanosig 字节环形缓冲区实现（mirror 法）。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <string.h>

#include <nanosig/nanosig_ringbuf.h>
#define ns_ringbuf_fix(ringbuf, pos) ((pos) % ((uint32_t)((ringbuf)->size << 1)))

int ns_ringbuf_init(ns_ringbuf_t *ringbuf, uint8_t *buf, int32_t size)
{
    if((ringbuf == NULL) || (buf == NULL) || (size <= 0)) return NS_E_INVAL;

    ringbuf->buf = buf;
    ringbuf->size = size;
    ringbuf->r = 0u;
    ringbuf->w = 0u;
    return NS_OK;
}

int32_t ns_ringbuf_total_size(const ns_ringbuf_t *ringbuf)
{
    if(ringbuf == NULL) return 0;
    return ringbuf->size;
}

int32_t ns_ringbuf_size(const ns_ringbuf_t *ringbuf)
{
    uint32_t w;
    uint32_t r;
    int32_t diff;

    if(ringbuf == NULL) return 0;

    w = ringbuf->w;
    r = ringbuf->r;
    diff = (int32_t)(w - r);
    return diff >= 0 ? diff : (ringbuf->size << 1) + diff;
}

int32_t ns_ringbuf_free_size(const ns_ringbuf_t *ringbuf)
{
    uint32_t w;
    uint32_t r;
    int32_t diff;

    if(ringbuf == NULL) return 0;

    w = ringbuf->w;
    r = ringbuf->r;
    diff = (int32_t)(w - r);
    return diff >= 0 ? ringbuf->size - diff : -(ringbuf->size + diff);
}

int32_t ns_ringbuf_write(ns_ringbuf_t *ringbuf, const uint8_t *buf, int32_t len)
{
    uint32_t w;
    int32_t free_size;
    int32_t write_size_first_max;
    int32_t wl;

    if((ringbuf == NULL) || (buf == NULL) || (len <= 0)) return 0;

    free_size = ns_ringbuf_free_size(ringbuf);
    wl = len > free_size ? free_size : len;
    if(wl <= 0) return 0;

    w = ringbuf->w % (uint32_t)ringbuf->size;
    write_size_first_max = ringbuf->size - (int32_t)w;

    if(wl <= write_size_first_max){
        memcpy(ringbuf->buf + w, buf, (size_t)wl);
    } else {
        memcpy(ringbuf->buf + w, buf, (size_t)write_size_first_max);
        memcpy(ringbuf->buf, buf + write_size_first_max, (size_t)(wl - write_size_first_max));
    }

    ringbuf->w = ns_ringbuf_fix(ringbuf, ringbuf->w + (uint32_t)wl);
    return wl;
}

int32_t ns_ringbuf_draft_write(ns_ringbuf_t *ringbuf, int32_t offset, const uint8_t *buf, int32_t len)
{
    uint32_t w;
    int32_t free_size;
    int32_t write_size_first_max;
    int32_t wl;

    if((ringbuf == NULL) || (buf == NULL) || (len <= 0)) return 0;

    free_size = ns_ringbuf_free_size(ringbuf) - offset;
    wl = len > free_size ? free_size : len;
    if(wl <= 0) return 0;

    w = (ringbuf->w + (uint32_t)offset) % (uint32_t)ringbuf->size;
    write_size_first_max = ringbuf->size - (int32_t)w;

    if(wl <= write_size_first_max){
        memcpy(ringbuf->buf + w, buf, (size_t)wl);
    } else {
        memcpy(ringbuf->buf + w, buf, (size_t)write_size_first_max);
        memcpy(ringbuf->buf, buf + write_size_first_max, (size_t)(wl - write_size_first_max));
    }

    return wl;
}

int32_t ns_ringbuf_write_skip(ns_ringbuf_t *ringbuf, int32_t len)
{
    int32_t free_size;
    int32_t wl;

    if((ringbuf == NULL) || (len <= 0)) return 0;

    free_size = ns_ringbuf_free_size(ringbuf);
    wl = len > free_size ? free_size : len;
    if(wl <= 0) return 0;

    ringbuf->w = ns_ringbuf_fix(ringbuf, ringbuf->w + (uint32_t)wl);
    return wl;
}

int32_t ns_ringbuf_read(ns_ringbuf_t *ringbuf, uint8_t *buf, int32_t len)
{
    uint32_t r;
    int32_t size;
    int32_t read_size_first_max;
    int32_t rl;

    if((ringbuf == NULL) || (buf == NULL) || (len <= 0)) return 0;

    size = ns_ringbuf_size(ringbuf);
    rl = len > size ? size : len;
    if(rl <= 0) return 0;

    r = ringbuf->r % (uint32_t)ringbuf->size;
    read_size_first_max = ringbuf->size - (int32_t)r;

    if(rl <= read_size_first_max){
        memcpy(buf, ringbuf->buf + r, (size_t)rl);
    } else {
        memcpy(buf, ringbuf->buf + r, (size_t)read_size_first_max);
        memcpy(buf + read_size_first_max, ringbuf->buf, (size_t)(rl - read_size_first_max));
    }

    ringbuf->r = ns_ringbuf_fix(ringbuf, ringbuf->r + (uint32_t)rl);
    return rl;
}

int32_t ns_ringbuf_read_skip(ns_ringbuf_t *ringbuf, int32_t len)
{
    int32_t size;
    int32_t rl;

    if((ringbuf == NULL) || (len <= 0)) return 0;

    size = ns_ringbuf_size(ringbuf);
    rl = len > size ? size : len;
    if(rl <= 0) return 0;

    ringbuf->r = ns_ringbuf_fix(ringbuf, ringbuf->r + (uint32_t)rl);
    return rl;
}

const uint8_t *ns_ringbuf_peek(ns_ringbuf_t *ringbuf, int32_t offset, uint8_t *buf, int32_t *len)
{
    uint32_t r;
    int32_t size;
    int32_t read_size_first_max;
    int32_t rl;

    if((ringbuf == NULL) || (buf == NULL) || (len == NULL)) return NULL;

    size = ns_ringbuf_size(ringbuf) - offset;
    rl = *len;
    if(size < rl) return NULL;

    r = (ringbuf->r + (uint32_t)offset) % (uint32_t)ringbuf->size;
    read_size_first_max = ringbuf->size - (int32_t)r;

    if(rl <= read_size_first_max){
        *len = size > read_size_first_max ? read_size_first_max : size;
        return ringbuf->buf + r;
    } else {
        memcpy(buf, ringbuf->buf + r, (size_t)read_size_first_max);
        memcpy(buf + read_size_first_max, ringbuf->buf, (size_t)(rl - read_size_first_max));
        return buf;
    }
}

int32_t ns_ringbuf_peek_copy(ns_ringbuf_t *ringbuf, int32_t offset, uint8_t *buf, int32_t len)
{
    uint32_t r;
    int32_t size;
    int32_t read_size_first_max;
    int32_t rl;

    if((ringbuf == NULL) || (buf == NULL) || (len <= 0)) return 0;

    size = ns_ringbuf_size(ringbuf) - offset;
    rl = len;
    if(size < rl) return 0;

    r = (ringbuf->r + (uint32_t)offset) % (uint32_t)ringbuf->size;
    read_size_first_max = ringbuf->size - (int32_t)r;

    if(rl <= read_size_first_max){
        memcpy(buf, ringbuf->buf + r, (size_t)rl);
    } else {
        memcpy(buf, ringbuf->buf + r, (size_t)read_size_first_max);
        memcpy(buf + read_size_first_max, ringbuf->buf, (size_t)(rl - read_size_first_max));
    }

    return rl;
}

void ns_ringbuf_clear(ns_ringbuf_t *ringbuf)
{
    if(ringbuf == NULL) return;
    ringbuf->r = ringbuf->w;
}

void ns_ringbuf_reset(ns_ringbuf_t *ringbuf)
{
    if(ringbuf == NULL) return;
    ringbuf->r = 0u;
    ringbuf->w = 0u;
}
