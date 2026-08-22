/**
 * @file ns_hashtbl.c
 * @brief 哈希表实现（对齐 eventhub_os eh_hashtbl）。
 * @date 2026-08-08
 *
 * 从 eventhub_os 的 eh_hashtbl.c 移植。库管理内存：节点 KV 内联，
 * 自动扩容（resize）+ 渐进式重建（try_remake）。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nanosig/nanosig_port.h>
#include <nanosig/nanosig_hashtbl.h>

NS_STATIC_ASSERT(
    (NS_CONFIG_HASHTBL_MIN_SIZE >= 8) &&
        ((NS_CONFIG_HASHTBL_MIN_SIZE & (NS_CONFIG_HASHTBL_MIN_SIZE - 1)) == 0),
    "NS_CONFIG_HASHTBL_MIN_SIZE must be a power of 2 and at least 8");

#define ns_hash_val(key, key_len)         fnv1a(key, key_len)
#define ns_hash_str_val(str, out_len_ptr) fnv1a_str(str, out_len_ptr)

#define FNV_OFFSET_BASIS_32 2166136261U
#define FNV_PRIME_32        16777619U

static ns_hash_val_t fnv1a(const char *data, ns_hashtbl_kv_len_t len)
{
    const char *bp = (const char *)data;
    const char *be = bp + len;
    ns_hash_val_t hash = FNV_OFFSET_BASIS_32;

    while(bp < be){
        hash ^= (ns_hash_val_t)*(unsigned char *)bp++;
        hash *= FNV_PRIME_32;
    }
    return hash;
}

static ns_hash_val_t fnv1a_str(const char *str, ns_hashtbl_kv_len_t *out_len)
{
    const char *bp = str;
    ns_hash_val_t hash = FNV_OFFSET_BASIS_32;

    while(*bp){
        hash ^= (ns_hash_val_t)*(unsigned char *)bp++;
        hash *= FNV_PRIME_32;
    }
    if(out_len)
        *out_len = (ns_hashtbl_kv_len_t)(bp - str);
    return hash;
}

/**
 * @brief 计算字符串哈希值（FNV-1a）。
 *
 * 兼容导出，NULL 返回 0（与旧 ns_hashtable 语义一致；上游 fnv1a_str 无 NULL 守卫）。
 */
uint32_t ns_hash_string(const char *key)
{
    if(key == NULL) return 0u;
    return fnv1a_str(key, NULL);
}

static int ns_hashtbl_resize(struct ns_hashtbl *hashtbl)
{
    /* 扩容 */
    unsigned int old_size = hashtbl->mask + 1;
    unsigned int new_mask = (hashtbl->mask << 1) + 1;
    unsigned int i;
    struct ns_list_node *new_table;

    new_table = (struct ns_list_node *)ns_platform_alloc(
        sizeof(struct ns_list_node) * (size_t)(new_mask + 1));
    if(new_table == NULL)
        return NS_E_NOMEM;

    /* 新的后一半表项应该设置为待重建，所以设置为 0 */
    memset(new_table + old_size, 0, sizeof(struct ns_list_node) * old_size);

    /* 前一半应该继承以前的表项 */
    for(i = 0; i < old_size; i++){
        /* 以前重建的依旧拷贝为重建 */
        if(ns_hash_table_node_is_need_remake(hashtbl, i)){
            new_table[i].next = NULL;
            continue;
        }
        ns_list_init(&new_table[i]);
        ns_list_splice_back_init(&new_table[i], &hashtbl->table[i]);
    }

    ns_platform_free(hashtbl->table);
    hashtbl->table = new_table;
    hashtbl->mask = new_mask;
    hashtbl->threshold = hashtbl->threshold << 1;
    return NS_OK;
}

