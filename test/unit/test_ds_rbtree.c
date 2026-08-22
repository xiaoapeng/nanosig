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

/* __ns_rb_erase_color 与 ns_rb_replace_node 为非 static 符号, 静态库会导出,
 * 但未在公共头文件声明, 测试在此补充 extern 声明 */
extern void __ns_rb_erase_color(ns_rbtree_node_t *, ns_rbtree_t *);
extern void ns_rb_replace_node(ns_rbtree_node_t *, ns_rbtree_node_t *, ns_rbtree_t *);

static ns_rbtree_node_t *find_by_key(ns_rbtree_t *tree, uint64_t key)
{
    ns_rbtree_node_t *cursor = ns_rbtree_first(tree);

    while(cursor != NULL){
        if(node_to_item(cursor)->key == key) return cursor;
        cursor = ns_rbtree_next(cursor);
    }
    return NULL;
}

/* 覆盖 ns_rbtree_prev 的全部路径 */
static int test_prev_traversal(void)
{
    ns_rbtree_t tree;
    tree_item_t items[7];
    tree_item_t empty_item;
    const uint64_t keys[] = { 5u, 2u, 8u, 1u, 3u, 7u, 9u };
    const uint64_t asc[] = { 1u, 2u, 3u, 5u, 7u, 8u, 9u };
    const uint64_t desc[] = { 9u, 8u, 7u, 5u, 3u, 2u, 1u };
    ns_rbtree_node_t *node;
    int i;

    ns_rbtree_root_init(&tree, tree_item_cmp);
    for(i = 0; i < 7; ++i){
        items[i].id = i;
        items[i].key = keys[i];
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
    }
    if(validate_tree(&tree) != 0) return 1;

    /* 空节点 prev → NULL */
    ns_rbtree_node_init(&empty_item.node);
    if(ns_rbtree_prev(&empty_item.node) != NULL) return 1;

    /* 最左节点 prev → NULL */
    node = ns_rbtree_first(&tree);
    if(node_to_item(node)->key != 1u) return 1;
    if(ns_rbtree_prev(node) != NULL) return 1;

    /* 有左子树 → 左子树的最右节点 */
    node = find_by_key(&tree, 5u);
    if(node == NULL) return 1;
    if(ns_rbtree_prev(node) == NULL) return 1;
    if(node_to_item(ns_rbtree_prev(node))->key != 3u) return 1;

    /* 无左子树 → 上溯至祖先是右孩子, 返回其父 */
    node = find_by_key(&tree, 3u);
    if(node == NULL) return 1;
    if(ns_rbtree_prev(node) == NULL) return 1;
    if(node_to_item(ns_rbtree_prev(node))->key != 2u) return 1;

    /* 直接调用 prev 做降序遍历 */
    node = ns_rbtree_last(&tree);
    for(i = 0; i < 7; ++i){
        if(node == NULL) return 1;
        if(node_to_item(node)->key != desc[i]) return 1;
        node = ns_rbtree_prev(node);
    }
    if(node != NULL) return 1;

    /* 降序遍历宏 */
    {
        int count = 0;
        tree_item_t *pos;
        ns_rbtree_for_each_entry_prev(pos, &tree, node){
            if(count >= 7) return 1;
            if(pos->key != desc[count]) return 1;
            ++count;
        }
        if(count != 7) return 1;
    }

    /* 升序不被破坏 */
    if(expect_order(&tree, asc, 7) != 0) return 1;

    return 0;
}

