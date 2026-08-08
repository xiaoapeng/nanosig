/**
 * @file nanosig_hashtbl.h
 * @brief nanosig 哈希表（对齐 eventhub_os eh_hashtbl）。
 * @date 2026-08-08
 *
 * 从 eventhub_os 的 eh_hashtbl 移植，命名空间映射为 ns_hashtbl_*。
 * 库管理内存：create/destroy 分配/释放表与节点，节点 KV 内联。
 *
 * @warning 本数据结构不是线程安全的。同一数据结构上的并发操作需要外部同步。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_HASHTBL_H
#define NANOSIG_HASHTBL_H

#include <nanosig/nanosig_list.h>
#include <nanosig/nanosig_status.h>
#include <nanosig/nanosig_types.h>

#include <stdint.h>
#include <string.h>   /* 遍历宏调用点展开需要的 strncmp/memcmp */

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t ns_hashtbl_kv_len_t;
typedef int *ns_hashtbl_t;
typedef uint32_t ns_hash_val_t;

#define NS_HASHTBL_DEFAULT_LOADFACTOR   0.75
#define NS_HASHTBL_KV_ALIGN             sizeof(unsigned long)

#ifndef NS_CONFIG_HASHTBL_MIN_SIZE
#define NS_CONFIG_HASHTBL_MIN_SIZE      16
#endif

/**
 * @brief 哈希表节点，KV 内联存储在 kv[0] 之后。
 */
struct ns_hashtbl_node {
    struct ns_list_node                     node;
    ns_hash_val_t                           hash_val;
    ns_hashtbl_kv_len_t                     value_len;
    ns_hashtbl_kv_len_t                     key_len;
    uint8_t NS_ALIGNED(NS_HASHTBL_KV_ALIGN) kv[0];
};

/**
 * @brief 哈希表内部结构（不透明）。
 */
struct ns_hashtbl {
    struct ns_list_node                    *table;      /* 散列表 */
    unsigned int                            mask;       /* 散列表大小减 1，大小为 2 的次幂 */
    unsigned int                            threshold;  /* 阈值，达到时自动扩容 */
    unsigned int                            count;      /* 元素个数 */
};

extern struct ns_list_node *_ns_hashtbl_find_list_head(
    ns_hashtbl_t hashtbl, const void *key, ns_hashtbl_kv_len_t key_len);
extern struct ns_list_node *_ns_hashtbl_find_list_head_with_string(
    ns_hashtbl_t hashtbl, const char *key_str);

/* 判断哈希表节点是否需要重建 */
#define ns_hash_table_node_is_need_remake(hashtbl, idx)  ((hashtbl)->table[idx].next == NULL)

/**
 * @brief 创建哈希表。
 *
 * @param load_factor 负载因子；0.0f 及以下或 NaN 返回 NULL，超过 1.0f 时钳制到 1.0f。
 * @return 成功返回哈希表句柄，失败返回 NULL（与 ns_platform_alloc 的 NULL 失败约定一致）。
 */
extern ns_hashtbl_t ns_hashtbl_create(float load_factor);

/**
 * @brief 销毁哈希表并释放所有节点和表内存。
 *
 * @note 库拥有节点内存，destroy 会释放所有仍挂在表上的节点。调用方不得再
 *       对同一节点调用 ns_hashtbl_node_delete，否则双重释放。
 * @param hashtbl 哈希表句柄。
 */
extern void ns_hashtbl_destroy(ns_hashtbl_t hashtbl);

/**
 * @brief 创建哈希表节点。
 *
 * 调用后需手动赋值 key，然后调用 ns_hashtbl_node_key_refresh，最后插入。
 *
 * @param key_len   键长度。
 * @param value_len 值长度。
 * @return 成功返回节点句柄，失败返回 NULL。
 */
extern struct ns_hashtbl_node *ns_hashtbl_node_new(
    ns_hashtbl_kv_len_t key_len, ns_hashtbl_kv_len_t value_len);

/**
 * @brief 创建哈希表节点并自动刷新内部哈希值。
 *
 * @param hashtbl   哈希表句柄。
 * @param key       键。
 * @param key_len   键长度。
 * @param value_len 值长度。
 * @return 成功返回节点句柄，失败返回 NULL。
 */
extern struct ns_hashtbl_node *ns_hashtbl_node_new_refresh(
    ns_hashtbl_t hashtbl, const void *key, ns_hashtbl_kv_len_t key_len,
    ns_hashtbl_kv_len_t value_len);

