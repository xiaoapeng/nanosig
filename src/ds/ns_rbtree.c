/**
 * @file ns_rbtree.c
 * @brief nanosig intrusive 红黑树实现。
 * @date 2026-07-06
 *
 * 移植自 eventhub_os eh_rbtree.c，仅修改 API 前缀 (eh_ → ns_)，逻辑未变。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stddef.h>
#include <stdbool.h>
#include <nanosig/nanosig_rbtree.h>

static inline void ns_rb_set_parent(ns_rbtree_node_t *rb, ns_rbtree_node_t *p)
{
    rb->parent_and_color = ns_rb_color(rb) + (ns_rb_parent_node_t)p;
}

static inline void ns_rb_set_parent_color(ns_rbtree_node_t *rb,
                       ns_rbtree_node_t *p, int color)
{
    rb->parent_and_color = (ns_rb_parent_node_t)p + (ns_rb_parent_node_t)color;
}

static inline void __ns_rb_change_child(ns_rbtree_node_t *old, ns_rbtree_node_t *new,
          ns_rbtree_node_t *parent, ns_rbtree_t *root)
{
    if (parent) {
        if (parent->rb_left == old)
            parent->rb_left = new;
        else
            parent->rb_right = new;
    } else
        root->rb_node = new;
}

static ns_rbtree_node_t *ns_rb_left_deepest_node(const ns_rbtree_node_t *node)
{
    for (;;) {
        if (node->rb_left)
            node = node->rb_left;
        else if (node->rb_right)
            node = node->rb_right;
        else
            return (ns_rbtree_node_t *)node;
    }
}

static inline void ns_rb_set_black(ns_rbtree_node_t *rb)
{
    rb->parent_and_color |= NS_RBTREE_BLACK;
}

static inline ns_rbtree_node_t *ns_rb_red_parent(ns_rbtree_node_t *red)
{
    return (ns_rbtree_node_t *)red->parent_and_color;
}

static inline void ns_rb_link_node(ns_rbtree_node_t *node, ns_rbtree_node_t *parent,
                ns_rbtree_node_t **rb_link)
{
    node->parent_and_color = (ns_rb_parent_node_t)parent;
    node->rb_left = node->rb_right = NULL;

    *rb_link = node;
}


/*
 * Helper function for rotations:
 * - old's parent and color get assigned to new
 * - old gets assigned new as a parent and 'color' as a color.
 */
static inline void
__ns_rb_rotate_set_parents(ns_rbtree_node_t *old, ns_rbtree_node_t *new,
            ns_rbtree_t *root, int color)
{
    ns_rbtree_node_t *parent = ns_rb_parent(old);
    new->parent_and_color = old->parent_and_color;
    ns_rb_set_parent_color(old, new, color);
    __ns_rb_change_child(old, new, parent, root);
}

