/**
 * @file test_ds_slist.c
 * @brief P2 公开 intrusive 单向链表单元测试。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_slist.h>

typedef struct slist_item {
    int value;
    ns_slist_node_t node;
} slist_item_t;

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static int expect_order(ns_slist_t *list, const int *expected, int count)
{
    ns_slist_node_t *cursor = list->first;
    int index = 0;

    while(cursor != NULL){
        slist_item_t *item = ns_slist_entry(cursor, slist_item_t, node);
        if(index >= count) return 1;
        if(item->value != expected[index]) return 1;
        cursor = cursor->next;
        ++index;
    }

    return index == count ? 0 : 1;
}

int main(void)
{
    ns_slist_t list = NS_SLIST_INITIALIZER;
    ns_slist_t other = NS_SLIST_INITIALIZER;
    slist_item_t a = { 1, { NULL } };
    slist_item_t b = { 2, { NULL } };
    slist_item_t c = { 3, { NULL } };
    slist_item_t d = { 4, { NULL } };
    const int first_order[] = { 1, 2, 3 };
    const int after_remove[] = { 1, 3 };
    const int after_append[] = { 1, 3, 4 };
    ns_slist_node_t *node;

    if(expect_true(ns_slist_empty(&list)) != 0) return 1;

    ns_slist_push_back(&list, &a.node);
    ns_slist_push_back(&list, &b.node);
    ns_slist_push_back(&list, &c.node);
    if(expect_order(&list, first_order, 3) != 0) return 1;

    node = ns_slist_remove_after(&list, &a.node);
    if(expect_true(node == &b.node) != 0) return 1;
    if(expect_order(&list, after_remove, 2) != 0) return 1;

    ns_slist_push_back(&other, &d.node);
    ns_slist_append_list(&list, &other);
    if(expect_true(ns_slist_empty(&other)) != 0) return 1;
    if(expect_order(&list, after_append, 3) != 0) return 1;

    node = ns_slist_pop_front(&list);
    if(expect_true(node == &a.node) != 0) return 1;
    node = ns_slist_pop_front(&list);
    if(expect_true(node == &c.node) != 0) return 1;
    node = ns_slist_pop_front(&list);
    if(expect_true(node == &d.node) != 0) return 1;
    if(expect_true(ns_slist_pop_front(&list) == NULL) != 0) return 1;
    if(expect_true(ns_slist_empty(&list)) != 0) return 1;

    return 0;
}
