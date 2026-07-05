/**
 * @file nanosig_rbtree.h
 * @brief nanosig intrusive 红黑树。
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

/**
 * @brief 红黑树节点。
 *
 * 节点由调用方持有并嵌入用户结构体中。
 * `parent_and_color` 打包了父指针和颜色位：最低位为颜色（0=红，1=黑），
 * 高位为父指针。空节点（未插入任何树）的 `parent_and_color` 指向自身。
 *
 * @note 结构体按 `sizeof(long)` 对齐，保证地址低 bit 可用于颜色编码，
 *       自指针 sentinel (`parent_and_color == self`) 与 RED/BLACK bit 不冲突。
 *       嵌入 ns_rbtree_node_t 的用户结构体应避免以 packed 修饰导致奇数偏移。
 */
typedef struct NS_ALIGNED(sizeof(long)) ns_rbtree_node {
    uintptr_t parent_and_color;
    struct ns_rbtree_node *left;
    struct ns_rbtree_node *right;
} ns_rbtree_node_t;

/**
 * @brief 红黑树根。
 *
 * 比较函数由调用方在初始化时提供，语义为：
 * 返回负值表示 a < b，零表示 a == b，正值表示 a > b。
 */
typedef struct ns_rbtree {
    ns_rbtree_node_t *root;
    ns_rbtree_node_t *leftmost;
    size_t size;
    int (*cmp)(const ns_rbtree_node_t *a, const ns_rbtree_node_t *b);
} ns_rbtree_t;

#define NS_RBTREE_RED   0u
#define NS_RBTREE_BLACK 1u

/* ---- 内部辅助宏 ---- */

#define NS_RB_PARENT(pc) \
    ((ns_rbtree_node_t *)((uintptr_t)(pc) & ~(uintptr_t)3u))

#define NS_RB_COLOR(pc) \
    ((int)((uintptr_t)(pc) & (uintptr_t)1u))

#define NS_RB_IS_BLACK(pc)  NS_RB_COLOR(pc)
#define NS_RB_IS_RED(pc)    (!NS_RB_COLOR(pc))

#define NS_RB_NODE_PARENT(node)    NS_RB_PARENT((node)->parent_and_color)
#define NS_RB_NODE_COLOR(node)     NS_RB_COLOR((node)->parent_and_color)
#define NS_RB_NODE_IS_RED(node)    NS_RB_IS_RED((node)->parent_and_color)
#define NS_RB_NODE_IS_BLACK(node)  NS_RB_IS_BLACK((node)->parent_and_color)

/* ---- 公开接口 ---- */

/**
 * @brief 初始化红黑树根。
 *
 * @param tree 待初始化的树。
 * @param cmp  比较函数。
 */
extern void ns_rbtree_init(ns_rbtree_t *tree, int (*cmp)(const ns_rbtree_node_t *, const ns_rbtree_node_t *));

/**
 * @brief 初始化红黑树节点为"空"状态。
 *
 * 空节点的 `parent_and_color` 指向自身。
 *
 * @param node 待初始化的节点。
 */
extern void ns_rbtree_node_init(ns_rbtree_node_t *node);

/**
 * @brief 判断红黑树是否为空。
 *
 * @param tree 红黑树根。
 * @return 树为空返回非零，否则返回 0。
 */
extern int ns_rbtree_empty(const ns_rbtree_t *tree);

/**
 * @brief 判断节点是否已经链接到某棵红黑树。
 *
 * @param node 红黑树节点。
 * @return 节点已链接返回非零，否则返回 0。
 */
extern int ns_rbtree_node_is_linked(const ns_rbtree_node_t *node);

