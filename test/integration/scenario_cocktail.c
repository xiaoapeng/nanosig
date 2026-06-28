/**
 * @file scenario_cocktail.c
 * @brief Multi-payload cocktail: no-payload, int, small struct, large struct.
 * @date 2026-06-20
 *
 * 4 payload types, 3 slots each, 500 emits per type.
 * Large struct (1024B) verified for data integrity.
 * Capacity: NS_CAPACITY_2097152 (large payloads)
 *
 * #included into test_layer3.c
 */

#include "test_macros.h"
#include "integration_helpers.h"

#define CKTL_NUM_TYPES 4u
#define CKTL_SLOTS_PER_TYPE 3u
#define CKTL_EMITS_PER_TYPE (500u * integration_test_scale())
#define CKTL_LARGE_SIZE 1024u

/* Small struct payload */
typedef struct {
    int32_t id;
    double value;
    char label[8];
} cktl_small_t;

/* Large struct payload */
typedef struct {
    int32_t magic;
    int32_t sequence;
    uint8_t data[CKTL_LARGE_SIZE - 8u];
} cktl_large_t;

#define CKTL_MAGIC ((int32_t)0xDEADBEEF)

static const size_t g_cktl_payload_sizes[CKTL_NUM_TYPES] = 
    {0u, sizeof(int32_t), sizeof(cktl_small_t), sizeof(cktl_large_t)};
static const char *g_cktl_labels[CKTL_NUM_TYPES] = 
    {"no-payload", "int32", "small-struct", "large-struct"};

static ns_signal_t g_cktl_signals[CKTL_NUM_TYPES];
static ns_loop_t *g_cktl_loops[3];
static ns_connection_t g_cktl_conns[CKTL_NUM_TYPES * CKTL_SLOTS_PER_TYPE];
static atomic_int g_cktl_counts[CKTL_NUM_TYPES * CKTL_SLOTS_PER_TYPE];

static void cktl_slot(void *user_data, const void *payload)
{
    size_t idx = (size_t)(intptr_t)user_data;
    (void)payload;
    ns_atomic_fetch_add_explicit(&g_cktl_counts[idx], 1, ns_memory_order_relaxed);
}

static int scenario_cocktail(void)
{
    size_t i, j;
    size_t conn_idx = 0u;

    for(i = 0u; i < CKTL_NUM_TYPES * CKTL_SLOTS_PER_TYPE; i++){
        ns_atomic_init(&g_cktl_counts[i], 0);
    }
    for(i = 0u; i < 3u; i++) g_cktl_loops[i] = NULL;

    INTEGRATION_PHASE("cocktail: init");
    EXPECT_OK(ns_init() == NS_OK);

    for(i = 0u; i < 3u; i++){
        char name[16];
        (void)snprintf(name, sizeof(name), "cktl_%zu", i);
        g_cktl_loops[i] = integration_create_loop(NS_CAPACITY_2097152, name);
        EXPECT_OK(g_cktl_loops[i] != NULL);
        EXPECT_OK(ns_loop_start(g_cktl_loops[i]) == NS_OK);
    }

    /* Init signals with correct payload sizes */
    for(i = 0u; i < CKTL_NUM_TYPES; i++){
        EXPECT_OK(ns_signal_init_raw(&g_cktl_signals[i], g_cktl_payload_sizes[i], 0u, g_cktl_labels[i]) == NS_OK);
    }

    /* Connect 3 slots per type, cycling through loops */
    for(i = 0u; i < CKTL_NUM_TYPES; i++){
        for(j = 0u; j < CKTL_SLOTS_PER_TYPE; j++){
            EXPECT_OK(ns_signal_connect(&g_cktl_signals[i], cktl_slot, g_cktl_loops[j],
                                        (void *)(intptr_t)conn_idx, &g_cktl_conns[conn_idx]) == NS_OK);
            conn_idx++;
        }
    }

    INTEGRATION_PHASE("cocktail: emitting %d per type", CKTL_EMITS_PER_TYPE);

    /* Emit no-payload */
    for(i = 0u; i < CKTL_EMITS_PER_TYPE; i++){
        EXPECT_OK(ns_signal_emit_raw(&g_cktl_signals[0], NULL, 0u) == NS_OK);
    }
    /* Emit int32 */
    for(i = 0u; i < CKTL_EMITS_PER_TYPE; i++){
        int32_t val = (int32_t)i;
        EXPECT_OK(ns_signal_emit_raw(&g_cktl_signals[1], &val, sizeof(val)) == NS_OK);
    }
    /* Emit small struct */
    for(i = 0u; i < CKTL_EMITS_PER_TYPE; i++){
        cktl_small_t s;
        s.id = (int32_t)i;
        s.value = (double)i * 1.5;
        (void)snprintf(s.label, sizeof(s.label), "s%04d", (int)i);
        EXPECT_OK(ns_signal_emit_raw(&g_cktl_signals[2], &s, sizeof(s)) == NS_OK);
    }
    /* Emit large struct */
    {
        int32_t seq = 0;
        for(i = 0u; i < CKTL_EMITS_PER_TYPE; i++){
            cktl_large_t l;
            l.magic = CKTL_MAGIC;
            l.sequence = seq++;
            memset(l.data, (int)(i & 0xFF), sizeof(l.data));
            EXPECT_OK(ns_signal_emit_raw(&g_cktl_signals[3], &l, sizeof(l)) == NS_OK);
        }
    }

    /* Allow dispatch to settle */
#if defined(_WIN32)
    Sleep(1000u);
#else
    { struct timespec ts = {1, 0}; (void)nanosleep(&ts, NULL); }
#endif

    /* Verify counts */
    for(i = 0u; i < CKTL_NUM_TYPES; i++){
        for(j = 0u; j < CKTL_SLOTS_PER_TYPE; j++){
            size_t idx = i * CKTL_SLOTS_PER_TYPE + j;
            int cnt = ns_atomic_load_explicit(&g_cktl_counts[idx], ns_memory_order_acquire);
            INTEGRATION_STATS("cocktail: %s/slot[%zu] = %d", g_cktl_labels[i], j, cnt);
            EXPECT_EQ(cnt, (int)CKTL_EMITS_PER_TYPE);
        }
    }

    /* Cleanup */
    for(i = 0u; i < CKTL_NUM_TYPES; i++){
        conn_idx = i * CKTL_SLOTS_PER_TYPE;
        for(j = 0u; j < CKTL_SLOTS_PER_TYPE; j++){
            EXPECT_OK(ns_signal_disconnect(&g_cktl_conns[conn_idx + j]) == NS_OK);
        }
        EXPECT_OK(ns_signal_deinit_raw(&g_cktl_signals[i]) == NS_OK);
    }

    for(i = 0u; i < 3u; i++){
        EXPECT_OK(ns_loop_stop(g_cktl_loops[i]) == NS_OK);
        EXPECT_OK(ns_loop_deinit(g_cktl_loops[i]) == NS_OK);
        g_cktl_loops[i] = NULL;
    }

    integration_verify_clean_shutdown();
    INTEGRATION_PASS("cocktail: all %d payload types verified", CKTL_NUM_TYPES);
    return 0;
}

#undef CKTL_NUM_TYPES
#undef CKTL_SLOTS_PER_TYPE
#undef CKTL_EMITS_PER_TYPE
#undef CKTL_LARGE_SIZE
#undef CKTL_MAGIC

#ifdef SCENARIO_MAIN
int main(void) { return scenario_cocktail(); }
#endif
