/**
 * @file test_ds_rbtree.c
 * @brief 公开红黑树单元测试。
 * @date 2026-07-06
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_rbtree.h>

typedef struct tree_item {
    int id;
    uint64_t key;
    ns_rbtree_node_t node;
} tree_item_t;

static int tree_item_cmp(ns_rbtree_node_t *a, ns_rbtree_node_t *b)
{
    const tree_item_t *ia = ns_rbtree_entry(a, const tree_item_t, node);
    const tree_item_t *ib = ns_rbtree_entry(b, const tree_item_t, node);

    if(ia->key < ib->key) return -1;
    if(ia->key > ib->key) return 1;
    return 0;
}

static int tree_item_match_key(const void *key, const ns_rbtree_node_t *node)
{
    uint64_t k = *(const uint64_t *)key;
    const tree_item_t *item = ns_rbtree_entry(node, const tree_item_t, node);

    if(k < item->key) return -1;
    if(k > item->key) return 1;
    return 0;
}

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static tree_item_t *node_to_item(const ns_rbtree_node_t *node)
{
    return ns_rbtree_entry(node, tree_item_t, node);
}

static int expect_order(ns_rbtree_t *tree, const uint64_t *expected, int count)
{
    ns_rbtree_node_t *cursor = ns_rbtree_first(tree);
    int index = 0;

    while(cursor != NULL){
        if(index >= count) return 1;
        if(node_to_item(cursor)->key != expected[index]) return 1;
        cursor = ns_rbtree_next(cursor);
        ++index;
    }

    return index == count ? 0 : 1;
}

static int validate_node(
    ns_rbtree_node_t *node,
    ns_rbtree_node_t *parent,
    int has_min,
    uint64_t min_key,
    int has_max,
    uint64_t max_key,
    int *out_black_height,
    size_t *out_count)
{
    int left_black_height;
    int right_black_height;
    size_t left_count;
    size_t right_count;
    uint64_t node_key;

    if(node == NULL){
        *out_black_height = 1;
        *out_count = 0u;
        return 0;
    }

    if(ns_rb_parent(node) != parent) return 1;

    node_key = node_to_item(node)->key;
    if(has_min && (node_key < min_key)) return 1;
    if(has_max && (node_key > max_key)) return 1;
    if(ns_rb_is_red(node) &&
       (((node->rb_left != NULL) && ns_rb_is_red(node->rb_left)) ||
        ((node->rb_right != NULL) && ns_rb_is_red(node->rb_right)))){
        return 1;
    }

    if(validate_node(node->rb_left, node, has_min, min_key, 1, node_key, &left_black_height, &left_count) != 0){
        return 1;
    }
    if(validate_node(node->rb_right, node, 1, node_key, has_max, max_key, &right_black_height, &right_count) != 0){
        return 1;
    }
    if(left_black_height != right_black_height) return 1;

    *out_black_height = left_black_height + (ns_rb_is_black(node) ? 1 : 0);
    *out_count = left_count + right_count + 1u;
    return 0;
}

static int count_nodes(ns_rbtree_t *tree)
{
    int count = 0;
    ns_rbtree_node_t *cursor = ns_rbtree_first(tree);
    while(cursor != NULL){
        ++count;
        cursor = ns_rbtree_next(cursor);
    }
    return count;
}

static int validate_tree(ns_rbtree_t *tree)
{
    int black_height;
    size_t count;

    if(tree->rb_node == NULL){
        return (tree->rb_leftmost == NULL) ? 0 : 1;
    }
    if(ns_rb_is_red(tree->rb_node)) return 1;
    if(tree->rb_leftmost != ns_rbtree_first(tree)) return 1;

    if(validate_node(
           tree->rb_node,
           NULL,
           0, 0u,
           0, 0u,
           &black_height,
           &count) != 0){
        return 1;
    }

    return 0;
}

static int test_basic_operations(void)
{
    ns_rbtree_t tree;
    tree_item_t item;

    ns_rbtree_root_init(&tree, tree_item_cmp);
    if(expect_true(ns_rbtree_root_is_empty(&tree)) != 0) return 1;

    item.id = 1;
    item.key = 42u;
    ns_rbtree_node_init(&item.node);
    if(expect_true(ns_rbtree_node_is_empty(&item.node)) != 0) return 1;

    ns_rbtree_add(&item.node, &tree);
    if(validate_tree(&tree) != 0) return 1;
    if(expect_true(!ns_rbtree_node_is_empty(&item.node)) != 0) return 1;

    ns_rbtree_del(&item.node, &tree);
    if(validate_tree(&tree) != 0) return 1;

    return 0;
}

static int test_many_operations(void)
{
    ns_rbtree_t tree;
    tree_item_t items[32];
    int i;

    ns_rbtree_root_init(&tree, tree_item_cmp);

    for(i = 0; i < 32; ++i){
        items[i].id = i;
        items[i].key = (uint64_t)((i * 17) % 23);
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
        if(validate_tree(&tree) != 0) return 1;
    }

    for(i = 0; i < 32; ++i){
        int index = (i * 7) % 32;
        ns_rbtree_del(&items[index].node, &tree);
        if(validate_tree(&tree) != 0) return 1;
    }

    return ns_rbtree_root_is_empty(&tree) ? 0 : 1;
}

static int test_find_first_and_match(void)
{
    ns_rbtree_t tree;
    tree_item_t items[4];
    uint64_t key;
    ns_rbtree_node_t *found;

    ns_rbtree_root_init(&tree, tree_item_cmp);

    items[0].id = 0; items[0].key = 10u;
    items[1].id = 1; items[1].key = 20u;
    items[2].id = 2; items[2].key = 20u;
    items[3].id = 3; items[3].key = 30u;

    for(int i = 0; i < 4; ++i){
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
    }

    /* find_first: 找最左的 key=20 */
    key = 20u;
    found = ns_rbtree_find_first(&key, &tree, tree_item_match_key);
    if(found == NULL) return 1;
    if(node_to_item(found)->key != 20u) return 1;

    /* find_first: 不存在的 key */
    key = 25u;
    found = ns_rbtree_find_first(&key, &tree, tree_item_match_key);
    if(found != NULL) return 1;

    /* find_add: 已存在 */
    {
        tree_item_t key_item;
        key_item.key = 20u;
        ns_rbtree_node_init(&key_item.node);
        found = ns_rbtree_find_add(&key_item.node, &tree);
        if(found == NULL) return 1;
        if(node_to_item(found)->key != 20u) return 1;
    }

    /* find_add: 不存在，应插入 */
    {
        tree_item_t new_item;
        new_item.id = 99;
        new_item.key = 25u;
        ns_rbtree_node_init(&new_item.node);
        found = ns_rbtree_find_add(&new_item.node, &tree);
        if(found != &new_item.node) return 1;
        if(ns_rbtree_node_is_empty(&new_item.node)) return 1;
        if(count_nodes(&tree) != 5) return 1;
        /* 清理 */
        ns_rbtree_del(&new_item.node, &tree);
        ns_rbtree_node_init(&new_item.node);
    }

    /* 遍历宏 */
    {
        int count = 0;
        tree_item_t *pos;
        ns_rbtree_for_each_entry(pos, &tree, node){
            ++count;
        }
        if(count != 4) return 1;
    }

    /* 后序遍历宏 */
    {
        int count = 0;
        tree_item_t *pos;
        tree_item_t *n;
        ns_rbtree_for_each_entry_postorder_safe(pos, n, &tree, node){
            ++count;
        }
        if(count != 4) return 1;
    }

    return 0;
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

    ns_rbtree_root_init(&tree, tree_item_cmp);
    if(expect_true(ns_rbtree_root_is_empty(&tree)) != 0) return 1;

    for(i = 0; i < 8; ++i){
        items[i].id = i;
        items[i].key = keys[i];
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
        if(validate_tree(&tree) != 0) return 1;
    }

    if(expect_true(count_nodes(&tree) == 8) != 0) return 1;
    if(expect_order(&tree, first_order, 8) != 0) return 1;
    if(expect_true(node_to_item(ns_rbtree_first(&tree))->key == 1u) != 0) return 1;
    if(expect_true(node_to_item(ns_rbtree_last(&tree))->key == 9u) != 0) return 1;

    ns_rbtree_del(&items[1].node, &tree);
    if(validate_tree(&tree) != 0) return 1;
    ns_rbtree_del(&items[2].node, &tree);
    if(validate_tree(&tree) != 0) return 1;
    ns_rbtree_del(&items[0].node, &tree);
    if(validate_tree(&tree) != 0) return 1;
    if(expect_true(count_nodes(&tree) == 5) != 0) return 1;
    if(expect_order(&tree, after_remove, 5) != 0) return 1;

    while((node = ns_rbtree_first(&tree)) != NULL){
        ns_rbtree_del(node, &tree);
        if(validate_tree(&tree) != 0) return 1;
    }

    if(expect_true(ns_rbtree_root_is_empty(&tree)) != 0) return 1;
    if(test_basic_operations() != 0) return 1;
    if(test_many_operations() != 0) return 1;
    if(test_find_first_and_match() != 0) return 1;

    return 0;
}
