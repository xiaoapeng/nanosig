/**
 * @file ns_debug.h
 * @brief nanosig 调试输出接口 — 移植自 eventhub_os eh_debug.h。
 * @date 2026-07-11
 *
 * 提供分级调试输出宏（ns_errln / ns_warnln / ns_infoln / ns_debugln 等），
 * 支持简单宏和模块级宏两种使用方式。模块级宏可通过编译选项
 * -DNS_DBG_MODULE_LEVEL_<name>=<level> 精细控制各模块输出等级。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_DEBUG_H
#define NANOSIG_DEBUG_H

#include <stddef.h>
#include <stdarg.h>

#include <nanosig/nanosig_types.h>
#include <nanosig/ns_formatio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  调试等级                                                            */
/* ================================================================== */

enum ns_dbg_level {
#define NS_DBG_SUPPRESS                 0
#define NS_DBG_ERR                      1
#define NS_DBG_WARNING                  2
#define NS_DBG_SYS                      3
#define NS_DBG_INFO                     4
#define NS_DBG_DEBUG                    5
    _NS_DBG_SUPPRESS = NS_DBG_SUPPRESS,
    _NS_DBG_ERR       = NS_DBG_ERR,
    _NS_DBG_WARNING   = NS_DBG_WARNING,
    _NS_DBG_SYS       = NS_DBG_SYS,
    _NS_DBG_INFO      = NS_DBG_INFO,
    _NS_DBG_DEBUG     = NS_DBG_DEBUG,
};

/* ================================================================== */
/*  调试标志                                                            */
/* ================================================================== */

enum ns_dbg_flags {
    NS_DBG_FLAGS_WALL_CLOCK         = 0x01u,
    NS_DBG_FLAGS_MONOTONIC_CLOCK    = 0x02u,
    NS_DBG_FLAGS_DEBUG_TAG          = 0x04u,
};

#ifndef NS_CONFIG_DEBUG_FLAGS
#define NS_CONFIG_DEBUG_FLAGS \
    (NS_DBG_FLAGS_DEBUG_TAG | NS_DBG_FLAGS_MONOTONIC_CLOCK)
#endif

#define NS_DBG_FLAGS NS_CONFIG_DEBUG_FLAGS

/* ================================================================== */
/*  换行符                                                              */
/* ================================================================== */

#ifndef NS_CONFIG_DEBUG_ENTER_SIGN
#define NS_CONFIG_DEBUG_ENTER_SIGN "\n"
#endif

#define NS_DEBUG_ENTER_SIGN NS_CONFIG_DEBUG_ENTER_SIGN

/* ================================================================== */
/*  内部辅助                                                            */
/* ================================================================== */

static inline int _ns_dbg_feign_return(int n) { return n; }

#define NS_MACRO_DEBUG_LEVEL(name) ((uint32_t)(NS_STRINGIFY(name)[0] - '0'))

/* ================================================================== */
/*  模块级宏（推荐用于生产代码）                                          */
/* ================================================================== */

#define ns_mprintfl(name, tag, level, fmt, ...) ({ \
        int n = 0; \
        if (NS_MACRO_DEBUG_LEVEL(name) >= (uint32_t)(level)) { \
            n = ns_dbg_printfl((level), "[" #tag "] ", fmt, ##__VA_ARGS__); \
        } \
        _ns_dbg_feign_return(n); \
    })

#define ns_mprintln(name, tag, level, fmt, ...) ({ \
        int n = 0; \
        if (NS_MACRO_DEBUG_LEVEL(name) >= (uint32_t)(level)) { \
            n = ns_dbg_println((level), "[" #tag "] ", fmt, ##__VA_ARGS__); \
        } \
        _ns_dbg_feign_return(n); \
    })

#define ns_mprintraw(name, level, fmt, ...) ({ \
        int n = 0; \
        if (NS_MACRO_DEBUG_LEVEL(name) >= (uint32_t)(level)) { \
            n = ns_dbg_printraw((level), fmt, ##__VA_ARGS__); \
        } \
        _ns_dbg_feign_return(n); \
    })

#define ns_mhex(name, level, buf, len) ({ \
        int n = 0; \
        if (NS_MACRO_DEBUG_LEVEL(name) >= (uint32_t)(level)) { \
            n = ns_dbg_hex((level), NS_DBG_FLAGS, (len), (buf)); \
        } \
        _ns_dbg_feign_return(n); \
    })