/* 覆盖 ns_rbtree_match_find 的命中/向左/向右/未命中/空树 */
static int test_match_find(void)
{
    ns_rbtree_t tree;
    ns_rbtree_t empty;
    tree_item_t items[6];
    const uint64_t keys[] = { 20u, 10u, 30u, 15u, 25u, 5u };
    uint64_t key;
    ns_rbtree_node_t *found;
    int i;

    ns_rbtree_root_init(&tree, tree_item_cmp);
    for(i = 0; i < 6; ++i){
        items[i].id = i;
        items[i].key = keys[i];
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
    }
    if(validate_tree(&tree) != 0) return 1;

    /* 命中 (c==0) */
    key = 20u;
    found = ns_rbtree_match_find(&key, &tree, tree_item_match_key);
    if(found == NULL) return 1;
    if(node_to_item(found)->key != 20u) return 1;

    /* 向左查找 (c<0) */
    key = 5u;
    found = ns_rbtree_match_find(&key, &tree, tree_item_match_key);
    if(found == NULL) return 1;
    if(node_to_item(found)->key != 5u) return 1;

    /* 向右查找 (c>0) */
    key = 25u;
    found = ns_rbtree_match_find(&key, &tree, tree_item_match_key);
    if(found == NULL) return 1;
    if(node_to_item(found)->key != 25u) return 1;

    /* 未命中 → NULL */
    key = 7u;
    found = ns_rbtree_match_find(&key, &tree, tree_item_match_key);
    if(found != NULL) return 1;

    /* 空树 → NULL */
    ns_rbtree_root_init(&empty, tree_item_cmp);
    key = 1u;
    found = ns_rbtree_match_find(&key, &empty, tree_item_match_key);
    if(found != NULL) return 1;

    return 0;
}

/* 覆盖 ns_rbtree_for_each_entry_match 宏与 ns_rbtree_next_match 链尾 */
static int test_next_match(void)
{
    ns_rbtree_t tree;
    tree_item_t items[5];
    const uint64_t keys[] = { 2u, 1u, 3u, 2u, 2u };
    const uint64_t order[] = { 1u, 2u, 2u, 2u, 3u };
    uint64_t key = 2u;
    ns_rbtree_node_t *found;
    int i;

    ns_rbtree_root_init(&tree, tree_item_cmp);
    for(i = 0; i < 5; ++i){
        items[i].id = i;
        items[i].key = keys[i];
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
    }
    if(validate_tree(&tree) != 0) return 1;
    if(expect_order(&tree, order, 5) != 0) return 1;

    /* 条件遍历宏: 3 个等键依次命中 */
    {
        int hits = 0;
        tree_item_t *pos;
        ns_rbtree_for_each_entry_match(pos, &tree, &key, tree_item_match_key, node){
            if(hits >= 3) return 1;
            if(pos->key != 2u) return 1;
            ++hits;
        }
        if(hits != 3) return 1;
    }

    /* find_first 定位最左命中, 逐级 next_match */
    found = ns_rbtree_find_first(&key, &tree, tree_item_match_key);
    if(found == NULL) return 1;
    found = ns_rbtree_next_match(&key, found, tree_item_match_key);
    if(found == NULL) return 1;   /* 第二个等键 */
    found = ns_rbtree_next_match(&key, found, tree_item_match_key);
    if(found == NULL) return 1;   /* 第三个等键 */
    if(node_to_item(found)->key != 2u) return 1;

    /* 链尾之后 next_match → NULL */
    if(ns_rbtree_next_match(&key, found, tree_item_match_key) != NULL) return 1;

    return 0;
}

typedef struct {
    tree_item_t *item;      /* 回调将返回的节点 */
    int *called;            /* 回调被调用次数统计 */
    int return_null;        /* 1: 回调返回 NULL */
} find_new_add_ctx_t;

static ns_rbtree_node_t *find_new_add_new_node(void *user_data)
{
    find_new_add_ctx_t *ctx = (find_new_add_ctx_t *)user_data;

    *ctx->called += 1;
    if(ctx->return_null)
        return NULL;
    /* 已链接节点直接返回, 触发库内防御分支 */
    if(!ns_rbtree_node_is_empty(&ctx->item->node))
        return &ctx->item->node;
    ns_rbtree_node_init(&ctx->item->node);
    return &ctx->item->node;
}

