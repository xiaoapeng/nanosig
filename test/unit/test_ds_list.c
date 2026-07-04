/**
 * @file test_ds_list.c
 * @brief 公开 intrusive 双向链表单元测试。
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

static int test_basic_ops(void)
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

    /* push_back → verify order */
    ns_list_push_back(&head, &a.node);
    ns_list_push_back(&head, &b.node);
    ns_list_push_back(&head, &c.node);
    if(expect_order(&head, initial_order, 3) != 0) return 1;

    /* remove_init */
    ns_list_remove_init(&b.node);
    if(expect_order(&head, after_remove, 2) != 0) return 1;

    /* move_back */
    ns_list_move_back(&head, &a.node);
    if(expect_order(&head, after_move, 2) != 0) return 1;

    /* splice_back_init */
    ns_list_push_back(&other, &d.node);
    ns_list_splice_back_init(&head, &other);
    if(expect_true(ns_list_empty(&other)) != 0) return 1;
    if(expect_order(&head, after_splice, 3) != 0) return 1;

    /* pop_front / pop_back */
    popped = ns_list_pop_front(&head);
    if(expect_true(popped == &c.node) != 0) return 1;
    popped = ns_list_pop_back(&head);
    if(expect_true(popped == &d.node) != 0) return 1;

    /* remove (without init) leaves node-dangling, but the list is done */
    ns_list_remove(&a.node);
    if(expect_true(ns_list_empty(&head)) != 0) return 1;

    return 0;
}

static int test_push_front(void)
{
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    list_item_t a = { 1, { NULL, NULL } };
    list_item_t b = { 2, { NULL, NULL } };

    ns_list_push_front(&head, &a.node);
    ns_list_push_front(&head, &b.node);
    /* b → a (b is new front) */
    if(expect_true(ns_list_front(&head) == &b.node) != 0) return 1;
    if(expect_true(ns_list_back(&head) == &a.node) != 0) return 1;
    return 0;
}

static int test_insert_between(void)
{
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    list_item_t a = { 10, { NULL, NULL } };
    list_item_t b = { 20, { NULL, NULL } };

    /* Insert between head and head->next (= head itself) on an empty list:
     * same effect as push_back. */
    ns_list_insert_between(&a.node, &head, head.next);
    if(expect_true(ns_list_front(&head) == &a.node) != 0) return 1;
    if(expect_true(ns_list_back(&head) == &a.node) != 0) return 1;

    /* Insert b after head, before a — i.e. push_front via between */
    ns_list_insert_between(&b.node, &head, &a.node);
    if(expect_true(ns_list_front(&head) == &b.node) != 0) return 1;
    if(expect_true(ns_list_back(&head) == &a.node) != 0) return 1;
    return 0;
}

static int test_front_back(void)
{
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    list_item_t a = { 1, { NULL, NULL } };

    if(expect_true(ns_list_front(&head) == NULL) != 0) return 1;
    if(expect_true(ns_list_back(&head) == NULL) != 0) return 1;

    ns_list_push_back(&head, &a.node);
    if(expect_true(ns_list_front(&head) == &a.node) != 0) return 1;
    if(expect_true(ns_list_back(&head) == &a.node) != 0) return 1;

    return 0;
}

static int test_splice_self(void)
{
    /* ns_list_splice_back_init with head == other: must be a no-op */
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    list_item_t a = { 1, { NULL, NULL } };

    ns_list_push_back(&head, &a.node);
    ns_list_splice_back_init(&head, &head);
    if(expect_true(ns_list_front(&head) == &a.node) != 0) return 1;
    if(expect_true(ns_list_back(&head) == &a.node) != 0) return 1;
    return 0;
}