/**
 * @brief 创建哈希表节点（字符串键）并自动刷新内部哈希值。
 *
 * @param hashtbl   哈希表句柄。
 * @param key       字符串键；长度限制 65535 字节（ns_hashtbl_kv_len_t 为 uint16_t）。
 * @param value_len 值长度。
 * @return 成功返回节点句柄，失败返回 NULL。
 */
extern struct ns_hashtbl_node *ns_hashtbl_node_new_with_string_refresh(
    ns_hashtbl_t hashtbl, const char *key, ns_hashtbl_kv_len_t value_len);

/**
 * @brief 重建哈希表节点（值长度变化时重新分配）。
 *
 * 如果新值长度与旧值相同，直接返回旧节点。旧节点若挂在表上会自动被替换，
 * 无需手动释放旧节点。
 *
 * @param hashtbl      哈希表句柄。
 * @param old_node     旧节点（会被自动释放）。
 * @param value_len    新值长度。
 * @return 成功返回新节点句柄，失败返回 NULL。
 */
extern struct ns_hashtbl_node *ns_hashtbl_node_renew(
    ns_hashtbl_t hashtbl, struct ns_hashtbl_node *old_node,
    ns_hashtbl_kv_len_t value_len);

/**
 * @brief 刷新节点的键，在强制修改键值后必须调用。
 *
 * @param hashtbl 哈希表句柄。
 * @param node    节点句柄。
 */
extern void ns_hashtbl_node_key_refresh(
    ns_hashtbl_t hashtbl, struct ns_hashtbl_node *node);

/**
 * @brief 删除节点并释放内存；若节点挂在表上会自动摘除。
 *
 * @param hashtbl 哈希表句柄。
 * @param node    节点句柄。
 */
extern void ns_hashtbl_node_delete(
    ns_hashtbl_t hashtbl, struct ns_hashtbl_node *node);

/**
 * @brief 获取节点键（只读）。
 */
#define ns_hashtbl_node_const_key(node)    ((const void *)((node)->kv))

/**
 * @brief 获取节点键；修改键后必须调用 ns_hashtbl_node_key_refresh。
 */
#define ns_hashtbl_node_key(node)          ((void *)((node)->kv))

/**
 * @brief 获取节点值。
 */
#define ns_hashtbl_node_value(node) \
    ((void *)((node)->kv + ns_align_up((node)->key_len, NS_HASHTBL_KV_ALIGN)))

/**
 * @brief 获取节点键长度。
 */
#define ns_hashtbl_node_key_len(node)      ((node)->key_len)

/**
 * @brief 获取节点值长度。
 */
#define ns_hashtbl_node_value_len(node)    ((node)->value_len)

/**
 * @brief 判断节点是否已插入哈希表。
 *
 * @note 不变量：已脱链但未重新 init 的节点（ns_list_remove 置空 next/prev）
 *       会被误判为已插入。任何 is_insert 检查前，脱链节点必须已
 *       ns_list_remove_init 重新初始化。
 */
#define ns_hashtbl_node_is_insert(_node)    (!ns_list_empty(&(_node)->node))

/**
 * @brief 向哈希表插入节点。
 *
 * @param hashtbl 哈希表句柄。
 * @param node    节点句柄。
 * @return NS_OK 成功；同一节点已插入返回 NS_E_EXISTS；OOM 返回 NS_E_NOMEM。
 *         （不同节点相同键允许共存。）
 */
extern int ns_hashtbl_insert(ns_hashtbl_t hashtbl, struct ns_hashtbl_node *node);

/**
 * @brief 从哈希表移除节点。
 *
 * @param hashtbl 哈希表句柄。
 * @param node    节点句柄。
 */
extern void ns_hashtbl_remove(ns_hashtbl_t hashtbl, struct ns_hashtbl_node *node);

/**
 * @brief 从哈希表寻找节点（二进制键）。
 *
 * @param hashtbl  哈希表句柄。
 * @param key      键。
 * @param key_len  键长度。
 * @param out_node 输出节点；可传 NULL。
 * @return NS_OK 找到；NS_E_EMPTY 未找到。
 */
extern int ns_hashtbl_find(
    ns_hashtbl_t hashtbl, const void *key, ns_hashtbl_kv_len_t key_len,
    struct ns_hashtbl_node **out_node);

