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

static int test_basic_ops(void)
{
    uint8_t storage[5];
    uint8_t out[8];
    ns_ringbuf_t ringbuf;
    uint32_t len;

    if(expect_true(ns_ringbuf_init(&ringbuf, storage, (uint32_t)sizeof(storage)) == NS_OK) != 0) return 1;
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
        if(expect_bytes(ptr, "BC", (int32_t)len) != 0) return 1;
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

    return 0;
}

static int test_invalid_inputs(void)
{
    uint8_t storage[5];
    uint8_t out[8];
    ns_ringbuf_t invalid;

    memset(&invalid, 0, sizeof(invalid));

    if(expect_true(ns_ringbuf_init(NULL, storage, (uint32_t)sizeof(storage)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_init(&invalid, NULL, (uint32_t)sizeof(storage)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_init(&invalid, storage, 0u) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_ringbuf_total_size(NULL) == -1) != 0) return 1;
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

static int test_wrap_paths(void)
{
    uint8_t storage[5];
    uint8_t out[8];
    ns_ringbuf_t ringbuf;
    uint32_t len;

    /* 构造绕回布局：size=5, 先写满, 再读走 3 字节, 再写 3 字节
     * 此时 w=3, r=3, 缓冲区内容: [F,G,H,D,E] 从 r=3 开始是 "DEFGH" */
    if(expect_true(ns_ringbuf_init(&ringbuf, storage, (uint32_t)sizeof(storage)) == NS_OK) != 0) return 1;
    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"ABCDE", 5) == 5) != 0) return 1;
    if(expect_true(ns_ringbuf_read_skip(&ringbuf, 3) == 3) != 0) return 1;
    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"FGH", 3) == 3) != 0) return 1;

    /* peek 绕回路径：rl=3 > read_size_first_max(5-3=2) */
    len = 3;
    {
        const uint8_t *ptr = ns_ringbuf_peek(&ringbuf, 0, out, &len);
        if(ptr == NULL) return 1;
        if(expect_bytes(ptr, "DEF", 3) != 0) return 1;
    }

    /* peek_copy 绕回路径 */
    if(expect_true(ns_ringbuf_peek_copy(&ringbuf, 0, out, 3) == 3) != 0) return 1;
    if(expect_bytes(out, "DEF", 3) != 0) return 1;

    /* peek 数据不足返回 NULL */
    len = 99;
    if(ns_ringbuf_peek(&ringbuf, 0, out, &len) != NULL) return 1;
    if(expect_true(ns_ringbuf_peek_copy(&ringbuf, 0, out, 99) == 0) != 0) return 1;

    /* draft_write 非零 offset */
    ns_ringbuf_clear(&ringbuf);
    /* 写 2 字节 "AB" 到 w=0,1 */
    if(expect_true(ns_ringbuf_write(&ringbuf, (const uint8_t *)"AB", 2) == 2) != 0) return 1;
    /* draft_write offset=1 写入 "Z" 到 w+1=3 处 */
    if(expect_true(ns_ringbuf_draft_write(&ringbuf, 1, (const uint8_t *)"Z", 1) == 1) != 0) return 1;
    /* write_skip 跳过 offset(1) + data(1) = 2 字节，w=4 */
    if(expect_true(ns_ringbuf_write_skip(&ringbuf, 2) == 2) != 0) return 1;
    /* 读 4 字节：pos0='A', pos1='B', pos2=gap, pos3='Z' */
    if(expect_true(ns_ringbuf_read(&ringbuf, out, 4) == 4) != 0) return 1;
    if(out[0] != 'A' || out[1] != 'B' || out[3] != 'Z') return 1;

    return 0;
}

int main(void)
{
    if(test_basic_ops() != 0) return 1;
    if(test_invalid_inputs() != 0) return 1;
    if(test_wrap_paths() != 0) return 1;
    return 0;
}
