/**
 * @file test_ds_ringbuf.c
 * @brief 公开字节环形缓冲区单元测试。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <string.h>

#include <nanosig/nanosig_ringbuf.h>
static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static int expect_bytes(const uint8_t *actual, const char *expected, int32_t size)
{
    return memcmp(actual, expected, (size_t)size) == 0 ? 0 : 1;
}

int main(void)
{
    uint8_t storage[5];
    uint8_t out[8];
    ns_ringbuf_t ringbuf;
    ns_ringbuf_t invalid;
    int32_t len;

    memset(&invalid, 0, sizeof(invalid));

    if(expect_true(ns_ringbuf_init(&ringbuf, storage, (int32_t)sizeof(storage)) == NS_OK) != 0) return 1;
    if(expect_true(ns_ringbuf_total_size(&ringbuf) == 5) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&ringbuf) == 0) != 0) return 1;

    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"ABC", 3) == 3) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&ringbuf) == 3) != 0) return 1;

    /* peek_copy */
    if(expect_true(ns_ringbuf_peek_copy(&ringbuf, 1, out, 2) == 2) != 0) return 1;
    if(expect_bytes(out, "BC", 2) != 0) return 1;

    /* peek 零拷贝 */
    len = 2;
    {
        const uint8_t *ptr = ns_ringbuf_peek(&ringbuf, 1, out, &len);
        if(ptr == NULL) return 1;
        if(expect_bytes(ptr, "BC", len) != 0) return 1;
    }

    if(expect_true(ns_ringbuf_read(&ringbuf, out, 2) == 2) != 0) return 1;
    if(expect_bytes(out, "AB", 2) != 0) return 1;
    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"DEFG", 4) == 4) != 0) return 1;
    if(expect_true(ns_ringbuf_free_size(&ringbuf) == 0) != 0) return 1;

    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"Z", 1) == 0) != 0) return 1;
    if(expect_true(ns_ringbuf_read(&ringbuf, out, 8) == 5) != 0) return 1;
    if(expect_bytes(out, "CDEFG", 5) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&ringbuf) == 0) != 0) return 1;

    /* read_skip */
    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"12345", 5) == 5) != 0) return 1;
    if(expect_true(ns_ringbuf_read_skip(&ringbuf, 3) == 3) != 0) return 1;
    if(expect_true(ns_ringbuf_read(&ringbuf, out, 4) == 2) != 0) return 1;
    if(expect_bytes(out, "45", 2) != 0) return 1;
    ns_ringbuf_clear(&ringbuf);
    if(expect_true(ns_ringbuf_size(&ringbuf) == 0) != 0) return 1;

    /* draft_write + write_skip */
    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"XY", 2) == 2) != 0) return 1;
    ns_ringbuf_clear(&ringbuf);
    if(expect_true(ns_ringbuf_draft_write(&ringbuf, 0, (const uint8_t *)"AB", 2) == 2) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&ringbuf) == 0) != 0) return 1; /* draft 不移动 w */
    if(expect_true(ns_ringbuf_write_skip(&ringbuf, 2) == 2) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&ringbuf) == 2) != 0) return 1;
    if(expect_true(ns_ringbuf_read(&ringbuf, out, 2) == 2) != 0) return 1;
    if(expect_bytes(out, "AB", 2) != 0) return 1;

    /* reset */
    ns_ringbuf_reset(&ringbuf);
    if(expect_true(ns_ringbuf_size(&ringbuf) == 0) != 0) return 1;

    /* 无效输入 */
    if(expect_true(ns_ringbuf_init(NULL, storage, (int32_t)sizeof(storage)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_init(&invalid, NULL, (int32_t)sizeof(storage)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_init(&invalid, storage, 0) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_total_size(NULL) == 0) != 0) return 1;
    if(expect_true(ns_ringbuf_total_size(&invalid) == 0) != 0) return 1;
    if(expect_true(ns_ringbuf_size(&invalid) == 0) != 0) return 1;
    if(expect_true(ns_ringbuf_free_size(&invalid) == 0) != 0) return 1;
    if(expect_true(ns_ringbuf_write(&invalid, storage, 1) == 0) != 0) return 1;
    if(expect_true(ns_ringbuf_read(&invalid, out, 1) == 0) != 0) return 1;
    if(expect_true(ns_ringbuf_peek_copy(&invalid, 0, out, 1) == 0) != 0) return 1;
    if(expect_true(ns_ringbuf_read_skip(&invalid, 1) == 0) != 0) return 1;
    ns_ringbuf_clear(&invalid);

    return 0;
}
