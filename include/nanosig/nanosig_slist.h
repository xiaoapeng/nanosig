/**
 * @file nanosig_slist.h
 * @brief nanosig intrusive 单向链表。
 * @date 2026-05-17
 *
 * @warning 本数据结构不是线程安全的。同一节点上的并发操作需要外部同步。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_SLIST_H
#define NANOSIG_SLIST_H

#include <nanosig/nanosig_types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief intrusive 单向链表节点。
 */
typedef struct ns_slist_node {
    struct ns_slist_node *next;
} ns_slist_node_t;

/**
 * @brief 单向链表头。
 */
typedef struct ns_slist {
    ns_slist_node_t *first;
    ns_slist_node_t *last;
} ns_slist_t;

/**
 * @brief 静态初始化一个空单向链表。
 */
#define NS_SLIST_INITIALIZER \
    { NULL, NULL }

/**
 * @brief 定义并初始化一个单向链表头。
 */
#define NS_SLIST_HEAD(name) \
    ns_slist_t name = NS_SLIST_INITIALIZER

/**
 * @brief 初始化单向链表头。
 */
static inline void ns_slist_init(ns_slist_t *list)
{
    list->first = NULL;
    list->last = NULL;
}

/**
 * @brief 由旧的链表头来初始化新的链表头，旧的链表头将被清空。
 */
static inline void ns_slist_head_move_init(ns_slist_t *old_list, ns_slist_t *new_list)
{
    new_list->first = old_list->first;
    new_list->last = old_list->last;
    old_list->first = NULL;
    old_list->last = NULL;
}

/**
 * @brief 初始化单向链表节点。
 */
static inline void ns_slist_node_init(ns_slist_node_t *node)
{
    node->next = NULL;
}

/**
 * @brief 判断单向链表是否为空。
 */
static inline int ns_slist_empty(const ns_slist_t *list)
{
    return list->first == NULL;
}

/**
 * @brief 判断节点是否在某个链表中。
 *
 * 节点的 `next` 不为 `NULL` 表示它可能在链表中。
 * 注意：尾节点的 `next` 为 `NULL`，此函数对尾节点返回 false。
 * 如需精确判断，请使用 intrusive 标记或外部状态。
 */
static inline int ns_slist_node_is_on_list(const ns_slist_node_t *node)
{
    return node->next != NULL;
}

/**
 * @brief 返回首个节点指针，空链表返回 `NULL`。
 */
static inline ns_slist_node_t *ns_slist_first(const ns_slist_t *list)
{
    return list->first;
}

/**
 * @brief 返回末尾节点指针，空链表返回 `NULL`。
 */
static inline ns_slist_node_t *ns_slist_last(const ns_slist_t *list)
{
    return list->last;
}

/**
 * @brief 返回首个节点指针（peek 别名）。
 */
static inline ns_slist_node_t *ns_slist_peek(const ns_slist_t *list)
{
    return list->first;
}

/**
 * @brief 获取链表下一个节点。
 */
static inline ns_slist_node_t *ns_slist_next(ns_slist_node_t *node)
{
    return node->next;
}

/**
 * @brief 头插法，插入一个节点到链表头。
 *
 * @return 如果在添加此条目之前列表为空，则返回非零。
 */
static inline int ns_slist_push_front(ns_slist_t *list, ns_slist_node_t *node)
{
    int was_empty = (list->first == NULL);

    node->next = list->first;
    list->first = node;
    if(list->last == NULL) list->last = node;
    return was_empty;
}

/**
 * @brief 尾插法，插入一个节点到链表尾。
 *
 * @return 如果在添加此条目之前列表为空，则返回非零。
 */
static inline int ns_slist_push_back(ns_slist_t *list, ns_slist_node_t *node)
{
    int was_empty = (list->first == NULL);

    node->next = NULL;
    if(list->last == NULL){
        list->first = node;
        list->last = node;
    } else {
        list->last->next = node;
        list->last = node;
    }
    return was_empty;
}