static void
__ns_rb_insert(ns_rbtree_node_t *node, ns_rbtree_t *root)
{
    ns_rbtree_node_t *parent = ns_rb_red_parent(node), *gparent, *tmp;

    while (true) {
        /*
         * Loop invariant: node is red.
         */
        if (NS_UNLIKELY(!parent)) {
            /*
             * The inserted node is root. Either this is the
             * first node, or we recursed at Case 1 below and
             * are no longer violating 4).
             */
            ns_rb_set_parent_color(node, NULL, NS_RBTREE_BLACK);
            break;
        }

        /*
         * If there is a black parent, we are done.
         * Otherwise, take some corrective action as,
         * per 4), we don't want a red root or two
         * consecutive red nodes.
         */
        if(ns_rb_is_black(parent))
            break;

        gparent = ns_rb_red_parent(parent);

        tmp = gparent->rb_right;
        if (parent != tmp) {    /* parent == gparent->rb_left */
            if (tmp && ns_rb_is_red(tmp)) {
                /*
                 * Case 1 - node's uncle is red (color flips).
                 *
                 *       G            g
                 *      / \          / \
                 *     p   u  -->   P   U
                 *    /            /
                 *   n            n
                 *
                 * However, since g's parent might be red, and
                 * 4) does not allow this, we need to recurse
                 * at g.
                 */
                ns_rb_set_parent_color(tmp, gparent, NS_RBTREE_BLACK);
                ns_rb_set_parent_color(parent, gparent, NS_RBTREE_BLACK);
                node = gparent;
                parent = ns_rb_parent(node);
                ns_rb_set_parent_color(node, parent, NS_RBTREE_RED);
                continue;
            }

            tmp = parent->rb_right;
            if (node == tmp) {
                /*
                 * Case 2 - node's uncle is black and node is
                 * the parent's right child (left rotate at parent).
                 *
                 *      G             G
                 *     / \           / \
                 *    p   U  -->    n   U
                 *     \           /
                 *      n         p
                 *
                 * This still leaves us in violation of 4), the
                 * continuation into Case 3 will fix that.
                 */
                tmp = node->rb_left;
                parent->rb_right = tmp;
                node->rb_left = parent;
                if (tmp)
                    ns_rb_set_parent_color(tmp, parent,
                                NS_RBTREE_BLACK);
                ns_rb_set_parent_color(parent, node, NS_RBTREE_RED);
                parent = node;
                tmp = node->rb_right;
            }

            /*
             * Case 3 - node's uncle is black and node is
             * the parent's left child (right rotate at gparent).
             *
             *        G           P
             *       / \         / \
             *      p   U  -->  n   g
             *     /                 \
             *    n                   U
             */
            gparent->rb_left = tmp;
            parent->rb_right = gparent;
            if (tmp)
                ns_rb_set_parent_color(tmp, gparent, NS_RBTREE_BLACK);
            __ns_rb_rotate_set_parents(gparent, parent, root, NS_RBTREE_RED);
            break;
        } else {
            tmp = gparent->rb_left;
            if (tmp && ns_rb_is_red(tmp)) {
                /* Case 1 - color flips */
                ns_rb_set_parent_color(tmp, gparent, NS_RBTREE_BLACK);
                ns_rb_set_parent_color(parent, gparent, NS_RBTREE_BLACK);
                node = gparent;
                parent = ns_rb_parent(node);
                ns_rb_set_parent_color(node, parent, NS_RBTREE_RED);
                continue;
            }

            tmp = parent->rb_left;
            if (node == tmp) {
                /* Case 2 - right rotate at parent */
                tmp = node->rb_right;
                parent->rb_left = tmp;
                node->rb_right = parent;
                if (tmp)
                    ns_rb_set_parent_color(tmp, parent,
                                NS_RBTREE_BLACK);
                ns_rb_set_parent_color(parent, node, NS_RBTREE_RED);
                parent = node;
                tmp = node->rb_left;
            }

            /* Case 3 - left rotate at gparent */
            gparent->rb_right = tmp;
            parent->rb_left = gparent;
            if (tmp)
                ns_rb_set_parent_color(tmp, gparent, NS_RBTREE_BLACK);
            __ns_rb_rotate_set_parents(gparent, parent, root, NS_RBTREE_RED);
            break;
        }
    }
}

/*
 * Inline version for ns_rbtree_del() use - we want to be able to inline
 * and eliminate the dummy_rotate callback there
 */
