/**
 * @file ns_rbtree.c
 * @brief nanosig intrusive 红黑树实现。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 *
 * @section encoding 编码约定
 * `ns_rbtree_node_t::parent_and_color` 打包父指针与颜色 bit：最低位是颜色
 * （NS_RBTREE_RED=0，NS_RBTREE_BLACK=1），高位是父指针。结构体按
 * `sizeof(long)` 对齐（见 nanosig_rbtree.h），保证地址低 bit 可用作颜色编码。
 *
 * 空节点（`ns_rbtree_node_init` 后未入树）使用自指针 sentinel：
 * `parent_and_color == (uintptr_t)node`。自指针的最低位必然为 0（指针按
 * `sizeof(long)` 对齐），因此 sentinel 自然编码为 RED。这与"空 = BLACK"的
 * 直觉相反但安全（红色空节点不影响父侧黑色路径计数），与 Linux 内核 rbtree
 * 的 NULL sentinel 在语义上等价但避免 NULL 解引用崩溃。
 *
 * @section match_vs_cmp match 与 cmp 的一致性
 * `ns_rbtree_find_new_add` 用 `match()` 做树下降定位插入点，用 `tree->cmp`
 * 维护 leftmost 缓存。如果两者偏序不一致，leftmost 缓存可能错误。
 * 调用方必须保证 `match` 与 `cmp` 的语义一致（典型做法：直接用 cmp 的
 * 包装作为 match）。头文件 `nanosig_rbtree.h` 中 `ns_rbtree_find_new_add`
 * 处的 `@attention` 详细说明此约束。
 */

#include <nanosig/nanosig_rbtree.h>

/* ---- 内部辅助 ---- */

static int ns_rb_is_red(const ns_rbtree_node_t *node)
{
    return (node != NULL) && NS_RB_NODE_IS_RED(node);
}

static int ns_rb_is_black(const ns_rbtree_node_t *node)
{
    return !ns_rb_is_red(node);
}

static void ns_rb_set_parent(ns_rbtree_node_t *node, ns_rbtree_node_t *parent)
{
    node->parent_and_color = (uintptr_t)parent | (node->parent_and_color & (uintptr_t)1u);
}

static void ns_rb_set_color(ns_rbtree_node_t *node, int color)
{
    node->parent_and_color = (node->parent_and_color & ~(uintptr_t)1u) | (uintptr_t)color;
}

static ns_rbtree_node_t *ns_rb_minimum(ns_rbtree_node_t *node)
{
    if(node == NULL) return NULL;
    while(node->left != NULL) node = node->left;
    return node;
}

static ns_rbtree_node_t *ns_rb_maximum(ns_rbtree_node_t *node)
{
    if(node == NULL) return NULL;
    while(node->right != NULL) node = node->right;
    return node;
}

static int ns_rb_node_can_remove(const ns_rbtree_t *tree, const ns_rbtree_node_t *node)
{
    if((tree == NULL) || (node == NULL)) return 0;
    if(ns_rbtree_node_is_empty(node)) return 0;
    if(tree->root == NULL) return 0;
    if((NS_RB_NODE_PARENT(node) == NULL) && (tree->root != node)) return 0;
    return 1;
}

static void ns_rb_rotate_left(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *right = node->right;
    ns_rbtree_node_t *parent = NS_RB_NODE_PARENT(node);

    node->right = right->left;
    if(right->left != NULL) ns_rb_set_parent(right->left, node);

    ns_rb_set_parent(right, parent);
    if(parent == NULL){
        tree->root = right;
    } else if(node == parent->left){
        parent->left = right;
    } else {
        parent->right = right;
    }

    right->left = node;
    ns_rb_set_parent(node, right);
}

static void ns_rb_rotate_right(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *left = node->left;
    ns_rbtree_node_t *parent = NS_RB_NODE_PARENT(node);

    node->left = left->right;
    if(left->right != NULL) ns_rb_set_parent(left->right, node);

    ns_rb_set_parent(left, parent);
    if(parent == NULL){
        tree->root = left;
    } else if(node == parent->right){
        parent->right = left;
    } else {
        parent->left = left;
    }

    left->right = node;
    ns_rb_set_parent(node, left);
}

