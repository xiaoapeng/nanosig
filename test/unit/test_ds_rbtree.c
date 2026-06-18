/**
 * @file test_ds_rbtree.c
 * @brief 公开红黑树单元测试。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_rbtree.h>

typedef struct tree_item {
    int id;
    uint64_t key;
    ns_rbtree_node_t node;
} tree_item_t;

static int tree_item_cmp(const ns_rbtree_node_t *a, const ns_rbtree_node_t *b)
{
    const tree_item_t *ia = ns_rbtree_entry(a, const tree_item_t, node);
    const tree_item_t *ib = ns_rbtree_entry(b, const tree_item_t, node);

    if(ia->key < ib->key) return -1;
    if(ia->key > ib->key) return 1;
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

    if(NS_RB_NODE_PARENT(node) != parent) return 1;

    node_key = node_to_item(node)->key;
    if(has_min && (node_key < min_key)) return 1;
    if(has_max && (node_key > max_key)) return 1;
    if(NS_RB_NODE_IS_RED(node) &&
       (((node->left != NULL) && NS_RB_NODE_IS_RED(node->left)) ||
        ((node->right != NULL) && NS_RB_NODE_IS_RED(node->right)))){
        return 1;
    }

    if(validate_node(node->left, node, has_min, min_key, 1, node_key, &left_black_height, &left_count) != 0){
        return 1;
    }
    if(validate_node(node->right, node, 1, node_key, has_max, max_key, &right_black_height, &right_count) != 0){
        return 1;
    }
    if(left_black_height != right_black_height) return 1;

    *out_black_height = left_black_height + (NS_RB_NODE_IS_BLACK(node) ? 1 : 0);
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
    if(NS_RB_NODE_IS_RED(tree->root)) return 1;
    if(tree->leftmost != ns_rbtree_first(tree)) return 1;

    if(validate_node(
           tree->root,
           NULL,
           0, 0u,
           0, 0u,
           &black_height,
           &count) != 0){
        return 1;
    }

    return count == tree->size ? 0 : 1;
}

static int test_invalid_operations(void)
{
    ns_rbtree_t tree;
    ns_rbtree_node_t zero_node;
    tree_item_t item;

    ns_rbtree_init(NULL, tree_item_cmp);
    ns_rbtree_node_init(NULL);

    ns_rbtree_init(&tree, tree_item_cmp);
    ns_rbtree_node_init(&zero_node);

    if(expect_true(ns_rbtree_empty(NULL)) != 0) return 1;
    if(expect_true(ns_rbtree_first(NULL) == NULL) != 0) return 1;
    if(expect_true(ns_rbtree_last(NULL) == NULL) != 0) return 1;
    if(expect_true(ns_rbtree_find_first(NULL, &tree) == NULL) != 0) return 1;

    ns_rbtree_remove(&tree, &zero_node);
    if(expect_true(ns_rbtree_empty(&tree)) != 0) return 1;

    item.id = 1;
    item.key = 42u;
    ns_rbtree_node_init(&item.node);
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

    ns_rbtree_init(&tree, tree_item_cmp);

    for(i = 0; i < 32; ++i){
        items[i].id = i;
        items[i].key = (uint64_t)((i * 17) % 23);
        ns_rbtree_node_init(&items[i].node);
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

static int test_find_first_and_match(void)
{
    ns_rbtree_t tree;
    tree_item_t items[4];
    tree_item_t key_item;
    ns_rbtree_node_t *found;

    ns_rbtree_init(&tree, tree_item_cmp);

    items[0].id = 0; items[0].key = 10u;
    items[1].id = 1; items[1].key = 20u;
    items[2].id = 2; items[2].key = 20u;
    items[3].id = 3; items[3].key = 30u;

    for(int i = 0; i < 4; ++i){
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_insert(&tree, &items[i].node);
    }

    /* find_first: 找最左的 key=20 */
    key_item.key = 20u;
    ns_rbtree_node_init(&key_item.node);
    found = ns_rbtree_find_first(&key_item.node, &tree);
    if(found == NULL) return 1;
    if(node_to_item(found)->key != 20u) return 1;

    /* find_first: 不存在的 key */
    key_item.key = 25u;
    ns_rbtree_node_init(&key_item.node);
    found = ns_rbtree_find_first(&key_item.node, &tree);
    if(found != NULL) return 1;

    /* find_add: 已存在 */
    key_item.key = 20u;
    ns_rbtree_node_init(&key_item.node);
    found = ns_rbtree_find_add(&key_item.node, &tree);
    if(found == NULL) return 1;
    if(node_to_item(found)->key != 20u) return 1;

    /* find_add: 不存在，应插入 */
    {
        tree_item_t new_item;
        new_item.id = 99;
        new_item.key = 25u;
        ns_rbtree_node_init(&new_item.node);
        found = ns_rbtree_find_add(&new_item.node, &tree);
        if(found != NULL) return 1;
        if(!ns_rbtree_node_is_linked(&new_item.node)) return 1;
        if(tree.size != 5u) return 1;
        /* 清理 */
        ns_rbtree_remove(&tree, &new_item.node);
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

    ns_rbtree_init(&tree, tree_item_cmp);
    if(expect_true(ns_rbtree_empty(&tree)) != 0) return 1;

    for(i = 0; i < 8; ++i){
        items[i].id = i;
        items[i].key = keys[i];
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_insert(&tree, &items[i].node);
        if(validate_tree(&tree) != 0) return 1;
    }

    if(expect_true(tree.size == 8u) != 0) return 1;
    if(expect_order(&tree, first_order, 8) != 0) return 1;
    if(expect_true(node_to_item(ns_rbtree_first(&tree))->key == 1u) != 0) return 1;
    if(expect_true(node_to_item(ns_rbtree_last(&tree))->key == 9u) != 0) return 1;

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
    if(test_find_first_and_match() != 0) return 1;

    return 0;
}