static void
____ns_rb_erase_color(ns_rbtree_node_t *parent, ns_rbtree_t *root)
{
    ns_rbtree_node_t *node = NULL, *sibling, *tmp1, *tmp2;

    while (true) {
        /*
         * Loop invariants:
         * - node is black (or NULL on first iteration)
         * - node is not the root (parent is not NULL)
         * - All leaf paths going through parent and node have a
         *   black node count that is 1 lower than other leaf paths.
         */
        sibling = parent->rb_right;
        if (node != sibling) {    /* node == parent->rb_left */
            if (ns_rb_is_red(sibling)) {
                /*
                 * Case 1 - left rotate at parent
                 *
                 *     P               S
                 *    / \             / \
                 *   N   s    -->    p   Sr
                 *      / \         / \
                 *     Sl  Sr      N   Sl
                 */
                tmp1 = sibling->rb_left;
                parent->rb_right = tmp1;
                sibling->rb_left = parent;
                ns_rb_set_parent_color(tmp1, parent, NS_RBTREE_BLACK);
                __ns_rb_rotate_set_parents(parent, sibling, root,
                            NS_RBTREE_RED);
                sibling = tmp1;
            }
            tmp1 = sibling->rb_right;
            if (!tmp1 || ns_rb_is_black(tmp1)) {
                tmp2 = sibling->rb_left;
                if (!tmp2 || ns_rb_is_black(tmp2)) {
                    /*
                     * Case 2 - sibling color flip
                     * (p could be either color here)
                     *
                     *    (p)           (p)
                     *    / \           / \
                     *   N   S    -->  N   s
                     *      / \           / \
                     *     Sl  Sr        Sl  Sr
                     *
                     * This leaves us violating 5) which
                     * can be fixed by flipping p to black
                     * if it was red, or by recursing at p.
                     * p is red when coming from Case 1.
                     */
                    ns_rb_set_parent_color(sibling, parent,
                                NS_RBTREE_RED);
                    if (ns_rb_is_red(parent))
                        ns_rb_set_black(parent);
                    else {
                        node = parent;
                        parent = ns_rb_parent(node);
                        if (parent)
                            continue;
                    }
                    break;
                }
                /*
                 * Case 3 - right rotate at sibling
                 * (p could be either color here)
                 *
                 *   (p)           (p)
                 *   / \           / \
                 *  N   S    -->  N   sl
                 *     / \             \
                 *    sl  Sr            S
                 *                       \
                 *                        Sr
                 *
                 * Note: p might be red, and then both
                 * p and sl are red after rotation(which
                 * breaks property 4). This is fixed in
                 * Case 4 (in __ns_rb_rotate_set_parents()
                 *         which set sl the color of p
                 *         and set p NS_RBTREE_BLACK)
                 *
                 *   (p)            (sl)
                 *   / \            /  \
                 *  N   sl   -->   P    S
                 *       \        /      \
                 *        S      N        Sr
                 *         \
                 *          Sr
                 */
                tmp1 = tmp2->rb_right;
                sibling->rb_left = tmp1;
                tmp2->rb_right = sibling;
                parent->rb_right = tmp2;
                if (tmp1)
                    ns_rb_set_parent_color(tmp1, sibling,
                                NS_RBTREE_BLACK);
                tmp1 = sibling;
                sibling = tmp2;
            }
            /*
             * Case 4 - left rotate at parent + color flips
             * (p and sl could be either color here.
             *  After rotation, p becomes black, s acquires
             *  p's color, and sl keeps its color)
             *
             *      (p)             (s)
             *      / \             / \
             *     N   S     -->   P   Sr
             *        / \         / \
             *      (sl) sr      N  (sl)
             */
            tmp2 = sibling->rb_left;
            parent->rb_right = tmp2;
            sibling->rb_left = parent;
            ns_rb_set_parent_color(tmp1, sibling, NS_RBTREE_BLACK);
            if (tmp2)
                ns_rb_set_parent(tmp2, parent);
            __ns_rb_rotate_set_parents(parent, sibling, root,
                        NS_RBTREE_BLACK);
            break;
        } else {
            sibling = parent->rb_left;
            if (ns_rb_is_red(sibling)) {
                /* Case 1 - right rotate at parent */
                tmp1 = sibling->rb_right;
                parent->rb_left = tmp1;
                sibling->rb_right = parent;
                ns_rb_set_parent_color(tmp1, parent, NS_RBTREE_BLACK);
                __ns_rb_rotate_set_parents(parent, sibling, root,
                            NS_RBTREE_RED);
                sibling = tmp1;
            }
            tmp1 = sibling->rb_left;
            if (!tmp1 || ns_rb_is_black(tmp1)) {
                tmp2 = sibling->rb_right;
                if (!tmp2 || ns_rb_is_black(tmp2)) {
                    /* Case 2 - sibling color flip */
                    ns_rb_set_parent_color(sibling, parent,
                                NS_RBTREE_RED);
                    if (ns_rb_is_red(parent))
                        ns_rb_set_black(parent);
                    else {
                        node = parent;
                        parent = ns_rb_parent(node);
                        if (parent)
                            continue;
                    }
                    break;
                }
                /* Case 3 - left rotate at sibling */
                tmp1 = tmp2->rb_left;
                sibling->rb_right = tmp1;
                tmp2->rb_left = sibling;
                parent->rb_left = tmp2;
                if (tmp1)
                    ns_rb_set_parent_color(tmp1, sibling,
                                NS_RBTREE_BLACK);
                tmp1 = sibling;
                sibling = tmp2;
            }
            /* Case 4 - right rotate at parent + color flips */
            tmp2 = sibling->rb_right;
            parent->rb_left = tmp2;
            sibling->rb_right = parent;
            ns_rb_set_parent_color(tmp1, sibling, NS_RBTREE_BLACK);
            if (tmp2)
                ns_rb_set_parent(tmp2, parent);
            __ns_rb_rotate_set_parents(parent, sibling, root,
                        NS_RBTREE_BLACK);
            break;
        }
    }
}