static void ns_rb_insert_fixup(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    while(ns_rb_is_red(NS_RB_NODE_PARENT(node))){
        ns_rbtree_node_t *parent = NS_RB_NODE_PARENT(node);
        ns_rbtree_node_t *grand = NS_RB_NODE_PARENT(parent);

        if(parent == grand->left){
            ns_rbtree_node_t *uncle = grand->right;
            if(ns_rb_is_red(uncle)){
                ns_rb_set_color(parent, NS_RBTREE_BLACK);
                ns_rb_set_color(uncle, NS_RBTREE_BLACK);
                ns_rb_set_color(grand, NS_RBTREE_RED);
                node = grand;
                continue;
            }

            if(node == parent->right){
                node = parent;
                ns_rb_rotate_left(tree, node);
                parent = NS_RB_NODE_PARENT(node);
                grand = NS_RB_NODE_PARENT(parent);
            }

            ns_rb_set_color(parent, NS_RBTREE_BLACK);
            ns_rb_set_color(grand, NS_RBTREE_RED);
            ns_rb_rotate_right(tree, grand);
        } else {
            ns_rbtree_node_t *uncle = grand->left;
            if(ns_rb_is_red(uncle)){
                ns_rb_set_color(parent, NS_RBTREE_BLACK);
                ns_rb_set_color(uncle, NS_RBTREE_BLACK);
                ns_rb_set_color(grand, NS_RBTREE_RED);
                node = grand;
                continue;
            }

            if(node == parent->left){
                node = parent;
                ns_rb_rotate_right(tree, node);
                parent = NS_RB_NODE_PARENT(node);
                grand = NS_RB_NODE_PARENT(parent);
            }

            ns_rb_set_color(parent, NS_RBTREE_BLACK);
            ns_rb_set_color(grand, NS_RBTREE_RED);
            ns_rb_rotate_left(tree, grand);
        }
    }

    ns_rb_set_color(tree->root, NS_RBTREE_BLACK);
}

static void ns_rb_transplant(ns_rbtree_t *tree, ns_rbtree_node_t *old_node, ns_rbtree_node_t *new_node)
{
    ns_rbtree_node_t *parent = NS_RB_NODE_PARENT(old_node);

    if(parent == NULL){
        tree->root = new_node;
    } else if(old_node == parent->left){
        parent->left = new_node;
    } else {
        parent->right = new_node;
    }

    if(new_node != NULL) ns_rb_set_parent(new_node, parent);
}

/* ns_rb_sibling_left / ns_rb_sibling_right: 取 sibling 的左右子节点，
   sibling 为 NULL 时返回 NULL，避免在调用点重复写三元表达式 */
static ns_rbtree_node_t *ns_rb_sibling_left(ns_rbtree_node_t *sibling)
{
    return (sibling != NULL) ? sibling->left : NULL;
}

static ns_rbtree_node_t *ns_rb_sibling_right(ns_rbtree_node_t *sibling)
{
    return (sibling != NULL) ? sibling->right : NULL;
}