static void ns_hashtbl_try_remake(struct ns_hashtbl *hashtbl, unsigned int idx)
{
    /* 哈希渐进式重建 */
    unsigned int old_idx;
    struct ns_list_node *pos, *n;

    if(ns_hash_table_node_is_need_remake(hashtbl, idx)){
        unsigned int mask_tmp = hashtbl->mask >> 1;

        for(mask_tmp = hashtbl->mask >> 1;
            mask_tmp && ns_hash_table_node_is_need_remake(hashtbl, (idx & mask_tmp));
            mask_tmp = mask_tmp >> 1){
            /* find ... */
        }
        old_idx = idx & mask_tmp;
        /* 用旧的 table node 进行重建 */
        ns_list_for_each_safe(pos, n, &hashtbl->table[old_idx]){
            struct ns_hashtbl_node *node = ns_list_entry(pos, struct ns_hashtbl_node, node);
            unsigned int new_idx = node->hash_val & hashtbl->mask;

            if(new_idx != old_idx){
                ns_list_remove(pos);
                if(ns_hash_table_node_is_need_remake(hashtbl, new_idx))
                    ns_list_init(hashtbl->table + new_idx);
                ns_list_push_front(hashtbl->table + new_idx, pos);
            }
        }
        if(ns_hash_table_node_is_need_remake(hashtbl, idx))
            ns_list_init(hashtbl->table + idx);
    }
}

struct ns_hashtbl_node *ns_hashtbl_node_new(
    ns_hashtbl_kv_len_t key_len, ns_hashtbl_kv_len_t value_len)
{
    struct ns_hashtbl_node *node;

    node = (struct ns_hashtbl_node *)ns_platform_alloc(
        sizeof(struct ns_hashtbl_node) +
        ns_align_up(key_len, NS_HASHTBL_KV_ALIGN) +
        ns_align_up(value_len, NS_HASHTBL_KV_ALIGN));
    if(node == NULL)
        return NULL;
    ns_list_init(&node->node);
    node->value_len = value_len;
    node->key_len = key_len;
    return node;
}

struct ns_hashtbl_node *ns_hashtbl_node_new_refresh(
    ns_hashtbl_t hashtbl, const void *key, ns_hashtbl_kv_len_t key_len,
    ns_hashtbl_kv_len_t value_len)
{
    struct ns_hashtbl_node *node;

    (void)hashtbl;
    node = (struct ns_hashtbl_node *)ns_platform_alloc(
        sizeof(struct ns_hashtbl_node) +
        ns_align_up(key_len, NS_HASHTBL_KV_ALIGN) +
        ns_align_up(value_len, NS_HASHTBL_KV_ALIGN));
    if(node == NULL)
        return NULL;
    memcpy(node->kv, key, key_len);
    node->value_len = value_len;
    node->key_len = key_len;
    node->hash_val = ns_hash_val(key, key_len);
    ns_list_init(&node->node);
    return node;
}

struct ns_hashtbl_node *ns_hashtbl_node_new_with_string_refresh(
    ns_hashtbl_t hashtbl, const char *key, ns_hashtbl_kv_len_t value_len)
{
    struct ns_hashtbl_node *node;
    ns_hashtbl_kv_len_t key_len;
    ns_hash_val_t hash_val;

    if(key == NULL)
        return NULL;    /* 与 ns_hash_string 的 NULL 守卫对称 */
    hash_val = ns_hash_str_val(key, &key_len);

    (void)hashtbl;
    node = (struct ns_hashtbl_node *)ns_platform_alloc(
        sizeof(struct ns_hashtbl_node) +
        ns_align_up(key_len, NS_HASHTBL_KV_ALIGN) +
        ns_align_up(value_len, NS_HASHTBL_KV_ALIGN));
    if(node == NULL)
        return NULL;
    memcpy(node->kv, key, key_len);
    node->value_len = value_len;
    node->key_len = key_len;
    node->hash_val = hash_val;
    ns_list_init(&node->node);
    return node;
}