static ns_rbtree_node_t *
__ns_rb_del(ns_rbtree_node_t *node, ns_rbtree_t *root)
{
    ns_rbtree_node_t *child = node->rb_right;
    ns_rbtree_node_t *tmp = node->rb_left;
    ns_rbtree_node_t *parent, *rebalance;
    ns_rb_parent_node_t pc;

    if (!tmp) {
        /*
         * Case 1: node to erase has no more than 1 child (easy!)
         *
         * Note that if there is one child it must be red due to 5)
         * and node must be black due to 4). We adjust colors locally
         * so as to bypass __ns_rb_erase_color() later on.
         */
        pc = node->parent_and_color;
        parent = __ns_rb_parent(pc);
        __ns_rb_change_child(node, child, parent, root);
        if (child) {
            child->parent_and_color = pc;
            rebalance = NULL;
        } else
            rebalance = __ns_rb_is_black(pc) ? parent : NULL;
        tmp = parent;
    } else if (!child) {
        /* Still case 1, but this time the child is node->rb_left */
        tmp->parent_and_color = pc = node->parent_and_color;
        parent = __ns_rb_parent(pc);
        __ns_rb_change_child(node, tmp, parent, root);
        rebalance = NULL;
        tmp = parent;
    } else {
        ns_rbtree_node_t *successor = child, *child2;

        tmp = child->rb_left;
        if (!tmp) {
            /*
             * Case 2: node's successor is its right child
             *
             *    (n)          (s)
             *    / \          / \
             *  (x) (s)  ->  (x) (c)
             *        \
             *        (c)
             */
            parent = successor;
            child2 = successor->rb_right;

        } else {
            /*
             * Case 3: node's successor is leftmost under
             * node's right child subtree
             *
             *    (n)          (s)
             *    / \          / \
             *  (x) (y)  ->  (x) (y)
             *      /            /
             *    (p)          (p)
             *    /            /
             *  (s)          (c)
             *    \
             *    (c)
             */
            do {
                parent = successor;
                successor = tmp;
                tmp = tmp->rb_left;
            } while (tmp);
            child2 = successor->rb_right;
            parent->rb_left = child2;
            successor->rb_right = child;
            ns_rb_set_parent(child, successor);
        }

        tmp = node->rb_left;
        successor->rb_left = tmp;
        ns_rb_set_parent(tmp, successor);

        pc = node->parent_and_color;
        tmp = __ns_rb_parent(pc);
        __ns_rb_change_child(node, successor, tmp, root);

        if (child2) {
            ns_rb_set_parent_color(child2, parent, NS_RBTREE_BLACK);
            rebalance = NULL;
        } else {
            rebalance = ns_rb_is_black(successor) ? parent : NULL;
        }
        successor->parent_and_color = pc;
        tmp = successor;
    }

    return rebalance;
}
/* Non-inline version for ns_rb_erase_augmented() use */
void __ns_rb_erase_color(ns_rbtree_node_t *parent, ns_rbtree_t *root)
{
    ____ns_rb_erase_color(parent, root);
}