/* 覆盖 ns_rbtree_find_new_add 的命中/未命中/返回 NULL/已链接防御分支 */
static int test_find_new_add(void)
{
    ns_rbtree_t tree;
    tree_item_t items[4];
    find_new_add_ctx_t ctx;
    ns_rbtree_node_t *found;
    uint64_t key;
    int i;
    int called = 0;

    ns_rbtree_root_init(&tree, tree_item_cmp);
    ctx.called = &called;
    ctx.return_null = 0;

    /* 预先初始化节点, 保证空态判断可靠 */
    for(i = 0; i < 4; ++i)
        ns_rbtree_node_init(&items[i].node);

    /* 未命中 → 调用 new_node 并插入 (空树, leftmost) */
    ctx.item = &items[0];
    items[0].id = 0; items[0].key = 20u;
    key = 20u;
    found = ns_rbtree_find_new_add(&key, &tree, tree_item_match_key, &ctx, find_new_add_new_node);
    if(found != &items[0].node) return 1;
    if(called != 1) return 1;
    if(count_nodes(&tree) != 1) return 1;
    if(ns_rbtree_first(&tree) != &items[0].node) return 1;

    /* 命中 → 返回已有节点, 不调用 new_node */
    ctx.item = &items[1];
    items[1].id = 1; items[1].key = 20u;
    key = 20u;
    found = ns_rbtree_find_new_add(&key, &tree, tree_item_match_key, &ctx, find_new_add_new_node);
    if(found != &items[0].node) return 1;
    if(called != 1) return 1;
    if(count_nodes(&tree) != 1) return 1;

    /* 未命中 → 插入非 leftmost */
    ctx.item = &items[2];
    items[2].id = 2; items[2].key = 30u;
    key = 30u;
    found = ns_rbtree_find_new_add(&key, &tree, tree_item_match_key, &ctx, find_new_add_new_node);
    if(found != &items[2].node) return 1;
    if(called != 2) return 1;
    if(count_nodes(&tree) != 2) return 1;

    /* 未命中 → 插入 leftmost (非空树) */
    ctx.item = &items[3];
    items[3].id = 3; items[3].key = 10u;
    key = 10u;
    found = ns_rbtree_find_new_add(&key, &tree, tree_item_match_key, &ctx, find_new_add_new_node);
    if(found != &items[3].node) return 1;
    if(called != 3) return 1;
    if(count_nodes(&tree) != 3) return 1;
    if(ns_rbtree_first(&tree) != &items[3].node) return 1;

    /* new_node 返回 NULL → find_new_add 返回 NULL, 树不变 */
    ctx.return_null = 1;
    key = 40u;
    found = ns_rbtree_find_new_add(&key, &tree, tree_item_match_key, &ctx, find_new_add_new_node);
    if(found != NULL) return 1;
    if(called != 4) return 1;
    if(count_nodes(&tree) != 3) return 1;

    /* new_node 返回已链接节点 → NULL (防御分支) */
    ctx.return_null = 0;
    ctx.item = &items[0];   /* items[0] 已在树中 */
    key = 50u;
    found = ns_rbtree_find_new_add(&key, &tree, tree_item_match_key, &ctx, find_new_add_new_node);
    if(found != NULL) return 1;
    if(called != 5) return 1;
    if(count_nodes(&tree) != 3) return 1;

    if(validate_tree(&tree) != 0) return 1;

    return 0;
}

/* 覆盖 ns_rb_replace_node 的左右子树/仅左/仅右/叶/根五种形态 */
static int test_replace_node(void)
{
    ns_rbtree_t tree;
    tree_item_t items[9];
    tree_item_t repl[5];
    const uint64_t keys[] = { 10u, 5u, 15u, 3u, 7u, 12u, 18u, 1u, 9u };
    const uint64_t order[] = { 1u, 3u, 5u, 7u, 9u, 10u, 12u, 15u, 18u };
    static const struct {
        uint64_t key;
        int has_left;
        int has_right;
    } shapes[] = {
        { 5u,  1, 1 },   /* 左右子树 */
        { 3u,  1, 0 },   /* 仅左 */
        { 7u,  0, 1 },   /* 仅右 */
        { 9u,  0, 0 },   /* 叶 */
        { 10u, 1, 1 },   /* 根 */
    };
    ns_rbtree_node_t *victim;
    ns_rbtree_node_t *old_parent;
    int i;
    int r;

    ns_rbtree_root_init(&tree, tree_item_cmp);
    for(i = 0; i < 9; ++i){
        items[i].id = i;
        items[i].key = keys[i];
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
    }
    if(validate_tree(&tree) != 0) return 1;

    for(r = 0; r < 5; ++r){
        victim = find_by_key(&tree, shapes[r].key);
        if(victim == NULL) return 1;
        if((victim->rb_left != NULL) != (shapes[r].has_left != 0)) return 1;
        if((victim->rb_right != NULL) != (shapes[r].has_right != 0)) return 1;

        old_parent = ns_rb_parent(victim);

        repl[r].id = 100 + r;
        repl[r].key = shapes[r].key;
        ns_rbtree_node_init(&repl[r].node);
        ns_rb_replace_node(victim, &repl[r].node, &tree);

        /* 替换后父指针指向 new */
        if(ns_rb_parent(&repl[r].node) != old_parent) return 1;
        if(repl[r].node.rb_left &&
           ns_rb_parent(repl[r].node.rb_left) != &repl[r].node) return 1;
        if(repl[r].node.rb_right &&
           ns_rb_parent(repl[r].node.rb_right) != &repl[r].node) return 1;

        if(validate_tree(&tree) != 0) return 1;
        if(count_nodes(&tree) != 9) return 1;
        if(expect_order(&tree, order, 9) != 0) return 1;
    }

    return 0;
}