/**
 * @brief 插入节点。
 *
 * 返回被插入的节点。如果插入后该节点成为最左节点，返回值即为该节点；
 * 否则返回 `NULL`。
 *
 * @param tree 红黑树根。
 * @param node 待插入的节点。
 * @return 插入后该节点成为最左节点时返回该节点，否则返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_insert(ns_rbtree_t *tree, ns_rbtree_node_t *node);

/**
 * @brief 移除节点。
 *
 * @param tree 红黑树根。
 * @param node 待移除的节点。
 * @return 被移除的节点，无效输入时返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_remove(ns_rbtree_t *tree, ns_rbtree_node_t *node);

/**
 * @brief 返回 key 最小的节点（中序第一个）。
 *
 * @param tree 红黑树根。
 * @return key 最小的节点，空树返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_first(const ns_rbtree_t *tree);

/**
 * @brief 返回 key 最大的节点（中序最后一个）。
 *
 * @param tree 红黑树根。
 * @return key 最大的节点，空树返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_last(const ns_rbtree_t *tree);

/**
 * @brief 返回中序遍历的下一个节点。
 *
 * @param node 当前节点。
 * @return 中序遍历的下一个节点，无后续节点返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_next(const ns_rbtree_node_t *node);

/**
 * @brief 返回中序遍历的上一个节点。
 *
 * @param node 当前节点。
 * @return 中序遍历的上一个节点，无前驱节点返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_prev(const ns_rbtree_node_t *node);

/**
 * @brief 查找与 key 匹配的第一个节点（最左匹配）。
 *
 * 使用树的比较函数，key 作为 `cmp` 的第一个参数参与比较。
 *
 * @param key  用于匹配的 key 节点。
 * @param tree 红黑树根。
 * @return 找到的第一个匹配节点，未找到返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_find_first(
    const ns_rbtree_node_t *key, const ns_rbtree_t *tree);

/**
 * @brief 查找与 key 匹配的下一个节点。
 *
 * 从 `node` 之后继续查找。
 *
 * @param key  用于匹配的 key 节点。
 * @param node 起始节点。
 * @param tree 红黑树根。
 * @return 下一个匹配节点，无更多匹配返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_next_match(
    const ns_rbtree_node_t *key, ns_rbtree_node_t *node, const ns_rbtree_t *tree);

/**
 * @brief 查找或插入。
 *
 * 查找与 `node` 匹配的节点：找到则返回已有节点，未找到则插入 `node` 并返回 `NULL`。
 *
 * @param node 待查找或插入的节点。
 * @param tree 红黑树根。
 * @return 找到时返回已有节点，未找到时插入 `node` 并返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_find_add(ns_rbtree_node_t *node, ns_rbtree_t *tree);

/**
 * @brief 查找或新建插入。
 *
 * 使用外部 `match` 函数查找。找到则返回已有节点；
 * 未找到则调用 `new_node(user_data)` 创建新节点并插入，返回新建节点。
 *
 * @attention `match` 和树的 `cmp` 必须保持一致的偏序关系 —— `match` 用于树下降（决定
 *            key 的最左插入位置），`cmp` 用于 leftmost 缓存更新。如果两者的排序不一致，
 *            leftmost 缓存可能错误，导致 `ns_rbtree_first()` 返回不正确的结果。
 *           简单场景：直接传递 `cmp` 的包装作为 `match` 即可保证一致性。
 *
 * @param key       外部匹配用的 key。
 * @param tree      红黑树根。
 * @param match     外部匹配函数。
 * @param user_data 传递给 `new_node` 和 `match` 的用户数据。
 * @param new_node  新建节点回调函数。
 * @return 找到时返回已有节点，未找到时返回新建并插入的节点。
 */
extern ns_rbtree_node_t *ns_rbtree_find_new_add(
    const void *key, ns_rbtree_t *tree,
    int (*match)(const void *key, const ns_rbtree_node_t *node),
    void *user_data,
    ns_rbtree_node_t *(*new_node)(void *user_data));

