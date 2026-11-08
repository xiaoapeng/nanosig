/**
 * @file main.c
 * @brief Verify module configuration through a linked consumer.
 * @date 2026-11-07
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <nanosig/nanosig_hashtbl.h>
#include <nanosig/nanosig_mpsc_record_ring.h>
#include <nanosig/nanosig_port.h>
#include <nanosig/ns_debug.h>

#if defined(NS_CONFIG_DEFAULT_DEBUG_LEVEL) || defined(NS_CONFIG_STDOUT_MEM_CACHE_SIZE) || \
    defined(NS_DBG_MODULE_LEVEL_CORE) || defined(NS_DBG_MODULE_LEVEL_TIMER) || \
    defined(NS_DBG_MODULE_LEVEL_BROKER) || defined(NS_DBG_MODULE_LEVEL_MPSC) || \
    defined(NS_DBG_MODULE_LEVEL_PLATFORM)
#error "Private module settings must not propagate to the consumer"
#endif

NS_STATIC_ASSERT(NS_MPSC_CACHELINE_SIZE == CHECK_CACHELINE_SIZE, "public cache-line size");
NS_STATIC_ASSERT(NS_MPSC_CACHELINE_SIZE >= 16 &&
                 (NS_MPSC_CACHELINE_SIZE & (NS_MPSC_CACHELINE_SIZE - 1)) == 0,
                 "valid cache-line size");
#ifdef NS_MPSC_CACHELINE_ALIGNED
NS_STATIC_ASSERT(offsetof(ns_mpsc_record_ring_t, read_pos) -
                 offsetof(ns_mpsc_record_ring_t, write_pos) == NS_MPSC_CACHELINE_SIZE,
                 "producer and consumer counters occupy separate cache-line spans");
NS_STATIC_ASSERT(offsetof(ns_mpsc_record_ring_t, storage) -
                 offsetof(ns_mpsc_record_ring_t, read_pos) == NS_MPSC_CACHELINE_SIZE,
                 "read-only fields do not share the consumer cache-line span");
#endif

#ifdef CHECK_CUSTOM_CONFIG
NS_STATIC_ASSERT(NS_DBG_MODULE_LEVEL_DEFAULT == NS_DBG_WARNING, "public debug level");
NS_STATIC_ASSERT(NS_CONFIG_DEBUG_FLAGS == 0, "public debug flags");
NS_STATIC_ASSERT(NS_CONFIG_HASHTBL_MIN_SIZE == 32, "public hash capacity");
NS_STATIC_ASSERT(NS_PLATFORM_WAIT_EMPTY_MAX_US == 250000, "public wait timeout");
#else
NS_STATIC_ASSERT(NS_DBG_MODULE_LEVEL_DEFAULT == NS_DBG_SYS, "default debug level");
NS_STATIC_ASSERT(NS_CONFIG_DEBUG_FLAGS == 6, "default debug flags");
NS_STATIC_ASSERT(NS_CONFIG_HASHTBL_MIN_SIZE == 16, "default hash capacity");
NS_STATIC_ASSERT(NS_PLATFORM_WAIT_EMPTY_MAX_US == 1000000, "default wait timeout");
#endif

#ifdef CHECK_ESCAPED_CONFIG
static size_t hash_count;

void platform_stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    (void)stream;
    for(size_t i = 0; i < size; i++){
        if(buf[i] == '#') hash_count++;
    }
}
#endif

int main(void)
{
    ns_hashtbl_t table = NULL;
    ns_mpsc_record_ring_t ring;
    size_t storage[128u / sizeof(size_t)];
    const uint8_t payload[16] = { 42 };
    void *record = NULL;
    size_t record_size = 0u;
    int rc = 1;

    if(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) != NS_OK) return 1;
    if(ns_mpsc_record_ring_try_push(&ring, payload, sizeof(payload)) != NS_OK) return 1;
    if(ns_mpsc_record_ring_try_acquire(&ring, &record, &record_size) != NS_OK) return 1;
    if(record_size != sizeof(payload) || memcmp(record, payload, record_size) != 0) return 1;
    if(ns_mpsc_record_ring_release(&ring, record) != NS_OK) return 1;
    if(ns_mpsc_record_ring_free_capacity(&ring) != sizeof(storage)) return 1;

    if(ns_platform_init() != 0) return 1;
    if(ns_debug_init() != 0) goto out_platform;
    table = ns_hashtbl_create(NS_HASHTBL_DEFAULT_LOADFACTOR);
    if(table == NULL) goto out_debug;
    if(((struct ns_hashtbl *)table)->mask + 1u != NS_CONFIG_HASHTBL_MIN_SIZE) goto out_table;

#ifdef CHECK_CUSTOM_CONFIG
    if(_ns_stdout.end - _ns_stdout.cache != 128) goto out_table;
#ifdef CHECK_ESCAPED_CONFIG
    if(strcmp(NS_DEBUG_ENTER_SIGN, "#") != 0) goto out_table;
    if(ns_errln("visible") != 8) goto out_table;
    (void)ns_printf("\n");
    hash_count = 0;
    /* Hex output appends the library's compiled-in line ending. */
    if(ns_dbg_hex(NS_DBG_ERR, 0, 0, NULL) <= 0) goto out_table;
    (void)ns_printf("\n");
    if(hash_count == 0) goto out_table;
#else
    if(strcmp(NS_DEBUG_ENTER_SIGN, "\r\n") != 0) goto out_table;
    if(ns_dbg_raw(NS_DBG_DEBUG, 0, "hidden\n") != 0) goto out_table;
#ifdef CHECK_ZERO_CONFIG
    if(ns_errln("hidden") != 0) goto out_table;
#else
    if(ns_errln("visible") != 9) goto out_table;
#endif
#endif
#else
    if(_ns_stdout.end - _ns_stdout.cache != 512) goto out_table;
    if(strcmp(NS_DEBUG_ENTER_SIGN, "\n") != 0) goto out_table;
    if(ns_dbg_raw(NS_DBG_DEBUG, 0, "visible\n") != 8) goto out_table;
#endif
    rc = 0;

out_table:
    ns_hashtbl_destroy(table);
out_debug:
    ns_debug_shutdown();
out_platform:
    (void)ns_platform_shutdown();
    if(rc != 0) fprintf(stderr, "module configuration did not reach the library/consumer\n");
    return rc;
}