/* 直接调用 __ns_rb_erase_color 覆盖包装层 (Case 1 左旋 + Case 2 父红转黑) */
static int test_erase_color_wrapper(void)
{
    ns_rbtree_t tree;
    tree_item_t items[5];
    ns_rbtree_node_t *N;
    ns_rbtree_node_t *P;
    ns_rbtree_node_t *S;
    ns_rbtree_node_t *Sl;
    ns_rbtree_node_t *Sr;
    int black_height;
    size_t count;

    /* 键严格递增: N(1) < P(2) < Sl(3) < S(4) < Sr(5) */
    items[0].id = 0; items[0].key = 1u; N = &items[0].node;
    items[1].id = 1; items[1].key = 2u; P = &items[1].node;
    items[2].id = 2; items[2].key = 3u; Sl = &items[2].node;
    items[3].id = 3; items[3].key = 4u; S = &items[3].node;
    items[4].id = 4; items[4].key = 5u; Sr = &items[4].node;

    ns_rbtree_root_init(&tree, tree_item_cmp);
    tree.rb_node = P;
    tree.rb_leftmost = N;

    /* 初始状态: P(黑根) / N(红) / S(红) / Sl(黑) / Sr(黑) */
    P->parent_and_color = (ns_rb_parent_node_t)NULL + (ns_rb_parent_node_t)NS_RBTREE_BLACK;
    N->parent_and_color = (ns_rb_parent_node_t)P + (ns_rb_parent_node_t)NS_RBTREE_RED;
    S->parent_and_color = (ns_rb_parent_node_t)P + (ns_rb_parent_node_t)NS_RBTREE_RED;
    Sl->parent_and_color = (ns_rb_parent_node_t)S + (ns_rb_parent_node_t)NS_RBTREE_BLACK;
    Sr->parent_and_color = (ns_rb_parent_node_t)S + (ns_rb_parent_node_t)NS_RBTREE_BLACK;

    P->rb_left = N;
    P->rb_right = S;
    S->rb_left = Sl;
    S->rb_right = Sr;
    N->rb_left = NULL;
    N->rb_right = NULL;
    Sl->rb_left = NULL;
    Sl->rb_right = NULL;
    Sr->rb_left = NULL;
    Sr->rb_right = NULL;

    __ns_rb_erase_color(P, &tree);

    /* 终态: S(黑根) / P(黑, S 左子) / Sr(黑, S 右子) /
       N(红, P 左子) / Sl(红, P 右子) */
    if(tree.rb_node != S) return 1;
    if(ns_rb_is_red(S)) return 1;
    if(S->rb_left != P) return 1;
    if(S->rb_right != Sr) return 1;
    if(ns_rb_is_red(P) || ns_rb_is_red(Sr)) return 1;
    if(P->rb_left != N) return 1;
    if(P->rb_right != Sl) return 1;
    if(ns_rb_is_black(N) || ns_rb_is_black(Sl)) return 1;
    if(ns_rb_parent(S) != NULL) return 1;
    if(ns_rb_parent(P) != S) return 1;
    if(ns_rb_parent(N) != P) return 1;
    if(ns_rb_parent(Sl) != P) return 1;
    if(ns_rb_parent(Sr) != S) return 1;

    /* 红黑不变量与节点数 */
    if(validate_node(S, NULL, 0, 0u, 0, 0u, &black_height, &count) != 0) return 1;
    if(count != 5u) return 1;
    if(validate_tree(&tree) != 0) return 1;
    if(count_nodes(&tree) != 5) return 1;

    return 0;
}