struct ns_hashtbl_node *ns_hashtbl_node_renew(
    ns_hashtbl_t hashtbl, struct ns_hashtbl_node *old_node,
    ns_hashtbl_kv_len_t value_len)
{
    struct ns_hashtbl_node *node;

    (void)hashtbl;
    if(old_node->value_len == value_len)
        return old_node;
    node = (struct ns_hashtbl_node *)ns_platform_alloc(
        sizeof(struct ns_hashtbl_node) +
        ns_align_up(old_node->key_len, NS_HASHTBL_KV_ALIGN) +
        ns_align_up(value_len, NS_HASHTBL_KV_ALIGN));
    if(node == NULL)
        return NULL;
    node->value_len = value_len;
    node->key_len = old_node->key_len;
    node->hash_val = old_node->hash_val;
    memcpy(node->kv, old_node->kv, old_node->key_len);
    /* 检查旧节点是否挂在哈希表上 */
    if(!ns_list_empty(&old_node->node)){
        ns_list_push_front(&old_node->node, &node->node);
        ns_list_remove(&old_node->node);
    }else{
        ns_list_init(&node->node);
    }
    ns_platform_free(old_node);
    return node;
}

void ns_hashtbl_node_key_refresh(ns_hashtbl_t _hashtbl, struct ns_hashtbl_node *node)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;
    unsigned int idx;

    node->hash_val = ns_hash_val((const char *)node->kv, node->key_len);
    if(ns_hashtbl_node_is_insert(node)){
        idx = node->hash_val & hashtbl->mask;
        ns_list_remove(&node->node);
        ns_list_push_front(hashtbl->table + idx, &node->node);
    }
}

void ns_hashtbl_node_delete(ns_hashtbl_t _hashtbl, struct ns_hashtbl_node *node)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;

    if(ns_hashtbl_node_is_insert(node)){
        ns_list_remove(&node->node);
        hashtbl->count--;
    }
    ns_platform_free(node);
}

int ns_hashtbl_insert(ns_hashtbl_t _hashtbl, struct ns_hashtbl_node *node)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;
    int ret;
    unsigned int idx;

    if(ns_hashtbl_node_is_insert(node))
        return NS_E_EXISTS;
    if(hashtbl->count + 1 >= hashtbl->threshold && hashtbl->mask != UINT32_MAX){
        ret = ns_hashtbl_resize(hashtbl);
        if(ret != NS_OK)
            return ret;
    }
    idx = node->hash_val & hashtbl->mask;

    /* 尝试进行重建 */
    ns_hashtbl_try_remake(hashtbl, idx);

    ns_list_push_front(hashtbl->table + idx, &node->node);
    hashtbl->count++;
    return NS_OK;
}

void ns_hashtbl_remove(ns_hashtbl_t _hashtbl, struct ns_hashtbl_node *node)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;

    if(ns_hashtbl_node_is_insert(node)){
        ns_list_remove_init(&node->node);
        hashtbl->count--;
    }
}

struct ns_list_node *_ns_hashtbl_find_list_head(
    ns_hashtbl_t _hashtbl, const void *key, ns_hashtbl_kv_len_t key_len)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;
    unsigned int idx = ns_hash_val(key, key_len) & hashtbl->mask;

    /* 尝试进行重建 */
    ns_hashtbl_try_remake(hashtbl, idx);
    return &hashtbl->table[idx];
}

struct ns_list_node *_ns_hashtbl_find_list_head_with_string(
    ns_hashtbl_t _hashtbl, const char *key_str)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;
    ns_hashtbl_kv_len_t key_len;
    unsigned int idx = ns_hash_str_val(key_str, &key_len) & hashtbl->mask;

    /* 尝试进行重建 */
    ns_hashtbl_try_remake(hashtbl, idx);
    return &hashtbl->table[idx];
}