static void ns_rb_delete_fixup(
    ns_rbtree_t *tree,
    ns_rbtree_node_t *node,
    ns_rbtree_node_t *parent)
{
    while((node != tree->root) && ns_rb_is_black(node)){
        ns_rbtree_node_t *sibling;

        if(parent == NULL) break;

        if(node == parent->left){
            sibling = parent->right;

            if(ns_rb_is_red(sibling)){
                ns_rb_set_color(sibling, NS_RBTREE_BLACK);
                ns_rb_set_color(parent, NS_RBTREE_RED);
                ns_rb_rotate_left(tree, parent);
                sibling = parent->right;
            }

            if(ns_rb_is_black(ns_rb_sibling_left(sibling)) &&
               ns_rb_is_black(ns_rb_sibling_right(sibling))){
                if(sibling != NULL) ns_rb_set_color(sibling, NS_RBTREE_RED);
                node = parent;
                parent = NS_RB_NODE_PARENT(node);
            } else {
                if(ns_rb_is_black(ns_rb_sibling_right(sibling))){
                    if(ns_rb_sibling_left(sibling) != NULL){
                        ns_rb_set_color(ns_rb_sibling_left(sibling), NS_RBTREE_BLACK);
                    }
                    if(sibling != NULL){
                        ns_rb_set_color(sibling, NS_RBTREE_RED);
                        ns_rb_rotate_right(tree, sibling);
                    }
                    sibling = parent->right;
                }

                if(sibling != NULL) ns_rb_set_color(sibling, NS_RB_NODE_COLOR(parent));
                ns_rb_set_color(parent, NS_RBTREE_BLACK);
                if(ns_rb_sibling_right(sibling) != NULL){
                    ns_rb_set_color(ns_rb_sibling_right(sibling), NS_RBTREE_BLACK);
                }
                ns_rb_rotate_left(tree, parent);
                node = tree->root;
                parent = NULL;
            }
        } else {
            sibling = parent->left;

            if(ns_rb_is_red(sibling)){
                ns_rb_set_color(sibling, NS_RBTREE_BLACK);
                ns_rb_set_color(parent, NS_RBTREE_RED);
                ns_rb_rotate_right(tree, parent);
                sibling = parent->left;
            }

            if(ns_rb_is_black(ns_rb_sibling_right(sibling)) &&
               ns_rb_is_black(ns_rb_sibling_left(sibling))){
                if(sibling != NULL) ns_rb_set_color(sibling, NS_RBTREE_RED);
                node = parent;
                parent = NS_RB_NODE_PARENT(node);
            } else {
                if(ns_rb_is_black(ns_rb_sibling_left(sibling))){
                    if(ns_rb_sibling_right(sibling) != NULL){
                        ns_rb_set_color(ns_rb_sibling_right(sibling), NS_RBTREE_BLACK);
                    }
                    if(sibling != NULL){
                        ns_rb_set_color(sibling, NS_RBTREE_RED);
                        ns_rb_rotate_left(tree, sibling);
                    }
                    sibling = parent->left;
                }

                if(sibling != NULL) ns_rb_set_color(sibling, NS_RB_NODE_COLOR(parent));
                ns_rb_set_color(parent, NS_RBTREE_BLACK);
                if(ns_rb_sibling_left(sibling) != NULL){
                    ns_rb_set_color(ns_rb_sibling_left(sibling), NS_RBTREE_BLACK);
                }
                ns_rb_rotate_right(tree, parent);
                node = tree->root;
                parent = NULL;
            }
        }
    }

    if(node != NULL) ns_rb_set_color(node, NS_RBTREE_BLACK);
}

/* ---- 后序遍历辅助 ---- */

static ns_rbtree_node_t *ns_rb_first_postorder(const ns_rbtree_node_t *root)
{
    if(root == NULL) return NULL;

    for(;;){
        while(root->left != NULL) root = root->left;
        if(root->right == NULL) break;
        root = root->right;
    }
    return (ns_rbtree_node_t *)root;
}

static ns_rbtree_node_t *ns_rb_next_postorder(const ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *parent = NS_RB_NODE_PARENT(node);

    if(parent == NULL) return NULL;

    if((node == parent->left) && (parent->right != NULL)){
        return ns_rb_first_postorder(parent->right);
    }
    return parent;
}

/* ---- 公开接口实现 ---- */

void ns_rbtree_init(ns_rbtree_t *tree, int (*cmp)(const ns_rbtree_node_t *, const ns_rbtree_node_t *))
{
    if(tree == NULL) return;

    tree->root = NULL;
    tree->leftmost = NULL;
    tree->size = 0u;
    tree->cmp = cmp;
}

void ns_rbtree_node_init(ns_rbtree_node_t *node)
{
    if(node == NULL) return;

    node->parent_and_color = (uintptr_t)node;
    node->left = NULL;
    node->right = NULL;
}

int ns_rbtree_empty(const ns_rbtree_t *tree)
{
    if(tree == NULL) return 1;

    return tree->root == NULL;
}

int ns_rbtree_node_is_linked(const ns_rbtree_node_t *node)
{
    return (node != NULL) && !ns_rbtree_node_is_empty(node);
}

