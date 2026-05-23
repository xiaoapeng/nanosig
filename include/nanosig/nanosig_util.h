/**
 * @file nanosig_util.h
 * @brief nanosig 通用辅助宏。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_UTIL_H
#define NANOSIG_UTIL_H

#include <stddef.h>

/**
 * @brief 从成员指针反查外层结构体指针。
 *
 * 该宏用于 intrusive list、slist、hashtable 和 rbtree 节点嵌入场景。
 *
 * @param ptr 指向结构体成员的指针。
 * @param type 外层结构体类型。
 * @param member 成员名称。
 */
#define NS_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#endif /* NANOSIG_UTIL_H */
