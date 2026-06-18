/**
 * @file test_ds_hashtable.c
 * @brief 公开字符串键哈希表单元测试。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_hashtable.h>

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    ns_slist_t buckets[4];
    ns_hashtable_t table;
    ns_hashtable_t invalid = { 0 };
    ns_hashtable_node_t alpha;
    ns_hashtable_node_t beta;
    ns_hashtable_node_t gamma;
    ns_hashtable_node_t duplicate;
    ns_hashtable_node_t null_key;
    int one = 1;
    int two = 2;
    int three = 3;
    int four = 4;
    ns_hashtable_node_t *node;

    if(expect_true(ns_hashtable_init(NULL, buckets, 4u) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_hashtable_init(&table, NULL, 4u) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_hashtable_init(&table, buckets, 0u) == NS_E_INVAL) != 0) return 1;

    ns_hashtable_node_init(NULL, "ignored", &one);
    ns_hashtable_node_init(&null_key, NULL, &one);
    if(expect_true(ns_hashtable_insert(&invalid, &null_key) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_hashtable_find(&invalid, "missing") == NULL) != 0) return 1;
    if(expect_true(ns_hashtable_remove(&invalid, "missing") == NULL) != 0) return 1;
    ns_hashtable_clear(&invalid);

    if(expect_true(ns_hashtable_init(&table, buckets, 4u) == NS_OK) != 0) return 1;
    if(expect_true(ns_hashtable_insert(&table, &null_key) == NS_E_INVAL) != 0) return 1;

    ns_hashtable_node_init(&alpha, "alpha", &one);
    ns_hashtable_node_init(&beta, "beta", &two);
    ns_hashtable_node_init(&gamma, "gamma", &three);
    ns_hashtable_node_init(&duplicate, "alpha", &four);

    if(expect_true(ns_hashtable_insert(&table, &alpha) == NS_OK) != 0) return 1;
    if(expect_true(ns_hashtable_insert(&table, &beta) == NS_OK) != 0) return 1;
    if(expect_true(ns_hashtable_insert(&table, &gamma) == NS_OK) != 0) return 1;
    if(expect_true(table.size == 3u) != 0) return 1;
    if(expect_true(ns_hashtable_insert(&table, &duplicate) == NS_E_EXISTS) != 0) return 1;

    node = ns_hashtable_find(&table, "beta");
    if(expect_true(node == &beta) != 0) return 1;
    if(expect_true(*(int *)node->value == 2) != 0) return 1;
    if(expect_true(ns_hashtable_find(&table, "missing") == NULL) != 0) return 1;

    node = ns_hashtable_remove(&table, "beta");
    if(expect_true(node == &beta) != 0) return 1;
    if(expect_true(table.size == 2u) != 0) return 1;
    if(expect_true(ns_hashtable_find(&table, "beta") == NULL) != 0) return 1;

    ns_hashtable_clear(&table);
    if(expect_true(table.size == 0u) != 0) return 1;
    if(expect_true(ns_hashtable_find(&table, "alpha") == NULL) != 0) return 1;

    return 0;
}
