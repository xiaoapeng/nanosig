/**
 * @file nanosig_slist.h
 * @brief nanosig intrusive 单向链表。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_SLIST_H
#define NANOSIG_SLIST_H

#include <nanosig/nanosig_types.h>

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
 * @brief 初始化单向链表头。
 */
static inline void ns_slist_init(ns_slist_t *list)
{
    list->first = NULL;
    list->last = NULL;
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
 * @brief 将节点插入单向链表头部。
 */
static inline void ns_slist_push_front(ns_slist_t *list, ns_slist_node_t *node)
{
    node->next = list->first;
    list->first = node;
    if(list->last == NULL) list->last = node;
}

/**
 * @brief 将节点插入单向链表尾部。
 */
static inline void ns_slist_push_back(ns_slist_t *list, ns_slist_node_t *node)
{
    node->next = NULL;
    if(list->last == NULL){
        list->first = node;
        list->last = node;
        return;
    }

    list->last->next = node;
    list->last = node;
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

/**
 * @brief 从单向链表节点反查外层结构体指针。
 */
#define ns_slist_entry(ptr, type, member) \
    NS_CONTAINER_OF((ptr), type, member)

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_SLIST_H */
