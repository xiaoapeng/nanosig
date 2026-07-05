/**
 * @file ns_hashtable.c
 * @brief nanosig 字符串键 intrusive 哈希表实现。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nanosig/nanosig_hashtable.h>
static int ns_hashtable_is_valid(const ns_hashtable_t *table)
{
    return (table != NULL) &&
           (table->buckets != NULL) &&
           (table->bucket_count != 0u);
}

uint32_t ns_hash_string(const char *key)
{
    uint32_t hash = 2166136261u;

    if(key == NULL) return 0u;

    const unsigned char *cursor = (const unsigned char *)key;

    while(*cursor != 0u){
        hash ^= (uint32_t)(*cursor);
        hash *= 16777619u;
        ++cursor;
    }

    return hash;
}

int ns_hashtable_init(ns_hashtable_t *table, ns_slist_t *buckets, size_t bucket_count)
{
    size_t i;

    if((table == NULL) || (buckets == NULL) || (bucket_count == 0u)){
        return NS_E_INVAL;
    }

    table->buckets = buckets;
    table->bucket_count = bucket_count;
    table->size = 0u;

    for(i = 0u; i < bucket_count; ++i){
        ns_slist_init(&buckets[i]);
    }

    return NS_OK;
}

void ns_hashtable_node_init(ns_hashtable_node_t *node, const char *key, void *value)
{
    if(node == NULL) return;
    if(key == NULL) return;

    ns_slist_node_init(&node->link);
    node->key = key;
    node->value = value;
    node->hash = ns_hash_string(key);
}

int ns_hashtable_insert(ns_hashtable_t *table, ns_hashtable_node_t *node)
{
    size_t index;
    ns_slist_node_t *cursor;

    if(!ns_hashtable_is_valid(table) || (node == NULL) || (node->key == NULL)){
        return NS_E_INVAL;
    }

    index = (size_t)node->hash % table->bucket_count;

    /* 走桶链检查重复：利用 node->hash 避免二次哈希计算 */
    cursor = table->buckets[index].first;
    while(cursor != NULL){
        ns_hashtable_node_t *entry = ns_slist_entry(cursor, ns_hashtable_node_t, link);
        if((entry->hash == node->hash) && (strcmp(entry->key, node->key) == 0)){
            return NS_E_EXISTS;
        }
        cursor = cursor->next;
    }

    ns_slist_push_front(&table->buckets[index], &node->link);
    ++table->size;
    return NS_OK;
}

ns_hashtable_node_t *ns_hashtable_find(const ns_hashtable_t *table, const char *key)
{
    uint32_t hash;
    size_t index;
    ns_slist_node_t *cursor;

    if(!ns_hashtable_is_valid(table) || (key == NULL)){
        return NULL;
    }

    hash = ns_hash_string(key);
    index = (size_t)hash % table->bucket_count;
    cursor = table->buckets[index].first;

    while(cursor != NULL){
        ns_hashtable_node_t *entry = ns_slist_entry(cursor, ns_hashtable_node_t, link);
        if((entry->hash == hash) && (strcmp(entry->key, key) == 0)) return entry;
        cursor = cursor->next;
    }

    return NULL;
}

ns_hashtable_node_t *ns_hashtable_remove(ns_hashtable_t *table, const char *key)
{
    uint32_t hash;
    size_t index;
    ns_slist_node_t *prev = NULL;
    ns_slist_node_t *cursor;

    if(!ns_hashtable_is_valid(table) || (key == NULL)){
        return NULL;
    }

    hash = ns_hash_string(key);
    index = (size_t)hash % table->bucket_count;
    cursor = table->buckets[index].first;

    while(cursor != NULL){
        ns_hashtable_node_t *entry = ns_slist_entry(cursor, ns_hashtable_node_t, link);
        if((entry->hash == hash) && (strcmp(entry->key, key) == 0)){
            (void)ns_slist_remove_after(&table->buckets[index], prev);
            --table->size;
            return entry;
        }
        prev = cursor;
        cursor = cursor->next;
    }

    return NULL;
}

void ns_hashtable_clear(ns_hashtable_t *table)
{
    size_t i;

    if(!ns_hashtable_is_valid(table)) return;

    for(i = 0u; i < table->bucket_count; ++i){
        ns_slist_node_t *node;
        do {
            node = ns_slist_pop_front(&table->buckets[i]);
        } while(node != NULL);
    }

    table->size = 0u;
}
