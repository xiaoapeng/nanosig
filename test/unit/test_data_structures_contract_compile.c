/**
 * @file test_data_structures_contract_compile.c
 * @brief 公开数据结构 API syntax-only 契约检查。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stddef.h>
#include <stdint.h>

#include <nanosig/nanosig_ds.h>
typedef struct ds_contract_item {
    int value;
    ns_list_node_t list_node;
    ns_slist_node_t slist_node;
    ns_hashtable_node_t hash_node;
    ns_rbtree_node_t tree_node;
} ds_contract_item_t;

static void ds_contract_use_list(void)
{
    ns_list_node_t head = NS_LIST_INITIALIZER(head);
    ds_contract_item_t item;
    ds_contract_item_t *owner;

    ns_list_init(&item.list_node);
    ns_list_push_back(&head, &item.list_node);
    owner = ns_list_entry(ns_list_front(&head), ds_contract_item_t, list_node);
    (void)owner;
    ns_list_remove_init(&item.list_node);
}

static void ds_contract_use_slist(void)
{
    ns_slist_t list = NS_SLIST_INITIALIZER;
    ds_contract_item_t item;
    ds_contract_item_t *owner;

    ns_slist_node_init(&item.slist_node);
    ns_slist_push_back(&list, &item.slist_node);
    owner = ns_slist_entry(ns_slist_pop_front(&list), ds_contract_item_t, slist_node);
    (void)owner;
}

static void ds_contract_use_ringbuf(void)
{
    uint8_t storage[8];
    uint8_t out[8];
    int32_t len;
    ns_ringbuf_t ringbuf;

    if(ns_ringbuf_init(&ringbuf, storage, (int32_t)sizeof(storage)) != NS_OK) return;
    (void)ns_ringbuf_write(&ringbuf, storage, (int32_t)sizeof(storage));
    len = (int32_t)sizeof(out);
    (void)ns_ringbuf_peek(&ringbuf, 0, out, &len);
    (void)ns_ringbuf_peek_copy(&ringbuf, 0, out, (int32_t)sizeof(out));
    (void)ns_ringbuf_read(&ringbuf, out, (int32_t)sizeof(out));
}

static void ds_contract_use_mpsc_record_ring(void)
{
    ns_mpsc_record_ring_t ring;
    size_t record_size = 0u;
    uint8_t storage[NS_CAPACITY_128];
    void *record = NULL;
    ns_mpsc_record_part_t parts[2];

    parts[0].data = "ab";
    parts[0].size = 2u;
    parts[1].data = "cd";
    parts[1].size = 2u;

    if(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) != NS_OK) return;
    (void)ns_mpsc_record_ring_capacity(&ring);
    (void)ns_mpsc_record_ring_free_capacity(&ring);
    (void)ns_mpsc_record_ring_max_record_size(&ring);
    (void)ns_mpsc_record_ring_try_push(&ring, parts[0].data, parts[0].size);
    (void)ns_mpsc_record_ring_try_pushv(&ring, parts, NS_ARRAY_SIZE(parts));
    if(ns_mpsc_record_ring_try_acquire(&ring, &record, &record_size) == NS_OK){
        (void)ns_mpsc_record_ring_release(&ring, record);
    }
}

static void ds_contract_use_hashtable(void)
{
    ns_slist_t buckets[4];
    ns_hashtable_t table;
    ds_contract_item_t item;

    if(ns_hashtable_init(&table, buckets, 4u) != NS_OK) return;
    ns_hashtable_node_init(&item.hash_node, "key", &item);
    (void)ns_hashtable_insert(&table, &item.hash_node);
    (void)ns_hashtable_find(&table, "key");
    (void)ns_hashtable_remove(&table, "key");
}

static int ds_contract_rbtree_cmp(const ns_rbtree_node_t *a, const ns_rbtree_node_t *b)
{
    (void)a; (void)b;
    return 0;
}

static void ds_contract_use_rbtree(void)
{
    ns_rbtree_t tree;
    ds_contract_item_t item;
    ds_contract_item_t *owner;

    ns_rbtree_init(&tree, ds_contract_rbtree_cmp);
    ns_rbtree_node_init(&item.tree_node);
    ns_rbtree_insert(&tree, &item.tree_node);
    owner = ns_rbtree_entry(ns_rbtree_first(&tree), ds_contract_item_t, tree_node);
    (void)owner;
    ns_rbtree_remove(&tree, &item.tree_node);
}

int main(void)
{
    ds_contract_use_list();
    ds_contract_use_slist();
    ds_contract_use_ringbuf();
    ds_contract_use_mpsc_record_ring();
    ds_contract_use_hashtable();
    ds_contract_use_rbtree();
    return 0;
}