/**
 * @brief 头插法，批量插入一批节点到链表头。
 *
 * @param new_first 要插入链表的第一个节点。
 * @param new_last  要插入链表的最后一个节点，其 `next` 应为 `NULL` 或无关紧要。
 * @param list      目标链表头。
 * @return 如果在添加此条目之前列表为空，则返回非零。
 */
static inline int ns_slist_add_batch(ns_slist_node_t *new_first, ns_slist_node_t *new_last, ns_slist_t *list)
{
    int was_empty = (list->first == NULL);

    new_last->next = list->first;
    list->first = new_first;
    if(was_empty) list->last = new_last;
    return was_empty;
}

/**
 * @brief 尾插法，批量插入一批节点到链表尾。
 *
 * @param new_first 要插入链表的第一个节点。
 * @param new_last  要插入链表的最后一个节点，其 `next` 应为 `NULL`。
 * @param list      目标链表头。
 * @return 如果在添加此条目之前列表为空，则返回非零。
 */
static inline int ns_slist_add_batch_tail(ns_slist_node_t *new_first, ns_slist_node_t *new_last, ns_slist_t *list)
{
    int was_empty = (list->first == NULL);

    new_last->next = NULL;
    if(list->last == NULL){
        list->first = new_first;
        list->last = new_last;
    } else {
        list->last->next = new_first;
        list->last = new_last;
    }
    return was_empty;
}

/**
 * @brief 在 `prev` 之后插入一个节点。
 *
 * @param prev 前驱节点；为 `NULL` 时等价于 `ns_slist_push_front`。
 * @param node 待插入节点。
 * @param list 目标链表头。
 */
static inline void ns_slist_insert(ns_slist_node_t *prev, ns_slist_node_t *node, ns_slist_t *list)
{
    if(prev == NULL){
        ns_slist_push_front(list, node);
        return;
    }

    node->next = prev->next;
    prev->next = node;
    if(node->next == NULL) list->last = node;
}

/**
 * @brief 弹出单向链表首节点。
 */
static inline ns_slist_node_t *ns_slist_pop_front(ns_slist_t *list)
{
    ns_slist_node_t *node = list->first;

    if(node == NULL) return NULL;

    list->first = node->next;
    if(list->first == NULL) list->last = NULL;
    ns_slist_node_init(node);
    return node;
}

/**
 * @brief 移除 `prev` 后面的节点。
 *
 * `prev == NULL` 表示移除首节点。
 *
 * @return 被移除的节点，链表为空或目标不存在时返回 `NULL`。
 */
static inline ns_slist_node_t *ns_slist_remove_after(ns_slist_t *list, ns_slist_node_t *prev)
{
    ns_slist_node_t *node;

    node = (prev == NULL) ? list->first : prev->next;
    if(node == NULL) return NULL;

    if(prev == NULL){
        list->first = node->next;
    } else {
        prev->next = node->next;
    }

    if(list->last == node) list->last = prev;
    ns_slist_node_init(node);
    return node;
}

/**
 * @brief 在 safe 遍历期间删除当前节点。
 *
 * 配合 `ns_slist_for_each_safe` 使用，在遍历体内安全删除 `pos`。
 *
 * @param list 目标链表。
 * @param prev 当前节点的前驱节点；首节点时传遍历循环提供的 `prev`。
 * @param next 当前节点的后继节点（已预取）。
 */
static inline void ns_slist_del_node_in_for_each_safe(
    ns_slist_t *list, ns_slist_node_t *prev, ns_slist_node_t *next)
{
    if(prev == NULL){
        list->first = next;
    } else {
        prev->next = next;
    }
    if(next == NULL) list->last = prev;
}

/**
 * @brief 将另一个单向链表拼接到目标链表尾部并清空源链表。
 */
static inline void ns_slist_append_list(ns_slist_t *list, ns_slist_t *other)
{
    if(ns_slist_empty(other)) return;

    if(ns_slist_empty(list)){
        list->first = other->first;
        list->last = other->last;
    } else {
        list->last->next = other->first;
        list->last = other->last;
    }

    ns_slist_init(other);
}

/* ---- 栈语义 ---- */

/**
 * @brief 栈：压入一个节点（等价于 `ns_slist_push_front`）。
 */
