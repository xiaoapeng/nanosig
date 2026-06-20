/**
 * @file nanosig_hashtable.h
 * @brief nanosig 字符串键 intrusive 哈希表。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_HASHTABLE_H
#define NANOSIG_HASHTABLE_H

#include <nanosig/nanosig_slist.h>
#include <nanosig/nanosig_status.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 字符串键哈希表节点。
 *
 * 节点由调用方持有并嵌入用户结构体中。`key` 字符串生命周期必须长于节点在表中的生命周期。
 */
typedef struct ns_hashtable_node {
    ns_slist_node_t link;
    const char *key;
    void *value;
    uint32_t hash;
} ns_hashtable_node_t;

/**
 * @brief 字符串键哈希表。
 *
 * bucket 数组由调用方提供，表本身不分配内存。
 */
typedef struct ns_hashtable {
    ns_slist_t *buckets;
    size_t bucket_count;
    size_t size;
} ns_hashtable_t;

/**
 * @brief 计算字符串哈希值。
 *
 * @param key 待计算哈希值的字符串。
 * @return 字符串的哈希值。
 */
extern uint32_t ns_hash_string(const char *key);

/**
 * @brief 初始化哈希表。
 *
 * @param table 哈希表对象。
 * @param buckets 调用方提供的 bucket 数组。
 * @param bucket_count bucket 数量，必须大于 0。
 * @return `NS_OK` 表示成功，失败返回负数状态码。
 */
extern int ns_hashtable_init(ns_hashtable_t *table, ns_slist_t *buckets, size_t bucket_count);

/**
 * @brief 初始化哈希表节点。
 *
 * @param node 节点对象。
 * @param key 字符串键；调用方负责保证其生命周期。
 * @param value 用户值指针。
 */
extern void ns_hashtable_node_init(ns_hashtable_node_t *node, const char *key, void *value);

/**
 * @brief 插入节点。
 *
 * 相同 key 已存在时返回 `NS_E_EXISTS`。
 *
 * @param table 哈希表对象。
 * @param node  待插入的节点。
 * @return `NS_OK` 表示成功；相同 key 已存在时返回 `NS_E_EXISTS`。
 */
extern int ns_hashtable_insert(ns_hashtable_t *table, ns_hashtable_node_t *node);

/**
 * @brief 查找指定 key 的节点。
 *
 * @param table 哈希表对象。
 * @param key   待查找的字符串键。
 * @return 找到时返回节点，未找到返回 `NULL`。
 */
extern ns_hashtable_node_t *ns_hashtable_find(const ns_hashtable_t *table, const char *key);

/**
 * @brief 移除指定 key 的节点。
 *
 * @param table 哈希表对象。
 * @param key   待移除节点的字符串键。
 * @return 找到时返回被移除节点，未找到返回 `NULL`。
 */
extern ns_hashtable_node_t *ns_hashtable_remove(ns_hashtable_t *table, const char *key);

/**
 * @brief 清空哈希表。
 *
 * 本函数只脱链节点，不释放用户对象。
 *
 * @param table 哈希表对象。
 */
extern void ns_hashtable_clear(ns_hashtable_t *table);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_HASHTABLE_H */
