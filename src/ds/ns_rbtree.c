/**
 * @file ns_rbtree.c
 * @brief nanosig uint64 key intrusive 红黑树实现。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_rbtree.h>

static int ns_rb_is_red(const ns_rbtree_node_t *node)
{
    return (node != NULL) && (node->color == NS_RBTREE_RED);
}

static int ns_rb_is_black(const ns_rbtree_node_t *node)
{
    return !ns_rb_is_red(node);
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
    if(node->parent == node) return 0;
    if(tree->root == NULL) return 0;
    if((node->parent == NULL) && (tree->root != node)) return 0;
    return 1;
}

static void ns_rb_rotate_left(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *right = node->right;

    node->right = right->left;
    if(right->left != NULL) right->left->parent = node;

    right->parent = node->parent;
    if(node->parent == NULL){
        tree->root = right;
    } else if(node == node->parent->left){
        node->parent->left = right;
    } else {
        node->parent->right = right;
    }

    right->left = node;
    node->parent = right;
}

static void ns_rb_rotate_right(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *left = node->left;

    node->left = left->right;
    if(left->right != NULL) left->right->parent = node;

    left->parent = node->parent;
    if(node->parent == NULL){
        tree->root = left;
    } else if(node == node->parent->right){
        node->parent->right = left;
    } else {
        node->parent->left = left;
    }

    left->right = node;
    node->parent = left;
}

static void ns_rb_insert_fixup(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    while(ns_rb_is_red(node->parent)){
        ns_rbtree_node_t *parent = node->parent;
        ns_rbtree_node_t *grand = parent->parent;

        if(parent == grand->left){
            ns_rbtree_node_t *uncle = grand->right;
            if(ns_rb_is_red(uncle)){
                parent->color = NS_RBTREE_BLACK;
                uncle->color = NS_RBTREE_BLACK;
                grand->color = NS_RBTREE_RED;
                node = grand;
                continue;
            }

            if(node == parent->right){
                node = parent;
                ns_rb_rotate_left(tree, node);
                parent = node->parent;
                grand = parent->parent;
            }

            parent->color = NS_RBTREE_BLACK;
            grand->color = NS_RBTREE_RED;
            ns_rb_rotate_right(tree, grand);
        } else {
            ns_rbtree_node_t *uncle = grand->left;
            if(ns_rb_is_red(uncle)){
                parent->color = NS_RBTREE_BLACK;
                uncle->color = NS_RBTREE_BLACK;
                grand->color = NS_RBTREE_RED;
                node = grand;
                continue;
            }

            if(node == parent->left){
                node = parent;
                ns_rb_rotate_right(tree, node);
                parent = node->parent;
                grand = parent->parent;
            }

            parent->color = NS_RBTREE_BLACK;
            grand->color = NS_RBTREE_RED;
            ns_rb_rotate_left(tree, grand);
        }
    }

    tree->root->color = NS_RBTREE_BLACK;
}

static void ns_rb_transplant(ns_rbtree_t *tree, ns_rbtree_node_t *old_node, ns_rbtree_node_t *new_node)
{
    if(old_node->parent == NULL){
        tree->root = new_node;
    } else if(old_node == old_node->parent->left){
        old_node->parent->left = new_node;
    } else {
        old_node->parent->right = new_node;
    }

    if(new_node != NULL) new_node->parent = old_node->parent;
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
                sibling->color = NS_RBTREE_BLACK;
                parent->color = NS_RBTREE_RED;
                ns_rb_rotate_left(tree, parent);
                sibling = parent->right;
            }

            if(ns_rb_is_black(sibling == NULL ? NULL : sibling->left) &&
               ns_rb_is_black(sibling == NULL ? NULL : sibling->right)){
                if(sibling != NULL) sibling->color = NS_RBTREE_RED;
                node = parent;
                parent = node->parent;
            } else {
                if(ns_rb_is_black(sibling == NULL ? NULL : sibling->right)){
                    if((sibling != NULL) && (sibling->left != NULL)){
                        sibling->left->color = NS_RBTREE_BLACK;
                    }
                    if(sibling != NULL){
                        sibling->color = NS_RBTREE_RED;
                        ns_rb_rotate_right(tree, sibling);
                    }
                    sibling = parent->right;
                }

                if(sibling != NULL) sibling->color = parent->color;
                parent->color = NS_RBTREE_BLACK;
                if((sibling != NULL) && (sibling->right != NULL)){
                    sibling->right->color = NS_RBTREE_BLACK;
                }
                ns_rb_rotate_left(tree, parent);
                node = tree->root;
                parent = NULL;
            }
        } else {
            sibling = parent->left;

            if(ns_rb_is_red(sibling)){
                sibling->color = NS_RBTREE_BLACK;
                parent->color = NS_RBTREE_RED;
                ns_rb_rotate_right(tree, parent);
                sibling = parent->left;
            }

            if(ns_rb_is_black(sibling == NULL ? NULL : sibling->right) &&
               ns_rb_is_black(sibling == NULL ? NULL : sibling->left)){
                if(sibling != NULL) sibling->color = NS_RBTREE_RED;
                node = parent;
                parent = node->parent;
            } else {
                if(ns_rb_is_black(sibling == NULL ? NULL : sibling->left)){
                    if((sibling != NULL) && (sibling->right != NULL)){
                        sibling->right->color = NS_RBTREE_BLACK;
                    }
                    if(sibling != NULL){
                        sibling->color = NS_RBTREE_RED;
                        ns_rb_rotate_left(tree, sibling);
                    }
                    sibling = parent->left;
                }

                if(sibling != NULL) sibling->color = parent->color;
                parent->color = NS_RBTREE_BLACK;
                if((sibling != NULL) && (sibling->left != NULL)){
                    sibling->left->color = NS_RBTREE_BLACK;
                }
                ns_rb_rotate_right(tree, parent);
                node = tree->root;
                parent = NULL;
            }
        }
    }

    if(node != NULL) node->color = NS_RBTREE_BLACK;
}

void ns_rbtree_init(ns_rbtree_t *tree)
{
    if(tree == NULL) return;

    tree->root = NULL;
    tree->leftmost = NULL;
    tree->size = 0u;
}

void ns_rbtree_node_init(ns_rbtree_node_t *node, uint64_t key)
{
    if(node == NULL) return;

    node->parent = node;
    node->left = NULL;
    node->right = NULL;
    node->key = key;
    node->color = NS_RBTREE_BLACK;
}

int ns_rbtree_empty(const ns_rbtree_t *tree)
{
    if(tree == NULL) return 1;

    return tree->root == NULL;
}

int ns_rbtree_node_is_linked(const ns_rbtree_node_t *node)
{
    return (node != NULL) && (node->parent != node);
}

void ns_rbtree_insert(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *parent = NULL;
    ns_rbtree_node_t **link;

    if((tree == NULL) || (node == NULL) || ns_rbtree_node_is_linked(node)){
        return;
    }

    link = &tree->root;

    while(*link != NULL){
        parent = *link;
        if(node->key < parent->key){
            link = &parent->left;
        } else {
            link = &parent->right;
        }
    }

    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    node->color = NS_RBTREE_RED;
    *link = node;

    if((tree->leftmost == NULL) || (node->key < tree->leftmost->key)){
        tree->leftmost = node;
    }

    ++tree->size;
    ns_rb_insert_fixup(tree, node);
}

void ns_rbtree_remove(ns_rbtree_t *tree, ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *child;
    ns_rbtree_node_t *fix_parent;
    ns_rbtree_node_t *successor;
    ns_rbtree_color_t original_color;
    uint64_t old_key;

    if(!ns_rb_node_can_remove(tree, node)) return;

    old_key = node->key;
    successor = node;
    original_color = successor->color;

    if(node->left == NULL){
        child = node->right;
        fix_parent = node->parent;
        ns_rb_transplant(tree, node, node->right);
    } else if(node->right == NULL){
        child = node->left;
        fix_parent = node->parent;
        ns_rb_transplant(tree, node, node->left);
    } else {
        successor = ns_rb_minimum(node->right);
        original_color = successor->color;
        child = successor->right;

        if(successor->parent == node){
            fix_parent = successor;
            if(child != NULL) child->parent = successor;
        } else {
            fix_parent = successor->parent;
            ns_rb_transplant(tree, successor, successor->right);
            successor->right = node->right;
            successor->right->parent = successor;
        }

        ns_rb_transplant(tree, node, successor);
        successor->left = node->left;
        successor->left->parent = successor;
        successor->color = node->color;
    }

    if(original_color == NS_RBTREE_BLACK) ns_rb_delete_fixup(tree, child, fix_parent);

    if((tree->leftmost == node) ||
       ((tree->leftmost != NULL) && (old_key == tree->leftmost->key))){
        tree->leftmost = ns_rb_minimum(tree->root);
    }
    if(tree->size != 0u) --tree->size;
    ns_rbtree_node_init(node, old_key);
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

    parent = node->parent;
    while((parent != NULL) && (cursor == parent->right)){
        cursor = parent;
        parent = parent->parent;
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

    parent = node->parent;
    while((parent != NULL) && (cursor == parent->left)){
        cursor = parent;
        parent = parent->parent;
    }

    return parent;
}

ns_rbtree_node_t *ns_rbtree_find_first(const ns_rbtree_t *tree, uint64_t key)
{
    ns_rbtree_node_t *cursor;
    ns_rbtree_node_t *match = NULL;

    if(tree == NULL) return NULL;

    cursor = tree->root;
    while(cursor != NULL){
        if(key <= cursor->key){
            if(key == cursor->key) match = cursor;
            cursor = cursor->left;
        } else {
            cursor = cursor->right;
        }
    }

    return match;
}