int ns_hashtbl_find(
    ns_hashtbl_t _hashtbl, const void *key, ns_hashtbl_kv_len_t key_len,
    struct ns_hashtbl_node **out_node)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;
    unsigned int idx = ns_hash_val(key, key_len) & hashtbl->mask;
    struct ns_list_node *pos;

    /* 尝试进行重建 */
    ns_hashtbl_try_remake(hashtbl, idx);

    ns_list_for_each(pos, &hashtbl->table[idx]){
        struct ns_hashtbl_node *node = ns_list_entry(pos, struct ns_hashtbl_node, node);

        if(node->key_len == key_len && memcmp(node->kv, key, key_len) == 0){
            if(out_node)
                *out_node = node;
            return NS_OK;
        }
    }
    return NS_E_EMPTY;
}

int ns_hashtbl_find_with_string(
    ns_hashtbl_t _hashtbl, const char *key_str, struct ns_hashtbl_node **out_node)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;
    ns_hashtbl_kv_len_t key_len;
    struct ns_list_node *pos;
    unsigned int idx;

    if(key_str == NULL)
        return NS_E_INVAL;    /* NULL 键参数无效，与 ns_hash_string 的 NULL 守卫对称 */
    idx = ns_hash_str_val(key_str, &key_len) & hashtbl->mask;

    /* 尝试进行重建 */
    ns_hashtbl_try_remake(hashtbl, idx);

    ns_list_for_each(pos, &hashtbl->table[idx]){
        struct ns_hashtbl_node *node = ns_list_entry(pos, struct ns_hashtbl_node, node);

        if(node->key_len == key_len && memcmp(node->kv, key_str, key_len) == 0){
            if(out_node)
                *out_node = node;
            return NS_OK;
        }
    }
    return NS_E_EMPTY;
}

ns_hashtbl_t ns_hashtbl_create(float load_factor)
{
    struct ns_hashtbl *hashtbl;
    ns_hashtbl_t ret;

    /* 失败契约：NULL（对齐 ns_platform_alloc）；NaN 拒绝；上限钳制到 1.0f */
    if(isnan(load_factor) || load_factor <= 0.0f)
        return NULL;
    if(load_factor > 1.0f)
        load_factor = 1.0f;

    hashtbl = (struct ns_hashtbl *)ns_platform_alloc(sizeof(struct ns_hashtbl));
    if(hashtbl == NULL)
        return NULL;
    hashtbl->mask = NS_CONFIG_HASHTBL_MIN_SIZE - 1;
    hashtbl->threshold = (unsigned int)(NS_CONFIG_HASHTBL_MIN_SIZE * load_factor);
    if(hashtbl->threshold == 0)
        hashtbl->threshold = 1;    /* 下限钳制：极小 load_factor 时避免每次 insert 都 resize */
    hashtbl->count = 0;
    hashtbl->table = (struct ns_list_node *)ns_platform_alloc(
        sizeof(struct ns_list_node) * NS_CONFIG_HASHTBL_MIN_SIZE);
    if(hashtbl->table == NULL){
        ret = NULL;
        goto table_malloc_error;
    }
    for(unsigned int i = 0; i < NS_CONFIG_HASHTBL_MIN_SIZE; i++){
        ns_list_init(&hashtbl->table[i]);
    }
    return (ns_hashtbl_t)hashtbl;

table_malloc_error:
    ns_platform_free(hashtbl);
    return ret;
}

void ns_hashtbl_destroy(ns_hashtbl_t _hashtbl)
{
    struct ns_hashtbl *hashtbl = (struct ns_hashtbl *)_hashtbl;

    for(unsigned int i = 0; i <= hashtbl->mask; i++){
        struct ns_list_node *pos, *n;

        if(ns_hash_table_node_is_need_remake(hashtbl, i))
            continue;
        ns_list_for_each_safe(pos, n, &hashtbl->table[i]){
            struct ns_hashtbl_node *node = ns_list_entry(pos, struct ns_hashtbl_node, node);

            ns_list_remove(pos);
            ns_platform_free(node);
        }
    }
    ns_platform_free(hashtbl->table);
    ns_platform_free(hashtbl);
}