/* 覆盖后序遍历的空树/父右子树下降/left_deepest 右子树路径 */
static int test_postorder_gaps(void)
{
    ns_rbtree_t tree;
    ns_rbtree_t chain;
    tree_item_t items[7];
    tree_item_t chain_items[2];
    const uint64_t keys[] = { 5u, 2u, 8u, 1u, 3u, 7u, 9u };
    const uint64_t post[] = { 1u, 3u, 2u, 7u, 9u, 8u, 5u };
    ns_rbtree_node_t *node;
    int i;

    /* 空树 → NULL */
    ns_rbtree_root_init(&tree, tree_item_cmp);
    if(ns_rbtree_first_postorder(&tree) != NULL) return 1;

    /* NULL 节点 next_postorder → NULL */
    if(ns_rbtree_next_postorder(NULL) != NULL) return 1;

    for(i = 0; i < 7; ++i){
        items[i].id = i;
        items[i].key = keys[i];
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
    }
    if(validate_tree(&tree) != 0) return 1;

    /* 完整后序序列 */
    node = ns_rbtree_first_postorder(&tree);
    for(i = 0; i < 7; ++i){
        if(node == NULL) return 1;
        if(node_to_item(node)->key != post[i]) return 1;
        node = ns_rbtree_next_postorder(node);
    }
    if(node != NULL) return 1;

    /* 左孩子且父有右子树 → 父右子树的 left_deepest_node */
    node = ns_rbtree_first_postorder(&tree);  /* 1 */
    node = ns_rbtree_next_postorder(node);    /* 3 */
    if(node == NULL) return 1;
    if(node_to_item(node)->key != 3u) return 1;

    /* 右孩子 → 返回父 */
    node = ns_rbtree_next_postorder(node);    /* 2 */
    if(node == NULL) return 1;
    if(node_to_item(node)->key != 2u) return 1;

    /* left_deepest_node 右子树下降路径: 无左子、有右子的节点 */
    ns_rbtree_root_init(&chain, tree_item_cmp);
    chain_items[0].id = 0; chain_items[0].key = 1u;
    chain_items[1].id = 1; chain_items[1].key = 2u;
    ns_rbtree_node_init(&chain_items[0].node);
    ns_rbtree_node_init(&chain_items[1].node);
    ns_rbtree_add(&chain_items[0].node, &chain);
    ns_rbtree_add(&chain_items[1].node, &chain);

    node = ns_rbtree_first_postorder(&chain);  /* 右子 2 */
    if(node != &chain_items[1].node) return 1;
    node = ns_rbtree_next_postorder(node);     /* 父 1 */
    if(node != &chain_items[0].node) return 1;
    node = ns_rbtree_next_postorder(node);     /* NULL */
    if(node != NULL) return 1;

    return 0;
}