static void ns_rb_insert_color(ns_rbtree_node_t *node, ns_rbtree_t *root)
{
    __ns_rb_insert(node, root);
}

ns_rbtree_node_t * ns_rbtree_del(ns_rbtree_node_t *node, ns_rbtree_t *root)
{
    ns_rbtree_node_t *rebalance;
    ns_rbtree_node_t *leftmost = NULL;

    if (root->rb_leftmost == node)
        leftmost = root->rb_leftmost = ns_rbtree_next(node);
    rebalance = __ns_rb_del(node, root);
    if (rebalance)
        ____ns_rb_erase_color(rebalance, root);
    ns_rbtree_node_init(node);
    return leftmost;
}

ns_rbtree_node_t *ns_rbtree_last(const ns_rbtree_t *root)
{
    ns_rbtree_node_t    *n;

    n = root->rb_node;
    if (!n)
        return NULL;
    while (n->rb_right)
        n = n->rb_right;
    return n;
}

ns_rbtree_node_t *ns_rbtree_next(const ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *parent;

    if (ns_rbtree_node_is_empty(node))
        return NULL;

    /*
     * If we have a right-hand child, go down and then left as far
     * as we can.
     */
    if (node->rb_right) {
        node = node->rb_right;
        while (node->rb_left)
            node = node->rb_left;
        return (ns_rbtree_node_t *)node;
    }

    /*
     * No right-hand children. Everything down and left is smaller than us,
     * so any 'next' node must be in the general direction of our parent.
     * Go up the tree; any time the ancestor is a right-hand child of its
     * parent, keep going up. First time it's a left-hand child of its
     * parent, said parent is our 'next' node.
     */
    while ((parent = ns_rb_parent(node)) && node == parent->rb_right)
        node = parent;

    return parent;
}

ns_rbtree_node_t *ns_rbtree_prev(const ns_rbtree_node_t *node)
{
    ns_rbtree_node_t *parent;

    if (ns_rbtree_node_is_empty(node))
        return NULL;

    /*
     * If we have a left-hand child, go down and then right as far
     * as we can.
     */
    if (node->rb_left) {
        node = node->rb_left;
        while (node->rb_right)
            node = node->rb_right;
        return (ns_rbtree_node_t *)node;
    }

    /*
     * No left-hand children. Go up till we find an ancestor which
     * is a right-hand child of its parent.
     */
    while ((parent = ns_rb_parent(node)) && node == parent->rb_left)
        node = parent;

    return parent;
}

void ns_rb_replace_node(ns_rbtree_node_t *victim, ns_rbtree_node_t *new,
             ns_rbtree_t *root)
{
    ns_rbtree_node_t *parent = ns_rb_parent(victim);

    /* Copy the pointers/colour from the victim to the replacement */
    *new = *victim;

    /* Set the surrounding nodes to point to the replacement */
    if (victim->rb_left)
        ns_rb_set_parent(victim->rb_left, new);
    if (victim->rb_right)
        ns_rb_set_parent(victim->rb_right, new);
    __ns_rb_change_child(victim, new, parent, root);
}


ns_rbtree_node_t * ns_rbtree_add(ns_rbtree_node_t *node, ns_rbtree_t *tree)
{
    ns_rbtree_node_t **link = &tree->rb_node;
    ns_rbtree_node_t *parent = NULL;
    bool leftmost = true;

    while (*link) {
        parent = *link;
        if (tree->cmp(node, parent) < 0) {
            link = &parent->rb_left;
        } else {
            link = &parent->rb_right;
            leftmost = false;
        }
    }

    ns_rb_link_node(node, parent, link);
    if (leftmost)
        tree->rb_leftmost = node;
    ns_rb_insert_color(node, tree);

    return leftmost ? node : NULL;
}


