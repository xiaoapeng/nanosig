/**
 * @file nanosig_platform_port.h
 * @brief nanosig 平台类型 — Windows 后端（构建内头，非安装头）。
 * @date 2026-10-17
 *
 * 本头文件把 nanosig 平台层使用的原语类型映射为 Win32 原语的**透明别名**，
 * 使平台对象全部变为调用方拥有存储。它只装当前平台这一个后端，由构建系统经
 * `NANOSIG_PLATFORM_HEADER` 注入到 `nanosig_port.h`；`nanosig_port.h` 本身
 * 不含任何平台判定宏。
 *
 * 依赖：Win32 API。本头文件会传递给公共头文件的使用者，因此使用者需要能看到
 * `windows.h`（注意 Windows 宏污染）。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_PLATFORM_PORT_H
#define NANOSIG_PLATFORM_PORT_H

#include <windows.h>

#include <stddef.h>
#include <stdint.h>

/* ================================================================== */
/*  ABI 一致性链接探针                                                   */
/* ================================================================== */

/* 宿主三后端（Linux/macOS/Windows）共用同一个探针符号名。 */
extern const int ns_platform_abi_host;
/** 指向当前平台 ABI 探针符号；消费者 TU 引用它以触发跨 TU 链接检查。 */
#define NS_PLATFORM_ABI_PROBE ns_platform_abi_host

/* ================================================================== */
/*  平台类型                                                             */
/* ================================================================== */

/** 平台可等待原语的句柄：Windows 为内核对象句柄。 */
typedef HANDLE ns_waitable_handle_t;

/** 句柄有效性谓词：排除 NULL 与 `ns_waitable_init` 写出的全 1 哨兵——在 Windows
    上全 1 就是 INVALID_HANDLE_VALUE，判定必须与 `ns_waitable_init` 的 docstring
    （"句柄型平台对应无效句柄"）一致。 */
#define ns_waitable_handle_is_valid(h) \
    (((h) != NULL) && ((h) != INVALID_HANDLE_VALUE))

/** wakeup：自动重置事件对象句柄。 */
typedef HANDLE ns_platform_wakeup_t;

/** mutex：SRW 锁，按值内嵌。 */
typedef SRWLOCK ns_platform_mutex_t;

/** thread：线程句柄 + 入口/参数簿记（trampoline 从本结构体读取）。 */
typedef struct ns_platform_thread {
    HANDLE                thread;
    ns_platform_thread_fn entry;
    void                 *arg;
} ns_platform_thread_t;

/** waitset 可容纳的 handle 总数。 */
#define NS_PLATFORM_WAITSET_MAX_HANDLES 64u
/** 预留给内部 timer handle 后，可注册的调用方 waitable 上限。 */
#define NS_PLATFORM_WAITSET_USER_HANDLES (NS_PLATFORM_WAITSET_MAX_HANDLES - 1u)

/** waitset：WFMO 返回 index，故需内部存 handle 数组与 index→waitable 映射。 */
typedef struct ns_platform_waitset {
    HANDLE                             handles[NS_PLATFORM_WAITSET_MAX_HANDLES];
    const struct ns_platform_waitable *waitables[NS_PLATFORM_WAITSET_USER_HANDLES];
    DWORD                              count;
    HANDLE                             timer;
} ns_platform_waitset_t;

#endif