/**
 * @brief 使用外部匹配函数查找节点。
 *
 * 从根开始查找，返回第一个 `match(key, node) == 0` 的节点。
 *
 * @param key   外部匹配用的 key。
 * @param tree  红黑树根。
 * @param match 外部匹配函数。
 * @return 第一个匹配节点，未找到返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_match_find(
    const void *key, const ns_rbtree_t *tree,
    int (*match)(const void *key, const ns_rbtree_node_t *node));

/**
 * @brief 返回后序遍历的第一个节点。
 *
 * @param tree 红黑树根。
 * @return 后序遍历的第一个节点，空树返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_first_postorder(const ns_rbtree_t *tree);

/**
 * @brief 返回后序遍历的下一个节点。
 *
 * @param node 当前节点。
 * @return 后序遍历的下一个节点，无后续节点返回 `NULL`。
 */
extern ns_rbtree_node_t *ns_rbtree_next_postorder(const ns_rbtree_node_t *node);

/**
 * @brief 从红黑树节点反查外层结构体指针。
 */
#define ns_rbtree_entry(ptr, type, member) \
    NS_CONTAINER_OF((ptr), type, member)

/**
 * @brief 安全版反查（NULL 安全）。
 */
#define ns_rbtree_entry_safe(ptr, type, member) \
    NS_CONTAINER_OF_SAFE((ptr), type, member)

/**
 * @brief 判断红黑树根是否为空。
 */
#define ns_rbtree_root_is_empty(root) \
    ((root)->root == NULL)

/**
 * @brief 判断节点是否处于"空"状态（未插入任何树）。
 */
#define ns_rbtree_node_is_empty(node) \
    ((node)->parent_and_color == (uintptr_t)(node))

/**
 * @brief 将节点重置为"空"状态。
 */
#define ns_rbtree_node_clear(node) \
    ((node)->parent_and_color = (uintptr_t)(node))

/* ---- 遍历宏 ---- */

/**
 * @brief 中序递增遍历。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param root   `ns_rbtree_t *` 树根。
 * @param member 红黑树节点在外层结构体中的成员名。
 */
#define ns_rbtree_for_each_entry(pos, root, member) \
    for((pos) = ns_rbtree_entry_safe(ns_rbtree_first(root), typeof(*(pos)), member); \
        (pos) != NULL; \
        (pos) = ns_rbtree_entry_safe(ns_rbtree_next(&(pos)->member), typeof(*(pos)), member))

/**
 * @brief 中序递减遍历。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param root   `ns_rbtree_t *` 树根。
 * @param member 红黑树节点在外层结构体中的成员名。
 */
#define ns_rbtree_for_each_entry_prev(pos, root, member) \
    for((pos) = ns_rbtree_entry_safe(ns_rbtree_last(root), typeof(*(pos)), member); \
        (pos) != NULL; \
        (pos) = ns_rbtree_entry_safe(ns_rbtree_prev(&(pos)->member), typeof(*(pos)), member))

/**
 * @brief 后序遍历（安全版，可用于遍历期间删除整棵树）。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param n      外层结构体指针临时变量。
 * @param root   `ns_rbtree_t *` 树根。
 * @param member 红黑树节点在外层结构体中的成员名。
 */
#define ns_rbtree_for_each_entry_postorder_safe(pos, n, root, member) \
    for((pos) = ns_rbtree_entry_safe(ns_rbtree_first_postorder(root), typeof(*(pos)), member); \
        (pos) != NULL && ((n) = ns_rbtree_entry_safe(ns_rbtree_next_postorder(&(pos)->member), \
            typeof(*(pos)), member), 1); \
        (pos) = (n))

/**
 * @brief 条件遍历：查找所有匹配 key 的节点。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param tree   `ns_rbtree_t *` 树根。
 * @param key    用于比较的 key 节点。
 * @param member 红黑树节点在外层结构体中的成员名。
 */
#define ns_rbtree_for_each_entry_match(pos, tree, key, member) \
    for((pos) = ns_rbtree_entry_safe(ns_rbtree_find_first((key), (tree)), \
            typeof(*(pos)), member); \
        (pos) != NULL; \
        (pos) = ns_rbtree_entry_safe(ns_rbtree_next_match((key), &((pos)->member), (tree)), \
            typeof(*(pos)), member))

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_RBTREE_H */