/* ================================================================== */
/*  公开 API 声明                                                        */
/* ================================================================== */

extern int ns_dbg_set_level(enum ns_dbg_level level);
extern int ns_dbg_raw(enum ns_dbg_level level, enum ns_dbg_flags flags,
    const char *fmt, ...);
extern int ns_vdbg_raw(enum ns_dbg_level level, enum ns_dbg_flags flags,
    const char *fmt, va_list args);
extern int ns_dbg_hex(enum ns_dbg_level level, enum ns_dbg_flags flags,
    size_t len, const void *buf);

/* ================================================================== */
/*  便捷宏                                                              */
/* ================================================================== */

#define ns_dbg_println(level, tag_str, fmt, ...) \
    ns_dbg_raw((level), NS_DBG_FLAGS, \
        (tag_str) fmt NS_DEBUG_ENTER_SIGN, ##__VA_ARGS__)
#define ns_dbg_printfl(level, tag_str, fmt, ...) \
    ns_dbg_raw((level), NS_DBG_FLAGS, \
        (tag_str) "[%s, %d]: " fmt NS_DEBUG_ENTER_SIGN, \
        __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ns_dbg_printraw(level, fmt, ...) \
    ns_dbg_raw((level), 0, fmt, ##__VA_ARGS__)

/* ---- 带自动回车的版本 ---- */
#define ns_debugln(fmt, ...)   ns_dbg_println(NS_DBG_DEBUG, "", fmt, ##__VA_ARGS__)
#define ns_infoln(fmt, ...)    ns_dbg_println(NS_DBG_INFO,  "", fmt, ##__VA_ARGS__)
#define ns_sysln(fmt, ...)     ns_dbg_println(NS_DBG_SYS,   "", fmt, ##__VA_ARGS__)
#define ns_warnln(fmt, ...)    ns_dbg_println(NS_DBG_WARNING, "", fmt, ##__VA_ARGS__)
#define ns_errln(fmt, ...)     ns_dbg_println(NS_DBG_ERR,   "", fmt, ##__VA_ARGS__)

/* ---- 带自动回车和函数定位 ---- */
#define ns_debugfl(fmt, ...)   ns_dbg_printfl(NS_DBG_DEBUG, "", fmt, ##__VA_ARGS__)
#define ns_infofl(fmt, ...)    ns_dbg_printfl(NS_DBG_INFO,  "", fmt, ##__VA_ARGS__)
#define ns_sysfl(fmt, ...)     ns_dbg_printfl(NS_DBG_SYS,   "", fmt, ##__VA_ARGS__)
#define ns_warnfl(fmt, ...)    ns_dbg_printfl(NS_DBG_WARNING, "", fmt, ##__VA_ARGS__)
#define ns_errfl(fmt, ...)     ns_dbg_printfl(NS_DBG_ERR,   "", fmt, ##__VA_ARGS__)

/* ---- 原始数据版本（无前缀、无回车） ---- */
#define ns_debugraw(fmt, ...)  ns_dbg_printraw(NS_DBG_DEBUG, fmt, ##__VA_ARGS__)
#define ns_inforaw(fmt, ...)   ns_dbg_printraw(NS_DBG_INFO,  fmt, ##__VA_ARGS__)
#define ns_sysraw(fmt, ...)    ns_dbg_printraw(NS_DBG_SYS,   fmt, ##__VA_ARGS__)
#define ns_warnraw(fmt, ...)   ns_dbg_printraw(NS_DBG_WARNING, fmt, ##__VA_ARGS__)
#define ns_errraw(fmt, ...)    ns_dbg_printraw(NS_DBG_ERR,   fmt, ##__VA_ARGS__)

/* ---- 16 进制数组打印 ---- */
#define ns_debughex(buf, len)  ns_dbg_hex(NS_DBG_DEBUG, NS_DBG_FLAGS, (len), (buf))
#define ns_infohex(buf, len)   ns_dbg_hex(NS_DBG_INFO,  NS_DBG_FLAGS, (len), (buf))
#define ns_syshex(buf, len)    ns_dbg_hex(NS_DBG_SYS,   NS_DBG_FLAGS, (len), (buf))
#define ns_warnhex(buf, len)   ns_dbg_hex(NS_DBG_WARNING, NS_DBG_FLAGS, (len), (buf))
#define ns_errhex(buf, len)    ns_dbg_hex(NS_DBG_ERR,   NS_DBG_FLAGS, (len), (buf))

/* ---- 模块级宏（编译时开关） ---- */
/* 使用方式：
 *   #define NS_DBG_MODULE_LEVEL_FOO NS_DBG_DEBUG
 *   void func(void) {
 *       ns_mdebugln(FOO, "hello %d", 42);
 *   }
 */
#define ns_mdebugln(name, fmt, ...) \
    ns_mprintln(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_DEBUG, fmt, ##__VA_ARGS__)
#define ns_minfoln(name, fmt, ...)  \
    ns_mprintln(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_INFO, fmt, ##__VA_ARGS__)
#define ns_msysln(name, fmt, ...)   \
    ns_mprintln(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_SYS, fmt, ##__VA_ARGS__)
#define ns_mwarnln(name, fmt, ...)  \
    ns_mprintln(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_WARNING, fmt, ##__VA_ARGS__)
#define ns_merrln(name, fmt, ...)   \
    ns_mprintln(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_ERR, fmt, ##__VA_ARGS__)

#define ns_mdebugfl(name, fmt, ...) \
    ns_mprintfl(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_DEBUG, fmt, ##__VA_ARGS__)
#define ns_minfofl(name, fmt, ...)  \
    ns_mprintfl(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_INFO, fmt, ##__VA_ARGS__)
#define ns_msysfl(name, fmt, ...)   \
    ns_mprintfl(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_SYS, fmt, ##__VA_ARGS__)
#define ns_mwarnfl(name, fmt, ...)  \
    ns_mprintfl(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_WARNING, fmt, ##__VA_ARGS__)
#define ns_merrfl(name, fmt, ...)   \
    ns_mprintfl(NS_DBG_MODULE_LEVEL_##name, name, NS_DBG_ERR, fmt, ##__VA_ARGS__)

#define ns_mdebugraw(name, fmt, ...) \
    ns_mprintraw(NS_DBG_MODULE_LEVEL_##name, NS_DBG_DEBUG, fmt, ##__VA_ARGS__)
#define ns_minforaw(name, fmt, ...)  \
    ns_mprintraw(NS_DBG_MODULE_LEVEL_##name, NS_DBG_INFO, fmt, ##__VA_ARGS__)
#define ns_msysraw(name, fmt, ...)   \
    ns_mprintraw(NS_DBG_MODULE_LEVEL_##name, NS_DBG_SYS, fmt, ##__VA_ARGS__)
#define ns_mwarnraw(name, fmt, ...)  \
    ns_mprintraw(NS_DBG_MODULE_LEVEL_##name, NS_DBG_WARNING, fmt, ##__VA_ARGS__)
#define ns_merrraw(name, fmt, ...)   \
    ns_mprintraw(NS_DBG_MODULE_LEVEL_##name, NS_DBG_ERR, fmt, ##__VA_ARGS__)

#define ns_mdebughex(name, buf, len) \
    ns_mhex(NS_DBG_MODULE_LEVEL_##name, NS_DBG_DEBUG, (buf), (len))
#define ns_minfohex(name, buf, len)  \
    ns_mhex(NS_DBG_MODULE_LEVEL_##name, NS_DBG_INFO, (buf), (len))
#define ns_msyshex(name, buf, len)   \
    ns_mhex(NS_DBG_MODULE_LEVEL_##name, NS_DBG_SYS, (buf), (len))
#define ns_mwarnhex(name, buf, len)  \
    ns_mhex(NS_DBG_MODULE_LEVEL_##name, NS_DBG_WARNING, (buf), (len))
#define ns_merrhex(name, buf, len)   \
    ns_mhex(NS_DBG_MODULE_LEVEL_##name, NS_DBG_ERR, (buf), (len))

/* ---- 错误检查宏 ---- */
#define NS_DBG_ERROR_EXEC(expression, action) do { \
    if (expression) { \
        ns_dbg_printfl(NS_DBG_ERR, "", \
            "(%s) execute {%s}", #expression, #action); \
        action; \
    } \
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_DEBUG_H */
