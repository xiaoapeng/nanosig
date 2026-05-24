/**
 * @file nanosig_rbtree.h
 * @brief nanosig uint64 key intrusive 红黑树。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_RBTREE_H
#define NANOSIG_RBTREE_H

#include <nanosig/nanosig_types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ns_rbtree_color {
    NS_RBTREE_RED = 0,
    NS_RBTREE_BLACK = 1
} ns_rbtree_color_t;

/**
 * @brief uint64 key 红黑树节点。
 *
 * 节点由调用方持有并嵌入用户结构体中。相同 key 允许重复插入，重复 key 按插入路径排在右侧。
 */
typedef struct ns_rbtree_node {
    struct ns_rbtree_node *parent;
    struct ns_rbtree_node *left;
    struct ns_rbtree_node *right;
    uint64_t key;
    ns_rbtree_color_t color;
} ns_rbtree_node_t;

/**
 * @brief uint64 key 红黑树根。
 */
typedef struct ns_rbtree {
    ns_rbtree_node_t *root;
    ns_rbtree_node_t *leftmost;
    size_t size;
} ns_rbtree_t;

/**
 * @brief 初始化红黑树根。
 */
extern void ns_rbtree_init(ns_rbtree_t *tree);

/**
 * @brief 初始化红黑树节点。
 */
extern void ns_rbtree_node_init(ns_rbtree_node_t *node, uint64_t key);

/**
 * @brief 判断红黑树是否为空。
 */
extern int ns_rbtree_empty(const ns_rbtree_t *tree);

/**
 * @brief 判断节点是否已经链接到某棵红黑树。
 */
extern int ns_rbtree_node_is_linked(const ns_rbtree_node_t *node);

/**
 * @brief 插入节点。
 */
extern void ns_rbtree_insert(ns_rbtree_t *tree, ns_rbtree_node_t *node);

/**
 * @brief 移除节点。
 */
extern void ns_rbtree_remove(ns_rbtree_t *tree, ns_rbtree_node_t *node);

/**
 * @brief 返回 key 最小的节点。
 */
extern ns_rbtree_node_t *ns_rbtree_first(const ns_rbtree_t *tree);

/**
 * @brief 返回 key 最大的节点。
 */
extern ns_rbtree_node_t *ns_rbtree_last(const ns_rbtree_t *tree);

/**
 * @brief 返回中序遍历的下一个节点。
 */
extern ns_rbtree_node_t *ns_rbtree_next(const ns_rbtree_node_t *node);

/**
 * @brief 返回中序遍历的上一个节点。
 */
extern ns_rbtree_node_t *ns_rbtree_prev(const ns_rbtree_node_t *node);

/**
 * @brief 查找指定 key 的最左匹配节点。
 */
extern ns_rbtree_node_t *ns_rbtree_find_first(const ns_rbtree_t *tree, uint64_t key);

/**
 * @brief 从红黑树节点反查外层结构体指针。
 */
#define ns_rbtree_entry(ptr, type, member) \
    NS_CONTAINER_OF((ptr), type, member)

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_RBTREE_H */