ns_rbtree_node_t *ns_rbtree_find_add(ns_rbtree_node_t *node, ns_rbtree_t *tree)
{
    ns_rbtree_node_t **link = &tree->rb_node;
    ns_rbtree_node_t *parent = NULL;
    bool leftmost = true;
    int c;

    while (*link) {
        parent = *link;
        c = tree->cmp(node, parent);

        if (c < 0)
            link = &parent->rb_left;
        else if (c > 0){
            link = &parent->rb_right;
            leftmost = false;
        }else
            return parent;
    }

    ns_rb_link_node(node, parent, link);
    if (leftmost)
        tree->rb_leftmost = node;
    ns_rb_insert_color(node, tree);
    return node;
}

ns_rbtree_node_t *ns_rbtree_find_new_add(const void *key, ns_rbtree_t *tree,
          int (*cmp)(const void *key, const ns_rbtree_node_t *), void *user_data,
          ns_rbtree_node_t* (new_node)(void *user_data))
{
    ns_rbtree_node_t **link = &tree->rb_node;
    ns_rbtree_node_t *parent = NULL;
    ns_rbtree_node_t *node;
    bool leftmost = true;
    int c;

    while (*link) {
        parent = *link;
        c = cmp(key, parent);

        if (c < 0)
            link = &parent->rb_left;
        else if (c > 0){
            link = &parent->rb_right;
            leftmost = false;
        }else
            return parent;
    }
    node = new_node(user_data);
    if(!node)
        return NULL;
    if(!ns_rbtree_node_is_empty(node))
        return NULL;
    ns_rb_link_node(node, parent, link);
    if (leftmost)
        tree->rb_leftmost = node;
    ns_rb_insert_color(node, tree);
    return node;
}


ns_rbtree_node_t * ns_rbtree_match_find(const void *key, ns_rbtree_t *tree,
    int (*match)(const void *key, const ns_rbtree_node_t *)){
    ns_rbtree_node_t *node;
    int c;

    node = tree->rb_node;
    while(node){
        c = match(key, node);
        if(c < 0){
            node = node->rb_left;
        }else if(c > 0){
            node = node->rb_right;
        }else{
            return node;
        }
    }
    return NULL;
}


ns_rbtree_node_t *ns_rbtree_find_first(const void *key, const ns_rbtree_t *tree,
          int (*match)(const void *key, const ns_rbtree_node_t *))
{
    ns_rbtree_node_t *node = tree->rb_node;
    ns_rbtree_node_t *match_node = NULL;

    while (node) {
        int c = match(key, node);

        if (c <= 0) {
            if (!c)
                match_node = node;
            node = node->rb_left;
        } else if (c > 0) {
            node = node->rb_right;
        }
    }

    return match_node;
}

ns_rbtree_node_t *ns_rbtree_next_postorder(const ns_rbtree_node_t *node)
{
    const ns_rbtree_node_t *parent;
    if (!node)
        return NULL;
    parent = ns_rb_parent(node);

    /* If we're sitting on node, we've already seen our children */
    if (parent && node == parent->rb_left && parent->rb_right) {
        /* If we are the parent's left node, go to the parent's right
         * node then all the way down to the left */
        return ns_rb_left_deepest_node(parent->rb_right);
    } else
        /* Otherwise we are the parent's right node, and the parent
         * should be next */
        return (ns_rbtree_node_t *)parent;
}

ns_rbtree_node_t *ns_rbtree_first_postorder(const ns_rbtree_t *root)
{
    if (!root->rb_node)
        return NULL;

    return ns_rb_left_deepest_node(root->rb_node);
}

ns_rbtree_node_t *ns_rbtree_next_match(const void *key, ns_rbtree_node_t *node,
          int (*match)(const void *key, const ns_rbtree_node_t *))
{
    node = ns_rbtree_next(node);
    if (node && match(key, node))
        node = NULL;
    return node;
}
