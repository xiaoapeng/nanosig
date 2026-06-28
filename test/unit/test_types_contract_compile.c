#include <stdint.h>

#include <nanosig/nanosig_types.h>
typedef struct ns_types_contract_node {
    int value;
} ns_types_contract_node_t;

typedef struct ns_types_contract_owner {
    int prefix;
    ns_types_contract_node_t node;
    int suffix;
} ns_types_contract_owner_t;

typedef struct NS_ALIGNED(16) ns_types_contract_aligned {
    uint32_t value;
} ns_types_contract_aligned_t;

NS_STATIC_ASSERT(NS_ARRAY_SIZE(((int[]){ 1, 2, 3 })) == 3u, "array size must work");
NS_STATIC_ASSERT(ns_offsetof(ns_types_contract_owner_t, node) > 0u, "offset must work");
NS_STATIC_ASSERT(sizeof(ns_types_contract_aligned_t) >= sizeof(uint32_t), "aligned type must compile");

static int ns_types_contract_use_container(void)
{
    ns_types_contract_owner_t owner = {
        .prefix = 1,
        .node = { .value = 2 },
        .suffix = 3
    };
    const ns_types_contract_owner_t const_owner = {
        .prefix = 4,
        .node = { .value = 5 },
        .suffix = 6
    };
    ns_types_contract_owner_t *from_node = NS_CONTAINER_OF(&owner.node, ns_types_contract_owner_t, node);
    ns_types_contract_owner_t *safe_null = NS_CONTAINER_OF_SAFE((ns_types_contract_node_t *)0, ns_types_contract_owner_t, node);
    const ns_types_contract_owner_t *from_const =
        NS_CONTAINER_OF_CONST(&const_owner.node, ns_types_contract_owner_t, node);

    if(from_node != &owner) return 1;
    if(safe_null != 0) return 1;
    if(from_const != &const_owner) return 1;
    if(!NS_MEMBER_ADDRESS_IS_NONNULL(&owner, node)) return 1;

    return 0;
}

static int ns_types_contract_use_misc(void)
{
    volatile uint32_t value = 8u;
    uint32_t aligned_up = ns_align_up(17u, 8u);
    uint32_t aligned_down = ns_align_down(17u, 8u);

    if(NS_STRINGIFY(nanosig)[0] != 'n') return 1;
    if(NS_LIKELY(value == 0u)) return 1;
    if(!NS_UNLIKELY(value != 0u)) return 1;
    if(ns_read_once(value) != 8u) return 1;
    if(aligned_up != 24u) return 1;
    if(aligned_down != 16u) return 1;
    if(ns_ctz(8u) != 3u) return 1;
    if(ns_clz(1u) != 31u) return 1;

    switch(value){
    case 8u:
        value = 9u;
        NS_FALLTHROUGH;
    case 9u:
        break;
    default:
        return 1;
    }

    return 0;
}

int ns_types_contract_compile_only(void)
{
    if(ns_types_contract_use_container() != 0) return 1;
    if(ns_types_contract_use_misc() != 0) return 1;

    return 0;
}