ns_rbtree_node_t *ns_rbtree_insert(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *parent = NULL;
    ns_rbtree_node_t **link;

    if((tree == NULL) || (node == NULL) || ns_rbtree_node_is_linked(node)){
        return NULL;
    }

    link = &tree->root;

    while(*link != NULL){
        parent = *link;
        if(tree->cmp(node, parent) < 0){
            link = &parent->left;
        } else {
            link = &parent->right;
        }
    }

    ns_rb_set_parent(node, parent);
    node->left = NULL;
    node->right = NULL;
    ns_rb_set_color(node, NS_RBTREE_RED);
    *link = node;

    if((tree->leftmost == NULL) || (tree->cmp(node, tree->leftmost) < 0)){
        tree->leftmost = node;
    }

    ++tree->size;
    ns_rb_insert_fixup(tree, node);
    return node;
}

ns_rbtree_node_t *ns_rbtree_remove(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *child;
    ns_rbtree_node_t *fix_parent;
    ns_rbtree_node_t *successor;
    int original_color;

    if(!ns_rb_node_can_remove(tree, node)) return NULL;

    successor = node;
    /* original_color 记录被移除的"那个槽位"在操作前的颜色：
       - 单子节点分支：successor == node，其原始颜色决定是否破坏黑高
       - 双子节点分支：successor == node 中序后继，其原始颜色决定是否破坏黑高
       后续 if(RED) 不修复，否则调用 delete_fixup 补偿 */
    original_color = NS_RB_NODE_COLOR(successor);

    if(node->left == NULL){
        child = node->right;
        fix_parent = NS_RB_NODE_PARENT(node);
        ns_rb_transplant(tree, node, node->right);
    } else if(node->right == NULL){
        child = node->left;
        fix_parent = NS_RB_NODE_PARENT(node);
        ns_rb_transplant(tree, node, node->left);
    } else {
        successor = ns_rb_minimum(node->right);
        original_color = NS_RB_NODE_COLOR(successor);
        child = successor->right;

        if(NS_RB_NODE_PARENT(successor) == node){
            fix_parent = successor;
            if(child != NULL) ns_rb_set_parent(child, successor);
        } else {
            fix_parent = NS_RB_NODE_PARENT(successor);
            ns_rb_transplant(tree, successor, successor->right);
            successor->right = node->right;
            ns_rb_set_parent(successor->right, successor);
        }

        ns_rb_transplant(tree, node, successor);
        successor->left = node->left;
        ns_rb_set_parent(successor->left, successor);
        /* successor 继承 node 的颜色，以保持红黑不变量。
           此处只复制颜色 bit，不影响 original_color 的判断逻辑 */
        ns_rb_set_color(successor, NS_RB_NODE_COLOR(node));
    }

    if(original_color == NS_RBTREE_BLACK) ns_rb_delete_fixup(tree, child, fix_parent);

    if(tree->leftmost == node){
        tree->leftmost = ns_rb_minimum(tree->root);
    }
    if(tree->size != 0u) --tree->size;

    /* 将 node 重置为空状态 */
    ns_rbtree_node_init(node);
    return node;
}

ns_rbtree_node_t *ns_rbtree_first(const ns_rbtree_t *tree)
{
    if(tree == NULL) return NULL;

    return tree->leftmost;
}

ns_rbtree_node_t *ns_rbtree_last(const ns_rbtree_t *tree)
{
    if(tree == NULL) return NULL;

    return ns_rb_maximum(tree->root);
}

ns_rbtree_node_t *ns_rbtree_next(const ns_rbtree_node_t *node)
{
    const ns_rbtree_node_t *cursor = node;
    ns_rbtree_node_t *parent;

    if((node == NULL) || !ns_rbtree_node_is_linked(node)){
        return NULL;
    }

    if(node->right != NULL) return ns_rb_minimum(node->right);

    parent = NS_RB_NODE_PARENT(node);
    while((parent != NULL) && (cursor == parent->right)){
        cursor = parent;
        parent = NS_RB_NODE_PARENT(parent);
    }

    return parent;
}

ns_rbtree_node_t *ns_rbtree_prev(const ns_rbtree_node_t *node)
{
    const ns_rbtree_node_t *cursor = node;
    ns_rbtree_node_t *parent;

    if((node == NULL) || !ns_rbtree_node_is_linked(node)){
        return NULL;
    }

    if(node->left != NULL) return ns_rb_maximum(node->left);

    parent = NS_RB_NODE_PARENT(node);
    while((parent != NULL) && (cursor == parent->left)){
        cursor = parent;
        parent = NS_RB_NODE_PARENT(parent);
    }

    return parent;
}

