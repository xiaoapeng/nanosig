/**
 * @file ns_ringbuf.c
 * @brief nanosig 字节环形缓冲区实现。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_ringbuf.h>

#include <string.h>

static size_t ns_ringbuf_min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static int ns_ringbuf_is_valid(const ns_ringbuf_t *ringbuf)
{
    return (ringbuf != NULL) &&
           (ringbuf->storage != NULL) &&
           (ringbuf->capacity != 0u) &&
           (ringbuf->write_pos >= ringbuf->read_pos) &&
           ((ringbuf->write_pos - ringbuf->read_pos) <= ringbuf->capacity);
}

int ns_ringbuf_init(ns_ringbuf_t *ringbuf, uint8_t *storage, size_t capacity)
{
    if((ringbuf == NULL) || (storage == NULL) || (capacity == 0u)) return NS_E_INVAL;

    ringbuf->storage = storage;
    ringbuf->capacity = capacity;
    ringbuf->read_pos = 0u;
    ringbuf->write_pos = 0u;
    return NS_OK;
}

void ns_ringbuf_clear(ns_ringbuf_t *ringbuf)
{
    if(!ns_ringbuf_is_valid(ringbuf)) return;

    ringbuf->read_pos = ringbuf->write_pos;
}

size_t ns_ringbuf_capacity(const ns_ringbuf_t *ringbuf)
{
    if(!ns_ringbuf_is_valid(ringbuf)) return 0u;

    return ringbuf->capacity;
}

size_t ns_ringbuf_size(const ns_ringbuf_t *ringbuf)
{
    if(!ns_ringbuf_is_valid(ringbuf)) return 0u;

    return ringbuf->write_pos - ringbuf->read_pos;
}

size_t ns_ringbuf_free_size(const ns_ringbuf_t *ringbuf)
{
    if(!ns_ringbuf_is_valid(ringbuf)) return 0u;

    return ringbuf->capacity - ns_ringbuf_size(ringbuf);
}

size_t ns_ringbuf_write(ns_ringbuf_t *ringbuf, const uint8_t *data, size_t size)
{
    size_t writable;
    size_t first_index;
    size_t first_size;
    size_t second_size;

    if(!ns_ringbuf_is_valid(ringbuf) || (data == NULL) || (size == 0u)) return 0u;

    writable = ns_ringbuf_min_size(size, ns_ringbuf_free_size(ringbuf));
    first_index = ringbuf->write_pos % ringbuf->capacity;
    first_size = ns_ringbuf_min_size(writable, ringbuf->capacity - first_index);
    second_size = writable - first_size;

    if(first_size != 0u) memcpy(&ringbuf->storage[first_index], data, first_size);
    if(second_size != 0u) memcpy(&ringbuf->storage[0], &data[first_size], second_size);

    ringbuf->write_pos += writable;
    return writable;
}

size_t ns_ringbuf_read(ns_ringbuf_t *ringbuf, uint8_t *data, size_t size)
{
    size_t readable;

    if(!ns_ringbuf_is_valid(ringbuf) || (data == NULL) || (size == 0u)) return 0u;

    readable = ns_ringbuf_peek(ringbuf, 0u, data, size);
    ringbuf->read_pos += readable;
    return readable;
}

size_t ns_ringbuf_peek(const ns_ringbuf_t *ringbuf, size_t offset, uint8_t *data, size_t size)
{
    size_t available;
    size_t readable;
    size_t first_index;
    size_t first_size;
    size_t second_size;

    if(!ns_ringbuf_is_valid(ringbuf) || (data == NULL) || (size == 0u)) return 0u;

    available = ns_ringbuf_size(ringbuf);
    if(offset >= available) return 0u;

    readable = ns_ringbuf_min_size(size, available - offset);
    first_index = (ringbuf->read_pos + offset) % ringbuf->capacity;
    first_size = ns_ringbuf_min_size(readable, ringbuf->capacity - first_index);
    second_size = readable - first_size;

    if(first_size != 0u) memcpy(data, &ringbuf->storage[first_index], first_size);
    if(second_size != 0u) memcpy(&data[first_size], &ringbuf->storage[0], second_size);

    return readable;
}

size_t ns_ringbuf_skip(ns_ringbuf_t *ringbuf, size_t size)
{
    size_t skipped;

    if(!ns_ringbuf_is_valid(ringbuf) || (size == 0u)) return 0u;

    skipped = ns_ringbuf_min_size(size, ns_ringbuf_size(ringbuf));
    ringbuf->read_pos += skipped;
    return skipped;
}
