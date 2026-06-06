/**
 * @file nanosig_list.h
 * @brief nanosig intrusive 双向链表。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_LIST_H
#define NANOSIG_LIST_H

#include <nanosig/nanosig_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief intrusive 双向链表节点。
 *
 * 用户将该节点嵌入自己的结构体中，链表本身不分配、不释放用户对象。
 */
typedef struct ns_list_node {
    struct ns_list_node *next;
    struct ns_list_node *prev;
} ns_list_node_t;

/**
 * @brief 静态初始化一个双向链表头。
 *
 * @param name 链表头变量名。
 */
#define NS_LIST_INITIALIZER(name) \
    { &(name), &(name) }

/**
 * @brief 初始化双向链表头或已脱链节点。
 *
 * @param head 链表头或节点。
 */
static inline void ns_list_init(ns_list_node_t *head)
{
    head->next = head;
    head->prev = head;
}

/**
 * @brief 判断双向链表是否为空。
 *
 * @param head 链表头。
 * @return 非零表示为空，0 表示非空。
 */
static inline int ns_list_empty(const ns_list_node_t *head)
{
    return head->next == head;
}

static inline void ns_list_insert_between(
    ns_list_node_t *node,
    ns_list_node_t *prev,
    ns_list_node_t *next)
{
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

/**
 * @brief 将节点插入链表头部。
 *
 * @param head 链表头。
 * @param node 待插入节点，调用前不应在其他链表中。
 */
static inline void ns_list_push_front(ns_list_node_t *head, ns_list_node_t *node)
{
    ns_list_insert_between(node, head, head->next);
}

/**
 * @brief 将节点插入链表尾部。
 *
 * @param head 链表头。
 * @param node 待插入节点，调用前不应在其他链表中。
 */
static inline void ns_list_push_back(ns_list_node_t *head, ns_list_node_t *node)
{
    ns_list_insert_between(node, head->prev, head);
}

/**
 * @brief 从链表中移除节点。
 *
 * 移除后节点的 `next` / `prev` 会被置空，便于暴露误用。
 *
 * @param node 待移除节点，必须已经在链表中。
 */
static inline void ns_list_remove(ns_list_node_t *node)
{
    node->next->prev = node->prev;
    node->prev->next = node->next;
    node->next = NULL;
    node->prev = NULL;
}

/**
 * @brief 从链表中移除节点并重新初始化。
 *
 * @param node 待移除节点，必须已经在链表中。
 */
static inline void ns_list_remove_init(ns_list_node_t *node)
{
    node->next->prev = node->prev;
    node->prev->next = node->next;
    ns_list_init(node);
}

/**
 * @brief 返回链表首节点。
 *
 * @param head 链表头。
 * @return 非空时返回首节点，空链表返回 `NULL`。
 */
static inline ns_list_node_t *ns_list_front(ns_list_node_t *head)
{
    return ns_list_empty(head) ? NULL : head->next;
}

/**
 * @brief 返回链表尾节点。
 *
 * @param head 链表头。
 * @return 非空时返回尾节点，空链表返回 `NULL`。
 */
static inline ns_list_node_t *ns_list_back(ns_list_node_t *head)
{
    return ns_list_empty(head) ? NULL : head->prev;
}

/**
 * @brief 弹出链表首节点。
 *
 * @param head 链表头。
 * @return 非空时返回被移除节点，空链表返回 `NULL`。
 */
static inline ns_list_node_t *ns_list_pop_front(ns_list_node_t *head)
{
    ns_list_node_t *node = ns_list_front(head);

    if(node != NULL) ns_list_remove(node);
    return node;
}

/**
 * @brief 弹出链表尾节点。
 *
 * @param head 链表头。
 * @return 非空时返回被移除节点，空链表返回 `NULL`。
 */
static inline ns_list_node_t *ns_list_pop_back(ns_list_node_t *head)
{
    ns_list_node_t *node = ns_list_back(head);

    if(node != NULL) ns_list_remove(node);
    return node;
}

/**
 * @brief 将已有节点移动到链表尾部。
 *
 * @param head 目标链表头。
 * @param node 已在某个链表中的节点。
 */
static inline void ns_list_move_back(ns_list_node_t *head, ns_list_node_t *node)
{
    ns_list_remove(node);
    ns_list_push_back(head, node);
}

/**
 * @brief 将另一个链表拼接到目标链表尾部并清空源链表。
 *
 * @param head 目标链表头。
 * @param other 源链表头。
 */
static inline void ns_list_splice_back_init(ns_list_node_t *head, ns_list_node_t *other)
{
    ns_list_node_t *first;
    ns_list_node_t *last;

    if(ns_list_empty(other)) return;

    first = other->next;
    last = other->prev;

    first->prev = head->prev;
    head->prev->next = first;
    last->next = head;
    head->prev = last;

    ns_list_init(other);
}

/**
 * @brief 从双向链表节点反查外层结构体指针。
 */
#define ns_list_entry(ptr, type, member) \
    NS_CONTAINER_OF((ptr), type, member)

/**
 * @brief 正向遍历双向链表。
 *
 * @param pos  `ns_list_node_t *` 迭代变量。
 * @param head 链表头。
 */
#define ns_list_for_each(pos, head) \
    for((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

/**
 * @brief 正向遍历双向链表，允许遍历期间移除当前节点。
 *
 * 预取下一个节点，安全应对 `ns_list_remove` / `ns_list_remove_init`。
 *
 * @param pos   `ns_list_node_t *` 迭代变量。
 * @param n     `ns_list_node_t *` 临时变量，用于保存下一个节点。
 * @param head  链表头。
 */
#define ns_list_for_each_safe(pos, n, head) \
    for((pos) = (head)->next, (n) = (pos)->next; \
        (pos) != (head); \
        (pos) = (n), (n) = (pos)->next)

/**
 * @brief 反向遍历双向链表。
 *
 * @param pos  `ns_list_node_t *` 迭代变量。
 * @param head 链表头。
 */
#define ns_list_for_each_prev(pos, head) \
    for((pos) = (head)->prev; (pos) != (head); (pos) = (pos)->prev)

/**
 * @brief 反向遍历双向链表，允许遍历期间移除当前节点。
 *
 * @param pos   `ns_list_node_t *` 迭代变量。
 * @param n     `ns_list_node_t *` 临时变量，用于保存上一个节点。
 * @param head  链表头。
 */
#define ns_list_for_each_prev_safe(pos, n, head) \
    for((pos) = (head)->prev, (n) = (pos)->prev; \
        (pos) != (head); \
        (pos) = (n), (n) = (pos)->prev)

/**
 * @brief 正向遍历双向链表中的外层结构体。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param head   链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_list_for_each_entry(pos, head, member) \
    for((pos) = ns_list_entry((head)->next, typeof(*(pos)), member); \
        &(pos)->member != (head); \
        (pos) = ns_list_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * @brief 反向遍历双向链表中的外层结构体。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param head   链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_list_for_each_prev_entry(pos, head, member) \
    for((pos) = ns_list_entry((head)->prev, typeof(*(pos)), member); \
        &(pos)->member != (head); \
        (pos) = ns_list_entry((pos)->member.prev, typeof(*(pos)), member))

/**
 * @brief 正向遍历双向链表中的外层结构体，允许遍历期间移除当前节点。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param n      外层结构体指针临时变量。
 * @param head   链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_list_for_each_entry_safe(pos, n, head, member) \
    for((pos) = ns_list_entry((head)->next, typeof(*(pos)), member), \
        (n) = ns_list_entry((pos)->member.next, typeof(*(pos)), member); \
        &(pos)->member != (head); \
        (pos) = (n), (n) = ns_list_entry((n)->member.next, typeof(*(n)), member))

/**
 * @brief 反向遍历双向链表中的外层结构体，允许遍历期间移除当前节点。
 *
 * @param pos    外层结构体指针迭代变量。
 * @param n      外层结构体指针临时变量。
 * @param head   链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_list_for_each_prev_entry_safe(pos, n, head, member) \
    for((pos) = ns_list_entry((head)->prev, typeof(*(pos)), member), \
        (n) = ns_list_entry((pos)->member.prev, typeof(*(pos)), member); \
        &(pos)->member != (head); \
        (pos) = (n), (n) = ns_list_entry((n)->member.prev, typeof(*(n)), member))

/**
 * @brief 从当前位置继续正向遍历。
 *
 * @param pos    当前位置的外层结构体指针。
 * @param head   链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_list_for_each_entry_continue(pos, head, member) \
    for((pos) = ns_list_entry((pos)->member.next, typeof(*(pos)), member); \
        &(pos)->member != (head); \
        (pos) = ns_list_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * @brief 从当前位置继续反向遍历。
 *
 * @param pos    当前位置的外层结构体指针。
 * @param head   链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_list_for_each_prev_entry_continue(pos, head, member) \
    for((pos) = ns_list_entry((pos)->member.prev, typeof(*(pos)), member); \
        &(pos)->member != (head); \
        (pos) = ns_list_entry((pos)->member.prev, typeof(*(pos)), member))

/**
 * @brief 从当前位置继续正向遍历，允许移除当前节点。
 *
 * @param pos    当前位置的外层结构体指针。
 * @param n      外层结构体指针临时变量。
 * @param head   链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_list_for_each_entry_continue_safe(pos, n, head, member) \
    for((pos) = ns_list_entry((pos)->member.next, typeof(*(pos)), member), \
        (n) = ns_list_entry((pos)->member.next, typeof(*(pos)), member); \
        &(pos)->member != (head); \
        (pos) = (n), (n) = ns_list_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * @brief 从当前位置继续反向遍历，允许移除当前节点。
 *
 * @param pos    当前位置的外层结构体指针。
 * @param n      外层结构体指针临时变量。
 * @param head   链表头。
 * @param member 链表节点在外层结构体中的成员名。
 */
#define ns_list_for_each_prev_entry_continue_safe(pos, n, head, member) \
    for((pos) = ns_list_entry((pos)->member.prev, typeof(*(pos)), member), \
        (n) = ns_list_entry((pos)->member.prev, typeof(*(pos)), member); \
        &(pos)->member != (head); \
        (pos) = (n), (n) = ns_list_entry((pos)->member.prev, typeof(*(pos)), member))

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_LIST_H */