/**
 * @brief 从哈希表寻找节点（字符串键）。
 *
 * @param hashtbl  哈希表句柄。
 * @param key_str  键字符串。
 * @param out_node 输出节点；可传 NULL（不会崩溃）。
 * @return NS_OK 找到；NS_E_EMPTY 未找到。
 */
extern int ns_hashtbl_find_with_string(
    ns_hashtbl_t hashtbl, const char *key_str, struct ns_hashtbl_node **out_node);

/**
 * @brief 计算字符串哈希值（FNV-1a）。
 *
 * 兼容导出，与旧 ns_hashtable 的 ns_hash_string 语义一致：NULL 返回 0。
 *
 * @param key 字符串；NULL 返回 0。
 * @return 字符串哈希值。
 */
extern uint32_t ns_hash_string(const char *key);

/* ---- 遍历宏 ---- */

/**
 * @brief 遍历哈希表全部节点。
 *
 * @note 使用表达式式静态断言做编译期类型检查（NS_STATIC_ASSERT_EXPR），
 *       C11 与 C++ 均可用。
 *
 * @param hashtbl    哈希表句柄。
 * @param node_pos   节点位置变量。
 * @param node_tmp_n 临时变量。
 * @param tmp_uint_i 临时变量（unsigned int）。
 */
#define ns_hashtbl_for_each_safe(hashtbl, node_pos, node_tmp_n, tmp_uint_i)              \
    for((tmp_uint_i) = 0, NS_STATIC_ASSERT_EXPR(                                          \
            ns_same_type(hashtbl, ns_hashtbl_t),                                          \
            "hashtbl must be ns_hashtbl_t");                                              \
        (tmp_uint_i) <= ((struct ns_hashtbl *)(hashtbl))->mask;                           \
        (tmp_uint_i)++)                                                                   \
        if(!ns_hash_table_node_is_need_remake(((struct ns_hashtbl *)(hashtbl)), (tmp_uint_i))) \
            ns_list_for_each_entry_safe(node_pos, node_tmp_n,                             \
                ((struct ns_hashtbl *)(hashtbl))->table + (tmp_uint_i), node)

/**
 * @brief 遍历哈希表，键为字符串（仅匹配键匹配的节点）。
 *
 * @param hashtbl       哈希表句柄。
 * @param string        字符串。
 * @param node_pos      节点位置变量。
 * @param node_tmp_n    临时变量。
 * @param list_tmp_head 临时变量（ns_list_node_t *）。
 */
#define ns_hashtbl_for_each_with_string_safe(hashtbl, string, node_pos, node_tmp_n, list_tmp_head)  \
    for (list_tmp_head = _ns_hashtbl_find_list_head_with_string(hashtbl, string),                   \
        node_pos = ns_list_entry((list_tmp_head)->next, typeof(*node_pos), node),                   \
        node_tmp_n = ns_list_entry(node_pos->node.next, typeof(*node_pos), node);                   \
        &node_pos->node != (list_tmp_head);                                                         \
        node_pos = node_tmp_n, node_tmp_n = ns_list_entry(node_tmp_n->node.next, typeof(*node_tmp_n), node)) \
        if(strncmp((const char *)ns_hashtbl_node_const_key(node_pos), string, ns_hashtbl_node_key_len(node_pos)) == 0)

/**
 * @brief 遍历哈希表，键为二进制（仅匹配键匹配的节点）。
 *
 * @param hashtbl       哈希表句柄。
 * @param key           键。
 * @param len           键长度。
 * @param node_pos      节点位置变量。
 * @param node_tmp_n    临时变量。
 * @param list_tmp_head 临时变量（ns_list_node_t *）。
 */
#define ns_hashtbl_for_each_with_key_safe(hashtbl, key, len, node_pos, node_tmp_n, list_tmp_head)   \
    for (list_tmp_head = _ns_hashtbl_find_list_head(hashtbl, key, len),                             \
        node_pos = ns_list_entry((list_tmp_head)->next, typeof(*node_pos), node),                   \
        node_tmp_n = ns_list_entry(node_pos->node.next, typeof(*node_pos), node);                   \
        &node_pos->node != (list_tmp_head);                                                         \
        node_pos = node_tmp_n, node_tmp_n = ns_list_entry(node_tmp_n->node.next, typeof(*node_tmp_n), node)) \
        if(len == ns_hashtbl_node_key_len(node_pos) && memcmp(ns_hashtbl_node_const_key(node_pos), key, len) == 0)

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_HASHTBL_H */
