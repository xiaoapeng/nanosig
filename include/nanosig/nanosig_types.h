/**
 * @file nanosig_types.h
 * @brief nanosig common compiler and type helper macros.
 * @date 2026-05-24
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_TYPES_H
#define NANOSIG_TYPES_H

#include <stddef.h>
#include <stdint.h>

#if defined(__has_attribute)
#define NS_HAS_ATTRIBUTE(attr) __has_attribute(attr)
#else
#define NS_HAS_ATTRIBUTE(attr) 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NS_HAS_GNU_TYPEOF 1
#else
#define NS_HAS_GNU_TYPEOF 0
#endif

#ifndef NS_SECTION
#if defined(__CC_ARM) || defined(__CLANG_ARM) || defined(__GNUC__) || defined(__clang__)
#define NS_SECTION(section_name) __attribute__((section(section_name)))
#else
#define NS_SECTION(section_name)
#endif
#endif

#define NS_STRINGIFY(x) #x
#define NS_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#if defined(__GNUC__) || defined(__clang__)
#define ns_offsetof(type, member) __builtin_offsetof(type, member)
#else
#define ns_offsetof(type, member) offsetof(type, member)
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define NS_STATIC_ASSERT(expr, msg) _Static_assert((expr), msg)
#else
#define NS_STATIC_ASSERT(expr, msg)
#endif

#if NS_HAS_GNU_TYPEOF
#define ns_same_type(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#else
#define ns_same_type(a, b) 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NS_LIKELY(x) __builtin_expect(!!(x), 1)
#define NS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define NS_LIKELY(x) (!!(x))
#define NS_UNLIKELY(x) (!!(x))
#endif

#if NS_HAS_GNU_TYPEOF
#define NS_CONTAINER_OF(ptr, type, member) \
    ({ \
        void *ns_container_ptr = (void *)(ptr); \
        NS_STATIC_ASSERT( \
            ns_same_type(*(ptr), ((type *)0)->member) || ns_same_type(*(ptr), void), \
            "pointer type mismatch in NS_CONTAINER_OF()"); \
        ((type *)((char *)ns_container_ptr - ns_offsetof(type, member))); \
    })

#define NS_CONTAINER_OF_CONST(ptr, type, member) \
    _Generic((ptr), \
        const __typeof__(*(ptr)) *: ((const type *)NS_CONTAINER_OF((ptr), type, member)), \
        default: ((type *)NS_CONTAINER_OF((ptr), type, member)))

#define NS_CONTAINER_OF_SAFE(ptr, type, member) \
    ({ \
        __typeof__(ptr) ns_container_safe_ptr = (ptr); \
        ns_container_safe_ptr != NULL ? NS_CONTAINER_OF(ns_container_safe_ptr, type, member) : NULL; \
    })

#define NS_MEMBER_ADDRESS_IS_NONNULL(ptr, member) \
    ((uintptr_t)(ptr) + ns_offsetof(__typeof__(*(ptr)), member) != 0u)

#define ns_read_once(x) (*(const volatile __typeof__(x) *)&(x))
#else
#define NS_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - ns_offsetof(type, member)))
#define NS_CONTAINER_OF_CONST(ptr, type, member) \
    NS_CONTAINER_OF((ptr), type, member)
#define NS_CONTAINER_OF_SAFE(ptr, type, member) \
    ((ptr) != NULL ? NS_CONTAINER_OF((ptr), type, member) : NULL)
#define NS_MEMBER_ADDRESS_IS_NONNULL(ptr, member) ((ptr) != NULL)
#define ns_read_once(x) (x)
#endif

#if defined(_MSC_VER)
#define NS_ALIGNED(alignment) __declspec(align(alignment))
#elif defined(__GNUC__) || defined(__clang__)
#define NS_ALIGNED(alignment) __attribute__((aligned(alignment)))
#else
#define NS_ALIGNED(alignment)
#endif

#define ns_align_up(value, alignment) \
    (((value) + ((alignment) - 1u)) & (~((alignment) - 1u)))
#define ns_align_down(value, alignment) \
    ((value) & (~((alignment) - 1u)))

static inline unsigned int ns_ctz_u32(uint32_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned int)__builtin_ctz(value);
#else
    unsigned int count = 0u;

    while((value & 1u) == 0u){
        ++count;
        value >>= 1u;
    }

    return count;
#endif
}

static inline unsigned int ns_clz_u32(uint32_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned int)__builtin_clz(value);
#else
    unsigned int count = 0u;
    uint32_t mask = UINT32_C(1) << 31u;

    while((value & mask) == 0u){
        ++count;
        mask >>= 1u;
    }

    return count;
#endif
}

#define ns_ctz(value) ns_ctz_u32((uint32_t)(value))
#define ns_clz(value) ns_clz_u32((uint32_t)(value))

/**
 * @brief Common power-of-two capacity values for fixed-size internal storage.
 *
 * C enum values are kept within the portable `int` range. Wider architectures
 * can still use the largest values here; larger-than-enum capacities should be
 * introduced with a separate API if they are ever needed.
 */
