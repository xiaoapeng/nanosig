/**
 * @file test_ds_slist.c
 * @brief 公开 intrusive 单向链表单元测试。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>

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

static int test_basic_ops(void)
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

    return 0;
}

static int test_push_front(void)
{
    ns_slist_t list = NS_SLIST_INITIALIZER;
    slist_item_t a = { 10, { NULL } };
    slist_item_t b = { 20, { NULL } };

    ns_slist_push_front(&list, &a.node);
    ns_slist_push_front(&list, &b.node);
    if(expect_true(ns_slist_first(&list) == &b.node) != 0) return 1;
    if(expect_true(ns_slist_last(&list) == &a.node) != 0) return 1;
    return 0;
}

static int test_add_batch(void)
{
    ns_slist_t list = NS_SLIST_INITIALIZER;
    slist_item_t batch[3] = {
        { 1, { NULL } },
        { 2, { NULL } },
        { 3, { NULL } },
    };
    slist_item_t tail[2] = {
        { 4, { NULL } },
        { 5, { NULL } },
    };

    batch[0].node.next = &batch[1].node;
    batch[1].node.next = &batch[2].node;
    batch[2].node.next = NULL;

    if(expect_true(ns_slist_add_batch(&batch[0].node, &batch[2].node, &list) != 0) != 0) return 1;
    if(expect_true(ns_slist_first(&list) == &batch[0].node) != 0) return 1;
    if(expect_true(ns_slist_last(&list) == &batch[2].node) != 0) return 1;

    tail[0].node.next = &tail[1].node;
    tail[1].node.next = NULL;

    if(expect_true(ns_slist_add_batch_tail(&tail[0].node, &tail[1].node, &list) == 0) != 0) return 1;
    if(expect_true(ns_slist_last(&list) == &tail[1].node) != 0) return 1;

    {
        const int expected[] = { 1, 2, 3, 4, 5 };
        if(expect_order(&list, expected, 5) != 0) return 1;
    }
    return 0;
}

static int test_insert_mid(void)
{
    ns_slist_t list = NS_SLIST_INITIALIZER;
    slist_item_t a = { 1, { NULL } };
    slist_item_t b = { 2, { NULL } };
    slist_item_t c = { 3, { NULL } };

    ns_slist_push_front(&list, &a.node);
    ns_slist_push_back(&list, &c.node);
    ns_slist_insert(&a.node, &b.node, &list);
    {
        const int expected[] = { 1, 2, 3 };
        if(expect_order(&list, expected, 3) != 0) return 1;
    }
    {
        slist_item_t z = { 0, { NULL } };
        ns_slist_insert(NULL, &z.node, &list);
        if(expect_true(ns_slist_first(&list) == &z.node) != 0) return 1;
    }
    return 0;
}

static int test_node_status(void)
{
    ns_slist_t list = NS_SLIST_INITIALIZER;
    slist_item_t a = { 1, { NULL } };
    slist_item_t b = { 2, { NULL } };

    if(expect_true(ns_slist_first(&list) == NULL) != 0) return 1;
    if(expect_true(ns_slist_last(&list) == NULL) != 0) return 1;
    if(expect_true(ns_slist_peek(&list) == NULL) != 0) return 1;
    if(expect_true(ns_slist_node_is_on_list(&a.node) == 0) != 0) return 1;

    ns_slist_push_back(&list, &a.node);
    ns_slist_push_back(&list, &b.node);
    if(expect_true(ns_slist_node_is_on_list(&a.node) != 0) != 0) return 1;
    if(expect_true(ns_slist_next(&a.node) == &b.node) != 0) return 1;
    if(expect_true(ns_slist_next(&b.node) == NULL) != 0) return 1;

    return 0;
}

static int test_head_move_init(void)
{
    ns_slist_t old = NS_SLIST_INITIALIZER;
    ns_slist_t new = NS_SLIST_INITIALIZER;
    slist_item_t a = { 1, { NULL } };

    ns_slist_push_back(&old, &a.node);
    ns_slist_head_move_init(&old, &new);

    if(expect_true(ns_slist_empty(&old)) != 0) return 1;
    if(expect_true(ns_slist_first(&new) == &a.node) != 0) return 1;
    if(expect_true(ns_slist_last(&new) == &a.node) != 0) return 1;

    return 0;
}

static int test_stack_queue_ops(void)
{
    ns_slist_t stack = NS_SLIST_INITIALIZER;
    slist_item_t s[3] = {
        { 10, { NULL } },
        { 20, { NULL } },
        { 30, { NULL } },
    };
    ns_slist_node_t *node;

    ns_slist_push(&stack, &s[0].node);
    ns_slist_push(&stack, &s[1].node);
    ns_slist_push(&stack, &s[2].node);

    node = ns_slist_pop(&stack);
    if(expect_true(node == &s[2].node) != 0) return 1;
    node = ns_slist_pop(&stack);
    if(expect_true(node == &s[1].node) != 0) return 1;
    node = ns_slist_pop(&stack);
    if(expect_true(node == &s[0].node) != 0) return 1;
    if(expect_true(ns_slist_empty(&stack)) != 0) return 1;
    if(expect_true(ns_slist_pop(&stack) == NULL) != 0) return 1;

    ns_slist_t queue = NS_SLIST_INITIALIZER;
    slist_item_t q[3] = {
        { 100, { NULL } },
        { 200, { NULL } },
        { 300, { NULL } },
    };

    ns_slist_enqueue(&queue, &q[0].node);
    ns_slist_enqueue(&queue, &q[1].node);
    ns_slist_enqueue(&queue, &q[2].node);

    node = ns_slist_dequeue(&queue);
    if(expect_true(node == &q[0].node) != 0) return 1;
    node = ns_slist_dequeue(&queue);
    if(expect_true(node == &q[1].node) != 0) return 1;
    node = ns_slist_dequeue(&queue);
    if(expect_true(node == &q[2].node) != 0) return 1;
    if(expect_true(ns_slist_empty(&queue)) != 0) return 1;
    if(expect_true(ns_slist_dequeue(&queue) == NULL) != 0) return 1;

    return 0;
}

static int test_traversal(void)
{
    ns_slist_t list = NS_SLIST_INITIALIZER;
    slist_item_t items[4] = {
        { 10, { NULL } },
        { 20, { NULL } },
        { 30, { NULL } },
        { 40, { NULL } },
    };
    ns_slist_node_t *pos_prev, *pos, *tmp;
    slist_item_t *epos;
    int count;

    ns_slist_push_back(&list, &items[0].node);
    ns_slist_push_back(&list, &items[1].node);
    ns_slist_push_back(&list, &items[2].node);
    ns_slist_push_back(&list, &items[3].node);

    /* ns_slist_for_each */
    count = 0;
    ns_slist_for_each(pos, &list){
        count++;
    }
    if(expect_true(count == 4) != 0) return 1;

    /* ns_slist_for_each_entry */
    {
        int sum = 0;
        ns_slist_for_each_entry(epos, &list, node){
            sum += epos->value;
        }
        if(expect_true(sum == 100) != 0) return 1;
    }

    /* ns_slist_for_each_safe + ns_slist_del_node_in_for_each_safe (remove middle) */
    count = 0;
    ns_slist_for_each_safe(pos_prev, pos, tmp, &list){
        if(pos == NULL) return 1;
        count++;
        if(count == 2){
            ns_slist_del_node_in_for_each_safe(&list, pos_prev, tmp);
        }
    }
    if(expect_true(count == 4) != 0) return 1;

    /* 1 removed => 3 remain */
    count = 0;
    ns_slist_for_each(pos, &list){
        count++;
    }
    if(expect_true(count == 3) != 0) return 1;

    /* Clean up: pop_front until empty */
    while(ns_slist_pop_front(&list) != NULL);
    if(expect_true(ns_slist_empty(&list)) != 0) return 1;

    return 0;
}

static int test_invalid_inputs(void)
{
    ns_slist_t list = NS_SLIST_INITIALIZER;
    if(expect_true(ns_slist_empty(&list)) != 0) return 1;
    if(expect_true(ns_slist_pop_front(&list) == NULL) != 0) return 1;
    if(expect_true(ns_slist_empty(&list)) != 0) return 1;
    return 0;
}

int main(void)
{
    if(test_basic_ops() != 0) return 1;
    if(test_push_front() != 0) return 1;
    if(test_add_batch() != 0) return 1;
    if(test_insert_mid() != 0) return 1;
    if(test_node_status() != 0) return 1;
    if(test_head_move_init() != 0) return 1;
    if(test_stack_queue_ops() != 0) return 1;
    if(test_traversal() != 0) return 1;
    if(test_invalid_inputs() != 0) return 1;
    return 0;
}