ns_rbtree_node_t *ns_rbtree_find_first(const ns_rbtree_node_t *key, const ns_rbtree_t *tree)
{
    ns_rbtree_node_t *cursor;
    ns_rbtree_node_t *match = NULL;
    int c;

    if((key == NULL) || (tree == NULL) || (tree->cmp == NULL)) return NULL;

    cursor = tree->root;
    while(cursor != NULL){
        c = tree->cmp(key, cursor);
        if(c <= 0){
            if(c == 0) match = cursor;
            cursor = cursor->left;
        } else {
            cursor = cursor->right;
        }
    }

    return match;
}

ns_rbtree_node_t *ns_rbtree_next_match(
    const ns_rbtree_node_t *key, ns_rbtree_node_t *node, const ns_rbtree_t *tree)
{
    ns_rbtree_node_t *next;

    if((key == NULL) || (node == NULL) || (tree == NULL)) return NULL;

    next = ns_rbtree_next(node);
    while(next != NULL){
        int c = tree->cmp(key, next);
        if(c == 0) return next;
        /* 如果 next > key，中序后续只会更大，无需继续 */
        if(c < 0) break;
        next = ns_rbtree_next(next);
    }

    return NULL;
}

ns_rbtree_node_t *ns_rbtree_find_add(ns_rbtree_node_t *node, ns_rbtree_t *tree)
{
    ns_rbtree_node_t *existing;

    if((node == NULL) || (tree == NULL)) return NULL;

    existing = ns_rbtree_find_first(node, tree);
    if(existing != NULL) return existing;

    ns_rbtree_insert(tree, node);
    return NULL;
}

ns_rbtree_node_t *ns_rbtree_find_new_add(
    const void *key, ns_rbtree_t *tree,
    int (*match)(const void *key, const ns_rbtree_node_t *node),
    void *user_data,
    ns_rbtree_node_t *(*new_node)(void *user_data))
{
    ns_rbtree_node_t *parent = NULL;
    ns_rbtree_node_t **link;
    int c;
    ns_rbtree_node_t *node;

    if((key == NULL) || (tree == NULL) || (match == NULL) || (new_node == NULL)) return NULL;

    /* 查找 */
    link = &tree->root;
    while(*link != NULL){
        parent = *link;
        c = match(key, parent);
        if(c < 0){
            link = &parent->left;
        } else if(c > 0){
            link = &parent->right;
        } else {
            return parent; /* 找到 */
        }
    }

    /* 未找到，新建并插入 */
    node = new_node(user_data);
    if(node == NULL) return NULL;

    ns_rb_set_parent(node, parent);
    node->left = NULL;
    node->right = NULL;
    ns_rb_set_color(node, NS_RBTREE_RED);
    *link = node;

    if((tree->leftmost == NULL) || (tree->cmp(node, tree->leftmost) < 0)){
        tree->leftmost = node;
    }

    ++tree->size;
    ns_rb_insert_fixup(tree, node);
    return node;
}

ns_rbtree_node_t *ns_rbtree_match_find(
    const void *key, const ns_rbtree_t *tree,
    int (*match)(const void *key, const ns_rbtree_node_t *node))
{
    ns_rbtree_node_t *cursor;
    int c;

    if((key == NULL) || (tree == NULL) || (match == NULL)) return NULL;

    cursor = tree->root;
    while(cursor != NULL){
        c = match(key, cursor);
        if(c < 0){
            cursor = cursor->left;
        } else if(c > 0){
            cursor = cursor->right;
        } else {
            return cursor;
        }
    }

    return NULL;
}

ns_rbtree_node_t *ns_rbtree_first_postorder(const ns_rbtree_t *tree)
{
    if(tree == NULL) return NULL;

    return ns_rb_first_postorder(tree->root);
}

ns_rbtree_node_t *ns_rbtree_next_postorder(const ns_rbtree_node_t *node)
{
    if(node == NULL) return NULL;

    return ns_rb_next_postorder(node);
}
