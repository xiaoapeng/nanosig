/**
 * @file test_ds_hashtable.c
 * @brief 哈希表单元测试（对齐 eventhub_os eh_hashtbl 的 ns_hashtbl_* API）。
 * @date 2026-08-08
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 *
 * 覆盖：create/destroy 生命周期、节点工厂、插入/查找/删除、
 * 二进制键/字符串键、自动扩容 + 渐进式重建、三种遍历宏、
 * node_key_refresh / node_renew、错误路径与两个 API 偏差。
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <nanosig/nanosig_hashtbl.h>

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

/* 创建/销毁生命周期；create 失败契约：load_factor<=0 或 NaN 返回 NULL */
static int test_create_destroy(void)
{
    ns_hashtbl_t tbl;

    if(ns_hashtbl_create(0.0f) != NULL) return 1;
    if(ns_hashtbl_create(-1.0f) != NULL) return 1;
    if(ns_hashtbl_create(NAN) != NULL) return 1;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(expect_true(tbl != NULL) != 0) return 1;
    ns_hashtbl_destroy(tbl);

    /* 自定义负载因子 */
    tbl = ns_hashtbl_create(0.5f);
    if(expect_true(tbl != NULL) != 0) return 1;
    ns_hashtbl_destroy(tbl);

    /* 负载因子超 1.0f 被钳制，仍能正常创建 */
    tbl = ns_hashtbl_create(1.5f);
    if(expect_true(tbl != NULL) != 0) return 1;
    ns_hashtbl_destroy(tbl);

    return 0;
}

/* 节点工厂：node_new / node_new_refresh / node_new_with_string_refresh */
static int test_node_factory(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node;
    uint32_t key = 0x12345678u;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    /* node_new：手动写键后必须 key_refresh 再插入 */
    node = ns_hashtbl_node_new(4, 4);
    if(expect_true(node != NULL) != 0) return 1;
    if(expect_true(ns_hashtbl_node_is_insert(node) == 0) != 0) return 1;
    if(expect_true(ns_hashtbl_node_key_len(node) == 4) != 0) return 1;
    if(expect_true(ns_hashtbl_node_value_len(node) == 4) != 0) return 1;
    *(uint32_t *)ns_hashtbl_node_key(node) = key;
    *(uint32_t *)ns_hashtbl_node_value(node) = 0xAAu;
    ns_hashtbl_node_key_refresh(tbl, node);
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    if(expect_true(ns_hashtbl_node_is_insert(node) != 0) != 0) return 1;
    {
        struct ns_hashtbl_node *out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &key, 4, &out) == NS_OK) != 0) return 1;
        if(expect_true(out == node) != 0) return 1;
        if(expect_true(*(uint32_t *)ns_hashtbl_node_value(out) == 0xAAu) != 0) return 1;
    }
    ns_hashtbl_node_delete(tbl, node);

    /* node_new_refresh：自动刷新哈希值并拷贝键 */
    node = ns_hashtbl_node_new_refresh(tbl, &key, 4, 8);
    if(expect_true(node != NULL) != 0) return 1;
    if(expect_true(ns_hashtbl_node_key_len(node) == 4) != 0) return 1;
    if(expect_true(ns_hashtbl_node_value_len(node) == 8) != 0) return 1;
    if(expect_true(memcmp(ns_hashtbl_node_const_key(node), &key, 4) == 0) != 0) return 1;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    ns_hashtbl_node_delete(tbl, node);

    /* node_new_with_string_refresh：字符串键（key_len 不含结尾 NUL） */
    node = ns_hashtbl_node_new_with_string_refresh(tbl, "hello", 4);
    if(expect_true(node != NULL) != 0) return 1;
    if(expect_true(ns_hashtbl_node_key_len(node) == 5) != 0) return 1;
    if(expect_true(memcmp(ns_hashtbl_node_const_key(node), "hello", 5) == 0) != 0) return 1;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    {
        struct ns_hashtbl_node *out = NULL;
        if(expect_true(ns_hashtbl_find_with_string(tbl, "hello", &out) == NS_OK) != 0) return 1;
        if(expect_true(out == node) != 0) return 1;
    }
    ns_hashtbl_node_delete(tbl, node);

    /* node_delete：未插入节点直接释放，不影响表 */
    node = ns_hashtbl_node_new(4, 4);
    if(expect_true(node != NULL) != 0) return 1;
    ns_hashtbl_node_delete(tbl, node);

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* 插入 / 查找 / 移除 基本 CRUD */
static int test_insert_find_remove(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node;
    struct ns_hashtbl_node *out = NULL;
    uint32_t key_a = 0x1111u, key_b = 0x2222u, key_c = 0x3333u;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    node = ns_hashtbl_node_new_refresh(tbl, &key_a, 4, 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 100u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    node = ns_hashtbl_node_new_refresh(tbl, &key_b, 4, 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 200u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    node = ns_hashtbl_node_new_refresh(tbl, &key_c, 4, 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 300u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    /* 命中：NS_OK + out_node */
    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, &key_b, 4, &out) == NS_OK) != 0) return 1;
    if(expect_true(out != NULL) != 0) return 1;
    if(expect_true(*(uint32_t *)ns_hashtbl_node_value(out) == 200u) != 0) return 1;

    /* 未命中：NS_E_EMPTY */
    {
        uint32_t missing = 0xFFFFu;
        out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &missing, 4, &out) == NS_E_EMPTY) != 0) return 1;
    }

    /* remove：脱链后节点可再次插入，由调用方决定是否释放 */
    {
        struct ns_hashtbl_node *found = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &key_b, 4, &found) == NS_OK) != 0) return 1;
        ns_hashtbl_remove(tbl, found);
        if(expect_true(ns_hashtbl_node_is_insert(found) == 0) != 0) return 1;
        out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &key_b, 4, &out) == NS_E_EMPTY) != 0) return 1;
        /* 重新插入同一节点（remove 已重新初始化节点） */
        if(expect_true(ns_hashtbl_insert(tbl, found) == NS_OK) != 0) return 1;
        ns_hashtbl_node_delete(tbl, found);
    }

    /* 其余节点仍可找到 */
    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, &key_a, 4, &out) == NS_OK) != 0) return 1;
    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, &key_c, 4, &out) == NS_OK) != 0) return 1;

    /* 剩余节点由 destroy 统一释放 */
    ns_hashtbl_destroy(tbl);
    return 0;
}

