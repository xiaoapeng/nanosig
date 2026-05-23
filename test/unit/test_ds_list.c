/**
 * @file test_ds_list.c
 * @brief P2 公开 intrusive 双向链表单元测试。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_list.h>

typedef struct list_item {
    int value;
    ns_list_node_t node;
} list_item_t;

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static int expect_order(ns_list_node_t *head, const int *expected, int count)
{
    ns_list_node_t *cursor = head->next;
    int index = 0;

    while(cursor != head){
        list_item_t *item = ns_list_entry(cursor, list_item_t, node);
        if(index >= count) return 1;
        if(item->value != expected[index]) return 1;
        cursor = cursor->next;
        ++index;
    }

    return index == count ? 0 : 1;
}

int main(void)
{
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    ns_list_node_t other = NS_LIST_INITIALIZER(other);
    list_item_t a = { 1, { NULL, NULL } };
    list_item_t b = { 2, { NULL, NULL } };
    list_item_t c = { 3, { NULL, NULL } };
    list_item_t d = { 4, { NULL, NULL } };
    const int initial_order[] = { 1, 2, 3 };
    const int after_remove[] = { 1, 3 };
    const int after_move[] = { 3, 1 };
    const int after_splice[] = { 3, 1, 4 };
    ns_list_node_t *popped;

    if(expect_true(ns_list_empty(&head)) != 0) return 1;

    ns_list_push_back(&head, &a.node);
    ns_list_push_back(&head, &b.node);
    ns_list_push_back(&head, &c.node);
    if(expect_order(&head, initial_order, 3) != 0) return 1;

    ns_list_remove_init(&b.node);
    if(expect_order(&head, after_remove, 2) != 0) return 1;

    ns_list_move_back(&head, &a.node);
    if(expect_order(&head, after_move, 2) != 0) return 1;

    ns_list_push_back(&other, &d.node);
    ns_list_splice_back_init(&head, &other);
    if(expect_true(ns_list_empty(&other)) != 0) return 1;
    if(expect_order(&head, after_splice, 3) != 0) return 1;

    popped = ns_list_pop_front(&head);
    if(expect_true(popped == &c.node) != 0) return 1;
    popped = ns_list_pop_back(&head);
    if(expect_true(popped == &d.node) != 0) return 1;

    ns_list_remove(&a.node);
    if(expect_true(ns_list_empty(&head)) != 0) return 1;

    return 0;
}