typedef enum ns_capacity {
    NS_CAPACITY_1 = 1u,
    NS_CAPACITY_2 = 2u,
    NS_CAPACITY_4 = 4u,
    NS_CAPACITY_8 = 8u,
    NS_CAPACITY_16 = 16u,
    NS_CAPACITY_32 = 32u,
    NS_CAPACITY_64 = 64u,
    NS_CAPACITY_128 = 128u,
    NS_CAPACITY_256 = 256u,
    NS_CAPACITY_512 = 512u,
    NS_CAPACITY_1024 = 1024u,
    NS_CAPACITY_2048 = 2048u,
    NS_CAPACITY_4096 = 4096u,
    NS_CAPACITY_8192 = 8192u,
    NS_CAPACITY_16384 = 16384u,
#if SIZE_MAX >= UINT32_MAX
    NS_CAPACITY_32768 = 32768u,
    NS_CAPACITY_65536 = 65536u,
    NS_CAPACITY_131072 = 131072u,
    NS_CAPACITY_262144 = 262144u,
    NS_CAPACITY_524288 = 524288u,
    NS_CAPACITY_1048576 = 1048576u,
    NS_CAPACITY_2097152 = 2097152u,
    NS_CAPACITY_4194304 = 4194304u,
    NS_CAPACITY_8388608 = 8388608u,
    NS_CAPACITY_16777216 = 16777216u,
    NS_CAPACITY_33554432 = 33554432u,
    NS_CAPACITY_67108864 = 67108864u,
    NS_CAPACITY_134217728 = 134217728u,
    NS_CAPACITY_268435456 = 268435456u,
    NS_CAPACITY_536870912 = 536870912u,
    NS_CAPACITY_1073741824 = 1073741824u,
#endif
} ns_capacity_t;

#if defined(__cplusplus) && (__cplusplus >= 201703L)
#define NS_FALLTHROUGH [[fallthrough]]
#elif NS_HAS_ATTRIBUTE(fallthrough) || (defined(__GNUC__) && (__GNUC__ >= 7))
#define NS_FALLTHROUGH __attribute__((fallthrough))
#else
#define NS_FALLTHROUGH do { } while(0)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NS_FUNCTION_WEAK __attribute__((weak))
#else
#define NS_FUNCTION_WEAK
#endif

#if defined(_MSC_VER)
#define NS_NORETURN __declspec(noreturn)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define NS_NORETURN _Noreturn
#elif defined(__GNUC__) || defined(__clang__)
#define NS_NORETURN __attribute__((noreturn))
#else
#define NS_NORETURN
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NS_PACKED __attribute__((packed))
#define NS_FUNCTION_CONST __attribute__((__const__))
#define NS_USED __attribute__((used))
#else
#define NS_PACKED
#define NS_FUNCTION_CONST
#define NS_USED
#endif

#if NS_HAS_ATTRIBUTE(no_sanitize_address)
#define NS_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#else
#define NS_NO_SANITIZE_ADDRESS
#endif

#endif /* NANOSIG_TYPES_H */
