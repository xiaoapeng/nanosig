/**
 * @file nanosig_rbtree.h
 * @brief nanosig intrusive 红黑树。
 * @date 2026-07-06
 *
 * 移植自 eventhub_os eh_rbtree.h，仅修改 API 前缀 (eh_ → ns_)，逻辑未变。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_RBTREE_H
#define NANOSIG_RBTREE_H

#include <stddef.h>
#include <stdbool.h>
#include <nanosig/nanosig_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t ns_rb_parent_node_t;

struct ns_rbtree_node {
    ns_rb_parent_node_t parent_and_color;
    struct ns_rbtree_node *rb_right;
    struct ns_rbtree_node *rb_left;
} NS_ALIGNED(sizeof(long));
/* The alignment might seem pointless, but allegedly CRIS needs it */

typedef struct ns_rbtree_node ns_rbtree_node_t;

struct ns_rbtree_root {
    ns_rbtree_node_t *rb_node;
    ns_rbtree_node_t *rb_leftmost;
    /**
     * @brief      内部使用的比较函数
     * @return  -1:a<b  0:a==b  1:a>b
     */
    int (*cmp)(ns_rbtree_node_t *a, ns_rbtree_node_t *b);
};

typedef struct ns_rbtree_root ns_rbtree_t;

#define NS_RBTREE_ROOT_INIT(root, _cmp) {               \
        .rb_node = NULL,                                \
        .rb_leftmost = NULL,                            \
        .cmp = _cmp,                                    \
    }
#define NS_RBTREE_NODE_INIT(root) {                     \
        .parent_and_color = (ns_rb_parent_node_t)&root,       \
        .rb_left = NULL,                                \
        .rb_right = NULL,                               \
    }

#define NS_RBTREE_RED        0
#define NS_RBTREE_BLACK      1

#define __ns_rb_parent(pc)    ((ns_rbtree_node_t *)(pc & ~((ns_rb_parent_node_t)3)))

#define __ns_rb_color(pc)     ((pc) & ((ns_rb_parent_node_t)1))
#define __ns_rb_is_black(pc)  __ns_rb_color(pc)
#define __ns_rb_is_red(pc)    (!__ns_rb_color(pc))
#define ns_rb_color(rb)       __ns_rb_color((rb)->parent_and_color)
#define ns_rb_is_red(rb)      __ns_rb_is_red((rb)->parent_and_color)
#define ns_rb_is_black(rb)    __ns_rb_is_black((rb)->parent_and_color)


#define ns_rb_parent(r)   ((ns_rbtree_node_t *)((r)->parent_and_color & ~((ns_rb_parent_node_t)3)))

#define ns_rbtree_entry(ptr, type, member)         NS_CONTAINER_OF(ptr, type, member)

#define ns_rbtree_entry_safe(ptr, type, member)    NS_CONTAINER_OF_SAFE(ptr, type, member)

#define ns_rbtree_root_is_empty(root)  ((root)->rb_node == NULL)

/* 'empty' nodes are nodes that are known not to be inserted in an rbtree */
#define ns_rbtree_node_is_empty(node)                               \
    ((node)->parent_and_color == (ns_rb_parent_node_t)(node))
#define ns_rbtree_node_clear(node)                                  \
    ((node)->parent_and_color = (ns_rb_parent_node_t)(node))

#define ns_rbtree_root_init(root, __cmp)                            \
    do{                                                             \
        (root)->rb_node = NULL;                                     \
        (root)->rb_leftmost = NULL;                                 \
        (root)->cmp = (__cmp);                                      \
    }while(0)

#define ns_rbtree_node_init(node)                                   \
    do{                                                             \
        (node)->parent_and_color = (ns_rb_parent_node_t)(node);     \
        (node)->rb_left = NULL;                                     \
        (node)->rb_right = NULL;                                    \
    }while(0)

/**
 * @brief                     删除某个节点
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t * ns_rbtree_del(ns_rbtree_node_t *, ns_rbtree_t *);

/**
 * @brief                     添加节点
 * @param  node             要添加的节点
 * @param  tree             rb树对象
 * @return                     正常返回NULL, 返回 node 说明 插入最左的节点(为定时器而考虑的逻辑)
 */
extern ns_rbtree_node_t * ns_rbtree_add(ns_rbtree_node_t *node, ns_rbtree_t *tree);