/* 覆盖 ns_rbtree_last / ns_rbtree_next 的空树、空节点与下降/上溯路径 */
static int test_last_and_next_empty(void)
{
    ns_rbtree_t tree;
    ns_rbtree_t empty;
    tree_item_t items[7];
    tree_item_t empty_item;
    const uint64_t keys[] = { 5u, 2u, 8u, 1u, 3u, 7u, 9u };
    const uint64_t asc[] = { 1u, 2u, 3u, 5u, 7u, 8u, 9u };
    ns_rbtree_node_t *node;
    int i;

    /* 空树 last → NULL */
    ns_rbtree_root_init(&empty, tree_item_cmp);
    if(ns_rbtree_last(&empty) != NULL) return 1;

    /* 空节点 next → NULL */
    ns_rbtree_node_init(&empty_item.node);
    if(ns_rbtree_next(&empty_item.node) != NULL) return 1;

    ns_rbtree_root_init(&tree, tree_item_cmp);
    for(i = 0; i < 7; ++i){
        items[i].id = i;
        items[i].key = keys[i];
        ns_rbtree_node_init(&items[i].node);
        ns_rbtree_add(&items[i].node, &tree);
    }
    if(validate_tree(&tree) != 0) return 1;

    /* last → 最右节点 */
    node = ns_rbtree_last(&tree);
    if(node == NULL) return 1;
    if(node_to_item(node)->key != 9u) return 1;

    /* next: 无右子 → 上溯至祖先是左孩子, 返回其父 */
    node = find_by_key(&tree, 3u);
    node = ns_rbtree_next(node);
    if(node == NULL) return 1;
    if(node_to_item(node)->key != 5u) return 1;

    /* next: 有右子 → 右子树左倾下降 */
    node = find_by_key(&tree, 2u);
    node = ns_rbtree_next(node);
    if(node == NULL) return 1;
    if(node_to_item(node)->key != 3u) return 1;

    /* next: 右子树含左子树, 触发左倾下降循环 */
    node = find_by_key(&tree, 5u);
    node = ns_rbtree_next(node);
    if(node == NULL) return 1;
    if(node_to_item(node)->key != 7u) return 1;

    /* 最右节点 next → NULL (上溯越过根) */
    if(ns_rbtree_next(ns_rbtree_last(&tree)) != NULL) return 1;

    /* 全序列 next 遍历 */
    node = ns_rbtree_first(&tree);
    for(i = 0; i < 7; ++i){
        if(node == NULL) return 1;
        if(node_to_item(node)->key != asc[i]) return 1;
        node = ns_rbtree_next(node);
    }
    if(node != NULL) return 1;

    return 0;
}

/* 强化 ns_rbtree_find_add 的 c==0/leftmost/非 leftmost 分支 */
static int test_find_add_more(void)
{
    ns_rbtree_t tree;
    tree_item_t items[5];
    ns_rbtree_node_t *found;

    ns_rbtree_root_init(&tree, tree_item_cmp);

    /* 空树插入 → leftmost */
    items[0].id = 0; items[0].key = 20u;
    ns_rbtree_node_init(&items[0].node);
    found = ns_rbtree_find_add(&items[0].node, &tree);
    if(found != &items[0].node) return 1;
    if(ns_rbtree_first(&tree) != &items[0].node) return 1;

    /* 非 leftmost 插入 */
    items[1].id = 1; items[1].key = 30u;
    ns_rbtree_node_init(&items[1].node);
    found = ns_rbtree_find_add(&items[1].node, &tree);
    if(found != &items[1].node) return 1;

    /* leftmost 插入 (非空树) */
    items[2].id = 2; items[2].key = 10u;
    ns_rbtree_node_init(&items[2].node);
    found = ns_rbtree_find_add(&items[2].node, &tree);
    if(found != &items[2].node) return 1;
    if(ns_rbtree_first(&tree) != &items[2].node) return 1;

    /* c==0 已存在 → 返回已有节点, 不插入 */
    items[3].id = 3; items[3].key = 20u;
    ns_rbtree_node_init(&items[3].node);
    found = ns_rbtree_find_add(&items[3].node, &tree);
    if(found != &items[0].node) return 1;
    if(count_nodes(&tree) != 3) return 1;

    /* c<0 / c>0 双向下降插入 */
    items[4].id = 4; items[4].key = 25u;
    ns_rbtree_node_init(&items[4].node);
    found = ns_rbtree_find_add(&items[4].node, &tree);
    if(found != &items[4].node) return 1;
    if(count_nodes(&tree) != 4) return 1;

    if(validate_tree(&tree) != 0) return 1;
    if(expect_order(&tree, (const uint64_t[]){ 10u, 20u, 25u, 30u }, 4) != 0) return 1;

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
    if(test_prev_traversal() != 0) return 1;
    if(test_match_find() != 0) return 1;
    if(test_next_match() != 0) return 1;
    if(test_find_new_add() != 0) return 1;
    if(test_replace_node() != 0) return 1;
    if(test_erase_color_wrapper() != 0) return 1;
    if(test_postorder_gaps() != 0) return 1;
    if(test_last_and_next_empty() != 0) return 1;
    if(test_find_add_more() != 0) return 1;

    return 0;
}