/* 二进制键：含 0 字节的键必须按长度精确匹配（memcmp 语义） */
static int test_binary_key(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node;
    struct ns_hashtbl_node *out = NULL;
    const uint8_t key1[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03 };
    const uint8_t key2[8] = { 0x00, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF };
    const uint8_t missing[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    node = ns_hashtbl_node_new_refresh(tbl, key1, 8, 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 1u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    node = ns_hashtbl_node_new_refresh(tbl, key2, 8, 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 2u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, key1, 8, &out) == NS_OK) != 0) return 1;
    if(expect_true(*(uint32_t *)ns_hashtbl_node_value(out) == 1u) != 0) return 1;
    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, key2, 8, &out) == NS_OK) != 0) return 1;
    if(expect_true(*(uint32_t *)ns_hashtbl_node_value(out) == 2u) != 0) return 1;

    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, missing, 8, &out) == NS_E_EMPTY) != 0) return 1;

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* 字符串键：find_with_string 命中 / 未命中；ns_hash_string 可链接 */
static int test_string_key(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node;
    struct ns_hashtbl_node *out = NULL;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    node = ns_hashtbl_node_new_with_string_refresh(tbl, "alpha", 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 10u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    node = ns_hashtbl_node_new_with_string_refresh(tbl, "beta", 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 20u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    out = NULL;
    if(expect_true(ns_hashtbl_find_with_string(tbl, "alpha", &out) == NS_OK) != 0) return 1;
    if(expect_true(*(uint32_t *)ns_hashtbl_node_value(out) == 10u) != 0) return 1;

    out = NULL;
    if(expect_true(ns_hashtbl_find_with_string(tbl, "gamma", &out) == NS_E_EMPTY) != 0) return 1;

    /* ns_hash_string 非悬空 extern：静态库下缺失定义仅在引用时暴露 */
    if(expect_true(ns_hash_string("alpha") != 0u) != 0) return 1;
    if(expect_true(ns_hash_string("alpha") == ns_hash_string("alpha")) != 0) return 1;

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* 自动扩容：插入超过阈值触发 resize，扩容后查找仍精确 */
static int test_auto_resize(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *out = NULL;
    unsigned int i;

    /* 默认 MIN_SIZE=16、阈值=12，插入 200 个键必然多次触发扩容 */
    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    for(i = 0u; i < 100u; i++){
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_refresh(tbl, &i, 4, 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = i;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    for(i = 0u; i < 100u; i++){
        out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &i, 4, &out) == NS_OK) != 0) return 1;
        if(expect_true(*(uint32_t *)ns_hashtbl_node_value(out) == i) != 0) return 1;
    }

    /* 扩容后再插入 + 再查找：同时覆盖 insert 与 find 路径的重建 */
    for(i = 100u; i < 200u; i++){
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_refresh(tbl, &i, 4, 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = i;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }
    for(i = 0u; i < 200u; i++){
        out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &i, 4, &out) == NS_OK) != 0) return 1;
        if(expect_true(*(uint32_t *)ns_hashtbl_node_value(out) == i) != 0) return 1;
    }

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* 极小 load_factor(HASHTBL-103)：threshold 钳制到 >= 1，
 * 插入 100 个节点后桶数有界（修复前每次 insert 都 resize，mask 会涨到 UINT32_MAX） */
static int test_tiny_load_factor_bounded(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl *h;
    unsigned int i;
    uint32_t key;

    tbl = ns_hashtbl_create(0.01f);
    if(tbl == NULL) return 1;
    h = (struct ns_hashtbl *)tbl;

    for(i = 0u; i < 100u; i++){
        struct ns_hashtbl_node *node;
        key = i;
        node = ns_hashtbl_node_new_refresh(tbl, &key, 4, 4);
        if(node == NULL) return 1;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    /* 修复后 threshold 1→2→4→...→64→128，100 次插入 resize 约 8 次，mask ≈ 2047；
     * 修复前每次 insert 都 resize，mask 到 UINT32_MAX */
    if(expect_true(h->mask < 4096u) != 0) return 1;

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* try_remake 渐进式重建：扩容后新桶被标记为待重建，查找/插入触发重建 */
static int test_try_remake(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *out = NULL;
    struct ns_hashtbl_node *node_pos, *node_tmp_n;
    unsigned int tmp_uint_i;
    int count;
    unsigned int i;

    /* 阈值 12：插入 12 个键后首次扩容（size 16→32），后一半桶待重建 */
    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    for(i = 0u; i < 12u; i++){
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_refresh(tbl, &i, 4, 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = i;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    /* 扩容后再插入 20 个键：落在待重建桶的插入会触发 try_remake */
    for(i = 12u; i < 32u; i++){
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_refresh(tbl, &i, 4, 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = i;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    /* 逐个 find 全部 32 个键：find 路径同样触发 try_remake */
    for(i = 0u; i < 32u; i++){
        out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &i, 4, &out) == NS_OK) != 0) return 1;
        if(expect_true(*(uint32_t *)ns_hashtbl_node_value(out) == i) != 0) return 1;
    }

    /* 重建不丢节点、不重复节点：遍历计数 == 插入总数 */
    count = 0;
    ns_hashtbl_for_each_safe(tbl, node_pos, node_tmp_n, tmp_uint_i){
        if(node_pos == NULL) return 1;
        count++;
    }
    if(expect_true(count == 32) != 0) return 1;

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* for_each_safe：全表遍历 + 遍历期间删除当前节点 */
static int test_for_each_safe(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node_pos, *node_tmp_n;
    unsigned int tmp_uint_i;
    int count;
    unsigned int i;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    for(i = 1u; i <= 5u; i++){
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_refresh(tbl, &i, 4, 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = i * 10u;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    count = 0;
    ns_hashtbl_for_each_safe(tbl, node_pos, node_tmp_n, tmp_uint_i){
        if(node_pos == NULL) return 1;
        count++;
    }
    if(expect_true(count == 5) != 0) return 1;

    /* 遍历期间删除偶数键节点：for_each_safe 允许删除当前节点 */
    count = 0;
    ns_hashtbl_for_each_safe(tbl, node_pos, node_tmp_n, tmp_uint_i){
        uint32_t k = *(uint32_t *)ns_hashtbl_node_const_key(node_pos);
        if((k & 1u) == 0u){
            ns_hashtbl_node_delete(tbl, node_pos);
        }
        count++;
    }
    if(expect_true(count == 5) != 0) return 1;

    count = 0;
    ns_hashtbl_for_each_safe(tbl, node_pos, node_tmp_n, tmp_uint_i){
        count++;
    }
    if(expect_true(count == 3) != 0) return 1;

    /* 被删节点不可再找到，存活节点仍可找到 */
    for(i = 2u; i <= 4u; i += 2u){
        struct ns_hashtbl_node *out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &i, 4, &out) == NS_E_EMPTY) != 0) return 1;
    }
    for(i = 1u; i <= 5u; i += 2u){
        struct ns_hashtbl_node *out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &i, 4, &out) == NS_OK) != 0) return 1;
    }

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* for_each_with_string_safe：仅遍历字符串键匹配的节点 */
static int test_for_each_with_string_safe(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node_pos, *node_tmp_n;
    ns_list_node_t *list_tmp_head;
    int count;
    unsigned int i;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    /* 4 个相同字符串键的节点（不同节点相同键允许共存）+ 1 个不同键 */
    for(i = 1u; i <= 4u; i++){
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_with_string_refresh(tbl, "abcdef", 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = i;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }
    {
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_with_string_refresh(tbl, "other", 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = 99u;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    count = 0;
    ns_hashtbl_for_each_with_string_safe(tbl, "abcdef", node_pos, node_tmp_n, list_tmp_head){
        count++;
    }
    if(expect_true(count == 4) != 0) return 1;

    /* 匹配节点的键内容与长度正确 */
    ns_hashtbl_for_each_with_string_safe(tbl, "abcdef", node_pos, node_tmp_n, list_tmp_head){
        if(expect_true(ns_hashtbl_node_key_len(node_pos) == 6) != 0) return 1;
        if(expect_true(memcmp(ns_hashtbl_node_const_key(node_pos), "abcdef", 6) == 0) != 0) return 1;
    }

    /* 不存在的键 → 遍历 0 个节点 */
    count = 0;
    ns_hashtbl_for_each_with_string_safe(tbl, "zzzz", node_pos, node_tmp_n, list_tmp_head){
        count++;
    }
    if(expect_true(count == 0) != 0) return 1;

    /* 前缀假阳性回归：键 "aa" 与搜索串 "aaz" 的 FNV-1a 哈希同桶（各掩码下均同桶）。
     * 修复前 strncmp("aa","aaz",2)==0 会把前缀键 "aa" 误当匹配带出；
     * 精确匹配（长度短路 + memcmp）后只出精确键 "aaz"。 */
    {
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_with_string_refresh(tbl, "aa", 2);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = 100u;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
        node = ns_hashtbl_node_new_with_string_refresh(tbl, "aaz", 3);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = 101u;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    /* 搜 "aaz"：不得遍历出前缀键 "aa"，只出精确键 "aaz" */
    count = 0;
    ns_hashtbl_for_each_with_string_safe(tbl, "aaz", node_pos, node_tmp_n, list_tmp_head){
        count++;
        if(expect_true(ns_hashtbl_node_key_len(node_pos) == 3) != 0) return 1;
        if(expect_true(memcmp(ns_hashtbl_node_const_key(node_pos), "aaz", 3) == 0) != 0) return 1;
    }
    if(expect_true(count == 1) != 0) return 1;

    /* 反向：搜 "aa" 只出 "aa"，不出 "aaz" */
    count = 0;
    ns_hashtbl_for_each_with_string_safe(tbl, "aa", node_pos, node_tmp_n, list_tmp_head){
        count++;
        if(expect_true(ns_hashtbl_node_key_len(node_pos) == 2) != 0) return 1;
        if(expect_true(memcmp(ns_hashtbl_node_const_key(node_pos), "aa", 2) == 0) != 0) return 1;
    }
    if(expect_true(count == 1) != 0) return 1;

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* for_each_with_key_safe：仅遍历二进制键匹配的节点（含遍历中删除） */
static int test_for_each_with_key_safe(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node_pos, *node_tmp_n;
    ns_list_node_t *list_tmp_head;
    uint32_t k = 0x11223344u;
    int count;
    unsigned int i;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    /* 4 个相同二进制键的节点 + 1 个不同键 */
    for(i = 1u; i <= 4u; i++){
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_refresh(tbl, &k, 4, 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = i * 100u;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }
    {
        struct ns_hashtbl_node *node;
        uint32_t other = 0xDEADBEEFu;
        node = ns_hashtbl_node_new_refresh(tbl, &other, 4, 4);
        if(node == NULL) return 1;
        *(uint32_t *)ns_hashtbl_node_value(node) = 999u;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    count = 0;
    ns_hashtbl_for_each_with_key_safe(tbl, &k, 4, node_pos, node_tmp_n, list_tmp_head){
        count++;
    }
    if(expect_true(count == 4) != 0) return 1;

    /* 遍历期间删除当前节点（safe） */
    count = 0;
    ns_hashtbl_for_each_with_key_safe(tbl, &k, 4, node_pos, node_tmp_n, list_tmp_head){
        if(count == 0){
            ns_hashtbl_node_delete(tbl, node_pos);
        }
        count++;
    }
    if(expect_true(count == 4) != 0) return 1;

    count = 0;
    ns_hashtbl_for_each_with_key_safe(tbl, &k, 4, node_pos, node_tmp_n, list_tmp_head){
        count++;
    }
    if(expect_true(count == 3) != 0) return 1;

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* node_key_refresh（原地改键后重新入桶）与 node_renew（重建节点） */
static int test_node_key_refresh_renew(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node;
    struct ns_hashtbl_node *out = NULL;
    uint32_t key_a = 0xAABBCCDDu;
    uint32_t key_b = 0x11223344u;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    node = ns_hashtbl_node_new_refresh(tbl, &key_a, 4, 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 7u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, &key_a, 4, &out) == NS_OK) != 0) return 1;
    if(expect_true(out == node) != 0) return 1;

    /* 原地改键 → key_refresh：节点迁移到新桶 */
    *(uint32_t *)ns_hashtbl_node_key(node) = key_b;
    ns_hashtbl_node_key_refresh(tbl, node);

    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, &key_a, 4, &out) == NS_E_EMPTY) != 0) return 1;
    out = NULL;
    if(expect_true(ns_hashtbl_find(tbl, &key_b, 4, &out) == NS_OK) != 0) return 1;
    if(expect_true(out == node) != 0) return 1;

    /* renew：值长度相同 → 返回原节点 */
    {
        struct ns_hashtbl_node *same;
        same = ns_hashtbl_node_renew(tbl, node, 4);
        if(expect_true(same == node) != 0) return 1;
    }

    /* renew：值长度变化 → 重新分配，键保留且仍在表中 */
    {
        struct ns_hashtbl_node *renewed;
        renewed = ns_hashtbl_node_renew(tbl, node, 8);
        if(expect_true(renewed != NULL) != 0) return 1;
        if(expect_true(renewed != node) != 0) return 1;
        if(expect_true(ns_hashtbl_node_key_len(renewed) == 4) != 0) return 1;
        if(expect_true(ns_hashtbl_node_value_len(renewed) == 8) != 0) return 1;
        if(expect_true(*(uint32_t *)ns_hashtbl_node_const_key(renewed) == key_b) != 0) return 1;
        out = NULL;
        if(expect_true(ns_hashtbl_find(tbl, &key_b, 4, &out) == NS_OK) != 0) return 1;
        if(expect_true(out == renewed) != 0) return 1;
    }

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* 错误路径：同一节点二次 insert → NS_E_EXISTS；不同节点相同键允许共存 */
static int test_error_path_exists(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node;
    struct ns_hashtbl_node *node2;
    struct ns_hashtbl_node *node_pos, *node_tmp_n;
    ns_list_node_t *list_tmp_head;
    uint32_t key = 0xCAFEBABEu;
    int count;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    node = ns_hashtbl_node_new_refresh(tbl, &key, 4, 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 1u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    /* 同一节点二次插入 → NS_E_EXISTS */
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_E_EXISTS) != 0) return 1;

    /* 不同节点相同键 → 允许共存（勿断言拒绝） */
    node2 = ns_hashtbl_node_new_refresh(tbl, &key, 4, 4);
    if(node2 == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node2) = 2u;
    if(expect_true(ns_hashtbl_insert(tbl, node2) == NS_OK) != 0) return 1;

    /* 相同键节点数 == 2，证明共存 */
    count = 0;
    ns_hashtbl_for_each_with_key_safe(tbl, &key, 4, node_pos, node_tmp_n, list_tmp_head){
        count++;
    }
    if(expect_true(count == 2) != 0) return 1;

    /* remove 后同一节点可再次插入 */
    ns_hashtbl_remove(tbl, node2);
    if(expect_true(ns_hashtbl_node_is_insert(node2) == 0) != 0) return 1;
    if(expect_true(ns_hashtbl_insert(tbl, node2) == NS_OK) != 0) return 1;

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* find_with_string 的 out_node 可传 NULL：命中与未命中都不崩溃 */
static int test_find_with_string_null_out(void)
{
    ns_hashtbl_t tbl;
    struct ns_hashtbl_node *node;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    node = ns_hashtbl_node_new_with_string_refresh(tbl, "alpha", 4);
    if(node == NULL) return 1;
    *(uint32_t *)ns_hashtbl_node_value(node) = 1u;
    if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;

    if(expect_true(ns_hashtbl_find_with_string(tbl, "alpha", NULL) == NS_OK) != 0) return 1;
    if(expect_true(ns_hashtbl_find_with_string(tbl, "missing", NULL) == NS_E_EMPTY) != 0) return 1;

    /* NULL 键守卫(HASHTBL-102)：工厂返回 NULL、查找返回 NS_E_INVAL，不崩溃 */
    if(expect_true(ns_hashtbl_node_new_with_string_refresh(tbl, NULL, 4) == NULL) != 0) return 1;
    if(expect_true(ns_hashtbl_find_with_string(tbl, NULL, NULL) == NS_E_INVAL) != 0) return 1;

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* destroy 清理：节点不逐个释放，destroy 统一回收（ASAN/LSAN 下验证无泄漏） */
static int test_destroy_cleanup(void)
{
    ns_hashtbl_t tbl;
    unsigned int i;

    tbl = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(tbl == NULL) return 1;

    for(i = 0u; i < 50u; i++){
        struct ns_hashtbl_node *node;
        node = ns_hashtbl_node_new_refresh(tbl, &i, 4, 4);
        if(node == NULL) return 1;
        if(expect_true(ns_hashtbl_insert(tbl, node) == NS_OK) != 0) return 1;
    }

    ns_hashtbl_destroy(tbl);
    return 0;
}

/* ns_hash_string：可链接、稳定、空串非 0、NULL 返回 0 */
static int test_hash_string(void)
{
    uint32_t h1, h2;

    h1 = ns_hash_string("key");
    h2 = ns_hash_string("key");
    if(expect_true(h1 != 0u) != 0) return 1;
    if(expect_true(h1 == h2) != 0) return 1;
    if(expect_true(ns_hash_string("") != 0u) != 0) return 1;
    if(expect_true(ns_hash_string(NULL) == 0u) != 0) return 1;

    return 0;
}

int main(void)
{
    if(test_create_destroy() != 0) return 1;
    if(test_node_factory() != 0) return 1;
    if(test_insert_find_remove() != 0) return 1;
    if(test_binary_key() != 0) return 1;
    if(test_string_key() != 0) return 1;
    if(test_auto_resize() != 0) return 1;
    if(test_tiny_load_factor_bounded() != 0) return 1;
    if(test_try_remake() != 0) return 1;
    if(test_for_each_safe() != 0) return 1;
    if(test_for_each_with_string_safe() != 0) return 1;
    if(test_for_each_with_key_safe() != 0) return 1;
    if(test_node_key_refresh_renew() != 0) return 1;
    if(test_error_path_exists() != 0) return 1;
    if(test_find_with_string_null_out() != 0) return 1;
    if(test_destroy_cleanup() != 0) return 1;
    if(test_hash_string() != 0) return 1;
    return 0;
}
