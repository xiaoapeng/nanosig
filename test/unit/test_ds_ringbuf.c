/**
 * @file test_ds_ringbuf.c
 * @brief P2 公开字节环形缓冲区单元测试。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_ringbuf.h>

#include <string.h>

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static int expect_bytes(const uint8_t *actual, const char *expected, size_t size)
{
    return memcmp(actual, expected, size) == 0 ? 0 : 1;
}

int main(void)
{
    uint8_t storage[5];
    uint8_t out[8];
    ns_ringbuf_t ringbuf;
    ns_ringbuf_t invalid = { 0 };

    if(expect_true(ns_ringbuf_init(&ringbuf, storage, sizeof(storage)) == NS_OK) != 0) return 1;
    if(expect_true(ns_ringbuf_capacity(&ringbuf) == 5u) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&ringbuf) == 0u) != 0) return 1;

    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"ABC", 3u) == 3u) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&ringbuf) == 3u) != 0) return 1;
    if(expect_true(ns_ringbuf_peek(&ringbuf, 1u, out, 2u) == 2u) != 0) return 1;
    if(expect_bytes(out, "BC", 2u) != 0) return 1;

    if(expect_true(ns_ringbuf_read(&ringbuf, out, 2u) == 2u) != 0) return 1;
    if(expect_bytes(out, "AB", 2u) != 0) return 1;
    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"DEFG", 4u) == 4u) != 0) return 1;
    if(expect_true(ns_ringbuf_free_size(&ringbuf) == 0u) != 0) return 1;

    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"Z", 1u) == 0u) != 0) return 1;
    if(expect_true(ns_ringbuf_read(&ringbuf, out, sizeof(out)) == 5u) != 0) return 1;
    if(expect_bytes(out, "CDEFG", 5u) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&ringbuf) == 0u) != 0) return 1;

    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"12345", 5u) == 5u) != 0) return 1;
    if(expect_true(ns_ringbuf_skip(&ringbuf, 3u) == 3u) != 0) return 1;
    if(expect_true(ns_ringbuf_read(&ringbuf, out, 4u) == 2u) != 0) return 1;
    if(expect_bytes(out, "45", 2u) != 0) return 1;
    ns_ringbuf_clear(&ringbuf);
    if(expect_true(ns_ringbuf_size(&ringbuf) == 0u) != 0) return 1;

    if(expect_true(ns_ringbuf_init(NULL, storage, sizeof(storage)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_init(&invalid, NULL, sizeof(storage)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_init(&invalid, storage, 0u) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_capacity(NULL) == 0u) != 0) return 1;
    if(expect_true(ns_ringbuf_capacity(&invalid) == 0u) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&invalid) == 0u) != 0) return 1;
    if(expect_true(ns_ringbuf_free_size(&invalid) == 0u) != 0) return 1;
    if(expect_true(ns_ringbuf_write(&invalid, storage, 1u) == 0u) != 0) return 1;
    if(expect_true(ns_ringbuf_read(&invalid, out, 1u) == 0u) != 0) return 1;
    if(expect_true(ns_ringbuf_peek(&invalid, 0u, out, 1u) == 0u) != 0) return 1;
    if(expect_true(ns_ringbuf_skip(&invalid, 1u) == 0u) != 0) return 1;
    ns_ringbuf_clear(&invalid);

    return 0;
}
