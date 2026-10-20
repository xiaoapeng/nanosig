/**
 * @file nanosig_platform_port.h
 * @brief nanosig 平台类型 — macOS 后端（构建内头，非安装头）。
 * @date 2026-10-17
 *
 * 本头文件把 nanosig 平台层使用的原语类型映射为 macOS 原语的**透明别名**，
 * 使平台对象全部变为调用方拥有存储。它只装当前平台这一个后端，由构建系统把
 * `platform/<os>/` 加入 include 路径后，按固定名 `nanosig_platform_port.h`
 * 被 `nanosig_port.h` 引入；`nanosig_port.h` 本身不含任何平台判定宏。
 *
 * 依赖：pthreads 与 kqueue（系统库）。本头文件会传递给公共头文件的使用者，
 * 因此使用者需要能看到 pthread 头文件。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_PLATFORM_PORT_H
#define NANOSIG_PLATFORM_PORT_H

#include <pthread.h>

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

/** 平台可等待原语的句柄：macOS 为文件描述符。 */
typedef int ns_waitable_handle_t;

/** 句柄有效性谓词：POSIX fd 非负即有效。 */
#define ns_waitable_handle_is_valid(h) ((h) >= 0)

/** wakeup：kqueue 实例描述符（EVFILT_USER + NOTE_TRIGGER），无额外存储。 */
typedef int ns_platform_wakeup_t;

/** mutex：pthread 互斥量，按值内嵌。 */
typedef pthread_mutex_t ns_platform_mutex_t;

/** thread：pthread 句柄 + 入口/参数簿记（trampoline 从本结构体读取）。 */
typedef struct ns_platform_thread {
    pthread_t             thread;
    ns_platform_thread_fn entry;
    void                 *arg;
} ns_platform_thread_t;

/** waitset：kqueue 实例；kevent udata 直接指向调用方 waitable，无需内部存储。 */
typedef struct ns_platform_waitset {
    int    kq;
    size_t count;
} ns_platform_waitset_t;

#endif