static inline void ns_slist_push(ns_slist_t *list, ns_slist_node_t *node)
{
    ns_slist_push_front(list, node);
}

/**
 * @brief 栈：弹出一个节点（等价于 `ns_slist_pop_front`）。
 */
static inline ns_slist_node_t *ns_slist_pop(ns_slist_t *list)
{
    return ns_slist_pop_front(list);
}

/* ---- 队列语义 ---- */

/**
 * @brief 队列：入队一个节点（等价于 `ns_slist_push_back`）。
 */
static inline void ns_slist_enqueue(ns_slist_t *list, ns_slist_node_t *node)
{
    ns_slist_push_back(list, node);
}

/**
 * @brief 队列：出队一个节点（等价于 `ns_slist_pop_front`）。
 */
static inline ns_slist_node_t *ns_slist_dequeue(ns_slist_t *list)
{
    return ns_slist_pop_front(list);
}

/* ---- 反查与遍历宏 ---- */

/**
 * @brief 从单向链表节点反查外层结构体指针。
 */
#define ns_slist_entry(ptr, type, member) \
    NS_CONTAINER_OF((ptr), type, member)

/**
 * @brief 安全版反查（NULL 安全）。
 */
#define ns_slist_entry_safe(ptr, type, member) \
    NS_CONTAINER_OF_SAFE((ptr), type, member)

/**
 * @brief 正向遍历单向链表指针节点。
 *
 * @param pos  `ns_slist_node_t *` 迭代变量。
 * @param head `ns_slist_t *` 或 `ns_slist_node_t *`（从某节点开始遍历）。
 */
#define ns_slist_for_each(pos, head) \
    for((pos) = (head) ? ((const ns_slist_t *)(head) == (const ns_slist_t *)(head) \
            ? ((ns_slist_t *)(head))->first \
            : ((ns_slist_node_t *)(head))->next) \
            : NULL; \
        (pos) != NULL; \
        (pos) = (pos)->next)

/**
 * @brief 正向遍历单向链表，允许遍历期间删除当前节点。
 *
 * @param prev `ns_slist_node_t *` 前驱迭代变量。
 * @param pos  `ns_slist_node_t *` 当前节点迭代变量。
 * @param _next `ns_slist_node_t *` 临时变量。
 * @param head `ns_slist_t *` 或 `ns_slist_node_t *`。
 */
#define ns_slist_for_each_safe(prev, pos, _next, head) \
    for((prev) = (ns_slist_node_t *)(head), \
        (pos) = (prev) ? (prev)->next : NULL; \
        (pos) && ((_next) = (pos)->next, 1); \
        (prev) = ((prev)->next == (_next) ? (prev) : (pos)), \
        (pos) = (_next))

/**
 * @brief 从当前位置继续 safe 遍历。
 */
#define ns_slist_for_each_safe_continue(prev, pos, _next) \
    for((pos) = (prev) ? (prev)->next : NULL; \
        (pos) && ((_next) = (pos)->next, 1); \
        (prev) = ((prev)->next == (_next) ? (prev) : (pos)), \
        (pos) = (_next))

/**
 * @brief 正向遍历单向链表外层结构体。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param head   `ns_slist_t *` 链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_slist_for_each_entry(pos, head, member) \
    for((pos) = ns_slist_entry((head)->first, typeof(*(pos)), member); \
        NS_MEMBER_ADDRESS_IS_NONNULL((pos), member); \
        (pos) = ns_slist_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * @brief 正向遍历单向链表外层结构体，安全版。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param n      外层结构体指针临时变量。
 * @param head   `ns_slist_t *` 链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_slist_for_each_entry_safe(pos, n, head, member) \
    for((pos) = ns_slist_entry((head)->first, typeof(*(pos)), member), \
        (n) = ns_slist_entry((pos)->member.next, typeof(*(pos)), member); \
        NS_MEMBER_ADDRESS_IS_NONNULL((pos), member); \
        (pos) = (n), (n) = ns_slist_entry((n)->member.next, typeof(*(n)), member))

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_SLIST_H */
