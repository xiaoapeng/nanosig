/**
 * @file test_ds_rbtree.c
 * @brief P2 公开 uint64 key 红黑树单元测试。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_rbtree.h>

typedef struct tree_item {
    int id;
    ns_rbtree_node_t node;
} tree_item_t;

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static int expect_order(ns_rbtree_t *tree, const uint64_t *expected, int count)
{
    ns_rbtree_node_t *cursor = ns_rbtree_first(tree);
    int index = 0;

    while(cursor != NULL){
        if(index >= count) return 1;
        if(cursor->key != expected[index]) return 1;
        cursor = ns_rbtree_next(cursor);
        ++index;
    }

    return index == count ? 0 : 1;
}

static int validate_node(
    ns_rbtree_node_t *node,
    ns_rbtree_node_t *parent,
    uint64_t min_key,
    int has_min,
    uint64_t max_key,
    int has_max,
    int *out_black_height,
    size_t *out_count)
{
    int left_black_height;
    int right_black_height;
    size_t left_count;
    size_t right_count;

    if(node == NULL){
        *out_black_height = 1;
        *out_count = 0u;
        return 0;
    }

    if(node->parent != parent) return 1;
    if(has_min && (node->key < min_key)) return 1;
    if(has_max && (node->key > max_key)) return 1;
    if((node->color == NS_RBTREE_RED) &&
       (((node->left != NULL) && (node->left->color == NS_RBTREE_RED)) ||
        ((node->right != NULL) && (node->right->color == NS_RBTREE_RED)))){
        return 1;
    }

    if(validate_node(node->left, node, min_key, has_min, node->key, 1, &left_black_height, &left_count) != 0){
        return 1;
    }
    if(validate_node(node->right, node, node->key, 1, max_key, has_max, &right_black_height, &right_count) != 0){
        return 1;
    }
    if(left_black_height != right_black_height) return 1;

    *out_black_height = left_black_height + (node->color == NS_RBTREE_BLACK ? 1 : 0);
    *out_count = left_count + right_count + 1u;
    return 0;
}

static int validate_tree(ns_rbtree_t *tree)
{
    int black_height;
    size_t count;

    if(tree->root == NULL){
        return (tree->leftmost == NULL) && (tree->size == 0u) ? 0 : 1;
    }
    if(tree->root->color != NS_RBTREE_BLACK) return 1;
    if(tree->leftmost != ns_rbtree_first(tree)) return 1;

    if(validate_node(
           tree->root,
           NULL,
           0u,
           0,
           0u,
           0,
           &black_height,
           &count) != 0){
        return 1;
    }

    return count == tree->size ? 0 : 1;
}

static int test_invalid_operations(void)
{
    ns_rbtree_t tree = { 0 };
    ns_rbtree_node_t zero_node = { 0 };
    tree_item_t item;

    ns_rbtree_init(NULL);
    ns_rbtree_node_init(NULL, 0u);

    if(expect_true(ns_rbtree_empty(NULL)) != 0) return 1;
    if(expect_true(ns_rbtree_first(NULL) == NULL) != 0) return 1;
    if(expect_true(ns_rbtree_last(NULL) == NULL) != 0) return 1;
    if(expect_true(ns_rbtree_find_first(NULL, 1u) == NULL) != 0) return 1;

    ns_rbtree_remove(&tree, &zero_node);
    if(expect_true(ns_rbtree_empty(&tree)) != 0) return 1;

    item.id = 1;
    ns_rbtree_node_init(&item.node, 42u);
    ns_rbtree_remove(&tree, &item.node);
    if(expect_true(!ns_rbtree_node_is_linked(&item.node)) != 0) return 1;

    ns_rbtree_insert(NULL, &item.node);
    if(expect_true(!ns_rbtree_node_is_linked(&item.node)) != 0) return 1;
    ns_rbtree_insert(&tree, NULL);
    if(expect_true(ns_rbtree_empty(&tree)) != 0) return 1;

    ns_rbtree_insert(&tree, &item.node);
    if(validate_tree(&tree) != 0) return 1;
    if(expect_true(tree.size == 1u) != 0) return 1;
    ns_rbtree_insert(&tree, &item.node);
    if(validate_tree(&tree) != 0) return 1;
    if(expect_true(tree.size == 1u) != 0) return 1;

    ns_rbtree_remove(&tree, &item.node);
    if(validate_tree(&tree) != 0) return 1;
    if(expect_true(!ns_rbtree_node_is_linked(&item.node)) != 0) return 1;

    return 0;
}

static int test_many_operations(void)
{
    ns_rbtree_t tree;
    tree_item_t items[32];
    int i;

    ns_rbtree_init(&tree);

    for(i = 0; i < 32; ++i){
        items[i].id = i;
        ns_rbtree_node_init(&items[i].node, (uint64_t)((i * 17) % 23));
        ns_rbtree_insert(&tree, &items[i].node);
        if(validate_tree(&tree) != 0) return 1;
    }

    for(i = 0; i < 32; ++i){
        int index = (i * 7) % 32;
        ns_rbtree_remove(&tree, &items[index].node);
        if(validate_tree(&tree) != 0) return 1;
    }

    return ns_rbtree_empty(&tree) ? 0 : 1;
}

int main(void)
{
    ns_rbtree_t tree;
    tree_item_t items[8];
    const uint64_t keys[] = { 5u, 2u, 8u, 1u, 3u, 7u, 9u, 3u };
    const uint64_t first_order[] = { 1u, 2u, 3u, 3u, 5u, 7u, 8u, 9u };
    const uint64_t after_remove[] = { 1u, 3u, 3u, 7u, 9u };
    ns_rbtree_node_t *node;
    int i;

    ns_rbtree_init(&tree);
    if(expect_true(ns_rbtree_empty(&tree)) != 0) return 1;

    for(i = 0; i < 8; ++i){
        items[i].id = i;
        ns_rbtree_node_init(&items[i].node, keys[i]);
        ns_rbtree_insert(&tree, &items[i].node);
        if(validate_tree(&tree) != 0) return 1;
    }

    if(expect_true(tree.size == 8u) != 0) return 1;
    if(expect_order(&tree, first_order, 8) != 0) return 1;
    if(expect_true(ns_rbtree_first(&tree)->key == 1u) != 0) return 1;
    if(expect_true(ns_rbtree_last(&tree)->key == 9u) != 0) return 1;
    if(expect_true(ns_rbtree_find_first(&tree, 3u)->key == 3u) != 0) return 1;
    if(expect_true(ns_rbtree_find_first(&tree, 4u) == NULL) != 0) return 1;

    ns_rbtree_remove(&tree, &items[1].node);
    if(validate_tree(&tree) != 0) return 1;
    ns_rbtree_remove(&tree, &items[2].node);
    if(validate_tree(&tree) != 0) return 1;
    ns_rbtree_remove(&tree, &items[0].node);
    if(validate_tree(&tree) != 0) return 1;
    if(expect_true(tree.size == 5u) != 0) return 1;
    if(expect_order(&tree, after_remove, 5) != 0) return 1;

    while((node = ns_rbtree_first(&tree)) != NULL){
        ns_rbtree_remove(&tree, node);
        if(validate_tree(&tree) != 0) return 1;
    }

    if(expect_true(ns_rbtree_empty(&tree)) != 0) return 1;
    if(expect_true(tree.size == 0u) != 0) return 1;
    if(test_invalid_operations() != 0) return 1;
    if(test_many_operations() != 0) return 1;

    return 0;
}