static int test_node_traversal(void)
{
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    list_item_t items[4] = {
        { 10, { NULL, NULL } },
        { 20, { NULL, NULL } },
        { 30, { NULL, NULL } },
        { 40, { NULL, NULL } },
    };
    ns_list_node_t *pos, *tmp;
    int count;

    /* Build: 10 → 20 → 30 → 40 */
    ns_list_push_back(&head, &items[0].node);
    ns_list_push_back(&head, &items[1].node);
    ns_list_push_back(&head, &items[2].node);
    ns_list_push_back(&head, &items[3].node);

    /* ns_list_for_each — forward traversal */
    count = 0;
    ns_list_for_each(pos, &head){
        if(pos == NULL) return 1;
        count++;
    }
    if(expect_true(count == 4) != 0) return 1;

    /* ns_list_for_each_prev — reverse traversal */
    count = 0;
    ns_list_for_each_prev(pos, &head){
        if(pos == NULL) return 1;
        count++;
    }
    if(expect_true(count == 4) != 0) return 1;

    /* ns_list_for_each_safe — forward with removal */
    count = 0;
    ns_list_for_each_safe(pos, tmp, &head){
        if(pos == NULL) return 1;
        if(pos == &items[1].node || pos == &items[3].node){
            ns_list_remove_init(pos);
        }
        count++;
    }
    /* 4 visited, 2 removed => still 2 left */
    if(expect_true(count == 4) != 0) return 1;
    count = 0;
    ns_list_for_each(pos, &head){
        count++;
    }
    if(expect_true(count == 2) != 0) return 1;

    /* ns_list_for_each_prev_safe — reverse with removal */
    ns_list_for_each_safe(pos, tmp, &head){
        ns_list_remove_init(pos);
    }
    if(expect_true(ns_list_empty(&head)) != 0) return 1;

    return 0;
}

static int test_entry_traversal(void)
{
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    list_item_t items[3] = {
        { 100, { NULL, NULL } },
        { 200, { NULL, NULL } },
        { 300, { NULL, NULL } },
    };
    list_item_t *epos, *n;
    int sum;

    ns_list_push_back(&head, &items[0].node);
    ns_list_push_back(&head, &items[1].node);
    ns_list_push_back(&head, &items[2].node);

    /* ns_list_for_each_entry */
    sum = 0;
    ns_list_for_each_entry(epos, &head, node){
        sum += epos->value;
    }
    if(expect_true(sum == 600) != 0) return 1;

    /* ns_list_for_each_prev_entry */
    sum = 0;
    ns_list_for_each_prev_entry(epos, &head, node){
        sum += epos->value;
    }
    if(expect_true(sum == 600) != 0) return 1;

    /* ns_list_for_each_entry_safe — remove middle item while iterating */
    ns_list_for_each_entry_safe(epos, n, &head, node){
        if(epos->value == 200){
            ns_list_remove_init(&epos->node);
        }
    }
    if(expect_true(ns_list_empty(&head) == 0) != 0) return 1;
    if(expect_true(ns_list_front(&head) == &items[0].node) != 0) return 1;
    if(expect_true(ns_list_back(&head) == &items[2].node) != 0) return 1;

    /* ns_list_for_each_entry_continue — from first item */
    epos = &items[0];
    ns_list_for_each_entry_continue(epos, &head, node){
        /* visits items[2] only; items[1] was removed */
        if(epos->value != 300) return 1;
    }

    /* Clean up */
    ns_list_for_each_entry_safe(epos, n, &head, node){
        ns_list_remove_init(&epos->node);
    }
    if(expect_true(ns_list_empty(&head)) != 0) return 1;

    return 0;
}

static int test_invalid_inputs(void)
{
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    if(expect_true(ns_list_empty(&head)) != 0) return 1;
    return 0;
}

int main(void)
{
    if(test_basic_ops() != 0) return 1;
    if(test_push_front() != 0) return 1;
    if(test_insert_between() != 0) return 1;
    if(test_front_back() != 0) return 1;
    if(test_splice_self() != 0) return 1;
    if(test_node_traversal() != 0) return 1;
    if(test_entry_traversal() != 0) return 1;
    if(test_invalid_inputs() != 0) return 1;
    return 0;
}