/**
 * @brief                     添加节点
 * @param  node             要添加的节点
 * @param  tree             rb树对象
 * @return                  返回找到的节点, 如果没有找到返回插入的节点
 */
extern ns_rbtree_node_t * ns_rbtree_find_add(ns_rbtree_node_t *node, ns_rbtree_t *tree);

/**
 * @brief                     找到一个与key匹配的节点, 如果没有找到, 则创建一个新节点
 * @param  key              用于cmp函数比较的第一个参数
 * @param  tree             rb树对象
 * @param  cmp              比较函数
 * @param  user_data        用于new_node函数的参数
 * @param  new_node         创建新节点的函数
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t *ns_rbtree_find_new_add(const void *key, ns_rbtree_t *tree,
          int (*cmp)(const void *key, const ns_rbtree_node_t *), void *user_data,
          ns_rbtree_node_t* (new_node)(void *user_data));

/**
 * @brief                     找到一个与key匹配的节点
 * @param  key              用于match函数比较的第一个参数
 * @param  tree             rb树对象
 * @param  match            匹配函数
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t * ns_rbtree_match_find(const void *key, ns_rbtree_t *tree,
    int (*match)(const void *key, const ns_rbtree_node_t *));

/**
 * @brief                     下一个节点
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t *    ns_rbtree_next(const ns_rbtree_node_t *);

/**
 * @brief                     上一个节点
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t *    ns_rbtree_prev(const ns_rbtree_node_t *);

/**
 * @brief                     第一个节点
 */
#define                         ns_rbtree_first(root)    ((root)->rb_leftmost)

/**
 * @brief                     最后一个节点
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t *    ns_rbtree_last(const ns_rbtree_t *);

/**
 * @brief                     找到后序遍历的第一个
 * @param  root             rb树
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t *ns_rbtree_first_postorder(const ns_rbtree_t *root);

/**
 * @brief                     返回后续遍历的下一个
 * @param  node             本次遍历的节点
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t *ns_rbtree_next_postorder(const ns_rbtree_node_t *);

/**
 * @brief                     找到一个与key匹配的节点,最左边的
 * @param  key              用于match函数比较的第一个参数
 * @param  tree             rb树对象
 * @param  match            匹配函数
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t *ns_rbtree_find_first(const void *key, const ns_rbtree_t *tree,
          int (*match)(const void *key, const ns_rbtree_node_t *));

/**
 * @brief                     找到下一个能匹配key的节点
 * @param  key              用于match函数比较的第一个参数
 * @param  node             本次用于匹配的节点
 * @param  match            匹配函数
 * @return ns_rbtree_node_t*
 */
extern ns_rbtree_node_t *ns_rbtree_next_match(const void *key, ns_rbtree_node_t *node,
          int (*match)(const void *key, const ns_rbtree_node_t *));

/*后序 左右根 */
#define ns_rbtree_for_each_entry_postorder_safe(pos, n, root, member)                   \
    for (pos = ns_rbtree_entry_safe(ns_rbtree_first_postorder(root), typeof(*pos), member);    \
         pos && ({ n = ns_rbtree_entry_safe(ns_rbtree_next_postorder(&pos->member),            \
            typeof(*pos), member); 1; });                                           \
         pos = n)

/* 递增遍历 */
#define ns_rbtree_for_each_entry(pos, root, member)                                \
    for (pos = ns_rbtree_entry_safe(ns_rbtree_first(root), typeof(*pos), member);           \
         pos ;                                                                      \
         pos = ns_rbtree_entry_safe(ns_rbtree_next(&pos->member), typeof(*pos), member))

/* 递减遍历 */
#define ns_rbtree_for_each_entry_prev(pos, root, member)                                \
    for (pos = ns_rbtree_entry_safe(ns_rbtree_last(root), typeof(*pos), member) ;           \
         pos ;                                                                      \
         pos = ns_rbtree_entry_safe(ns_rbtree_prev(&pos->member), typeof(*pos), member))

/* 条件遍历 */
#define ns_rbtree_for_each_entry_match(pos, tree, key, match, member)                         \
    for ((pos) = ns_rbtree_entry_safe(ns_rbtree_find_first((key), (tree),                   \
            (match)), typeof(*pos), member);                                        \
         (pos); (pos) = ns_rbtree_entry_safe( ns_rbtree_next_match((key), &((pos)->member), (match)), \
             typeof(*pos), member))

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_RBTREE_H */
