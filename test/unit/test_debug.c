/**
 * @file test_debug.c
 * @brief Debug module unit tests.
 * @date 2026-07-12
 *
 * Uses platform_stdout_write capture to verify debug output without
 * writing to real stdout.
 *
 * NOTE: _ns_stdout is NS_STREAM_TYPE_FUNCTION (cached write). The cache
 * is flushed to platform_stdout_write only on '\n'. All ns_dbg_raw format
 * strings in this test include an explicit newline for that reason.
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include <nanosig/nanosig.h>
#include <nanosig/ns_debug.h>

#include "test_macros.h"

/* ================================================================== */
/*  Capture buffer — overrides the weak platform_stdout_write           */
/* ================================================================== */

#define TEST_OUTPUT_CAPACITY 4096u

static char g_output[TEST_OUTPUT_CAPACITY];
static size_t g_output_len;

/** Strong override of the weak platform_stdout_write — captures output. */
void platform_stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    size_t to_copy;
    size_t avail;

    (void)stream;
    if(buf == NULL || size == 0u) return;
    if(g_output_len >= TEST_OUTPUT_CAPACITY - 1u) return;

    avail = (TEST_OUTPUT_CAPACITY - 1u) - g_output_len;
    to_copy = (size < avail) ? size : avail;
    if(to_copy > 0u){
        memcpy(g_output + g_output_len, buf, to_copy);
        g_output_len += to_copy;
        g_output[g_output_len] = '\0';
    }
}

static void output_reset(void)
{
    g_output_len = 0u;
    g_output[0] = '\0';
}

static int output_contains(const char *needle)
{
    return (needle != NULL) ? (strstr(g_output, needle) != NULL) : 0;
}

/* ================================================================== */
/*  Tests — ns_dbg_set_level                                            */
/* ================================================================== */

static int test_set_level_returns_0(void)
{
    EXPECT_OK(ns_dbg_set_level(NS_DBG_SUPPRESS) == 0);
    EXPECT_OK(ns_dbg_set_level(NS_DBG_DEBUG) == 0);
    EXPECT_OK(ns_dbg_set_level(NS_DBG_ERR) == 0);
    EXPECT_OK(ns_dbg_set_level(NS_DBG_WARNING) == 0);
    EXPECT_OK(ns_dbg_set_level(NS_DBG_INFO) == 0);
    EXPECT_OK(ns_dbg_set_level(NS_DBG_SYS) == 0);
    return 0;
}

/* ================================================================== */
/*  Tests — level gating                                                */
/* ================================================================== */

static int test_level_suppress_all(void)
{
    ns_dbg_set_level(NS_DBG_SUPPRESS);

    output_reset();
    EXPECT_EQ(ns_dbg_raw(NS_DBG_ERR, 0, "err msg\n"), 0);
    EXPECT_EQ(g_output_len, 0u);

    output_reset();
    EXPECT_EQ(ns_dbg_raw(NS_DBG_DEBUG, 0, "debug msg\n"), 0);
    EXPECT_EQ(g_output_len, 0u);

    return 0;
}

static int test_level_err_only(void)
{
    ns_dbg_set_level(NS_DBG_ERR);

    /* ERR passes */
    output_reset();
    ns_dbg_raw(NS_DBG_ERR, 0, "err msg\n");
    EXPECT_OK(output_contains("err msg"));

    /* WARNING is suppressed (WARNING = 2 > ERR = 1) */
    output_reset();
    EXPECT_EQ(ns_dbg_raw(NS_DBG_WARNING, 0, "warn msg\n"), 0);
    EXPECT_EQ(g_output_len, 0u);

    /* DEBUG is suppressed */
    output_reset();
    EXPECT_EQ(ns_dbg_raw(NS_DBG_DEBUG, 0, "debug msg\n"), 0);
    EXPECT_EQ(g_output_len, 0u);

    return 0;
}

static int test_level_debug_all(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);

    /* All levels produce output at DEBUG level */
    output_reset();
    EXPECT_OK(ns_dbg_raw(NS_DBG_ERR, 0, "err\n") > 0);
    EXPECT_OK(output_contains("err"));

    output_reset();
    EXPECT_OK(ns_dbg_raw(NS_DBG_DEBUG, 0, "debug\n") > 0);
    EXPECT_OK(output_contains("debug"));

    output_reset();
    EXPECT_OK(ns_dbg_raw(NS_DBG_INFO, 0, "info\n") > 0);
    EXPECT_OK(output_contains("info"));

    output_reset();
    EXPECT_OK(ns_dbg_raw(NS_DBG_WARNING, 0, "warn\n") > 0);
    EXPECT_OK(output_contains("warn"));

    output_reset();
    EXPECT_OK(ns_dbg_raw(NS_DBG_SYS, 0, "sys\n") > 0);
    EXPECT_OK(output_contains("sys"));

    return 0;
}

/* ================================================================== */
/*  Tests — ns_dbg_raw output flags                                     */
/* ================================================================== */

static int test_dbg_raw_no_flags(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);

    output_reset();
    ns_dbg_raw(NS_DBG_ERR, 0, "hello\n");
    EXPECT_OK(output_contains("hello"));

    /* With flags=0, no prefix tag or monotonic clock should appear */
    EXPECT_EQ(output_contains("[ERR]"), 0);
    EXPECT_EQ(output_contains("["), 0);

    return 0;
}

static int test_dbg_raw_debug_tag(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);

    output_reset();
    ns_dbg_raw(NS_DBG_ERR, NS_DBG_FLAGS_DEBUG_TAG, "tagged\n");
    EXPECT_OK(output_contains(" ERR]"));  /* %4s right-justifies */
    EXPECT_OK(output_contains("tagged"));

    output_reset();
    ns_dbg_raw(NS_DBG_DEBUG, NS_DBG_FLAGS_DEBUG_TAG, "debug\n");
    EXPECT_OK(output_contains(" DBG]"));  /* %4s right-justifies */

    output_reset();
    ns_dbg_raw(NS_DBG_INFO, NS_DBG_FLAGS_DEBUG_TAG, "info\n");
    EXPECT_OK(output_contains("INFO]"));

    output_reset();
    ns_dbg_raw(NS_DBG_WARNING, NS_DBG_FLAGS_DEBUG_TAG, "warn\n");
    EXPECT_OK(output_contains("WARN]"));

    output_reset();
    ns_dbg_raw(NS_DBG_SYS, NS_DBG_FLAGS_DEBUG_TAG, "sys\n");
    EXPECT_OK(output_contains(" SYS]"));  /* %4s right-justifies */

    return 0;
}

static int test_dbg_raw_monotonic_clock(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);

    output_reset();
    ns_dbg_raw(NS_DBG_ERR, NS_DBG_FLAGS_MONOTONIC_CLOCK, "timed\n");

    /* Expect: [digits.digits] msg */
    EXPECT_EQ(g_output[0], '[');

    /* Should contain a dot for the microsecond separator */
    EXPECT_OK(output_contains("."));
    EXPECT_OK(output_contains("timed"));

    return 0;
}

static int test_dbg_raw_multiple_flags(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);

    output_reset();
    ns_dbg_raw(NS_DBG_ERR,
        NS_DBG_FLAGS_MONOTONIC_CLOCK | NS_DBG_FLAGS_DEBUG_TAG,
        "multi\n");
    EXPECT_OK(output_contains(" ERR]"));  /* %4s right-justifies */
    EXPECT_OK(output_contains("multi"));

    return 0;
}

/* ================================================================== */
/*  Tests — ns_vdbg_raw                                                */
/* ================================================================== */

static int ns_vdbg_printf(const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = ns_vdbg_raw(NS_DBG_ERR, NS_DBG_FLAGS_DEBUG_TAG, fmt, args);
    va_end(args);
    return n;
}

static int test_vdbg_raw_via_helper(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);

    output_reset();
    ns_vdbg_printf("vdbg %d %s\n", 42, "works");
    EXPECT_OK(output_contains(" ERR]"));  /* %4s right-justifies */
    EXPECT_OK(output_contains("vdbg 42 works"));

    return 0;
}

/* ================================================================== */
/*  Tests — Return value                                                */
/* ================================================================== */

static int test_dbg_raw_return_value(void)
{
    int n;

    ns_dbg_set_level(NS_DBG_DEBUG);

    /* Return value counts characters written */
    output_reset();
    n = ns_dbg_raw(NS_DBG_ERR, 0, "12345\n");
    EXPECT_EQ(n, 6); /* 5 chars + '\n' */

    /* Suppressed level returns 0 */
    ns_dbg_set_level(NS_DBG_SUPPRESS);
    n = ns_dbg_raw(NS_DBG_ERR, 0, "12345\n");
    EXPECT_EQ(n, 0);

    ns_dbg_set_level(NS_DBG_DEBUG);
    return 0;
}

static int test_vdbg_raw_return_value(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();

    /* Use the helper which already wraps ns_vdbg_raw in a variadic function */
    int n = ns_vdbg_printf("vdbg rv\n");

    EXPECT_OK(n > 0);
    EXPECT_OK(output_contains("vdbg rv"));

    return 0;
}

/* ================================================================== */
/*  Tests — Convenience macros                                          */
/* ================================================================== */

static int test_errln(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_errln("error msg %d", 1);
    EXPECT_OK(output_contains("error msg 1"));
    return 0;
}

static int test_warnln(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_warnln("warn msg");
    EXPECT_OK(output_contains("warn msg"));
    return 0;
}

static int test_sysln(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_sysln("sys msg");
    EXPECT_OK(output_contains("sys msg"));
    return 0;
}

static int test_infoln(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_infoln("info msg");
    EXPECT_OK(output_contains("info msg"));
    return 0;
}

static int test_debugln(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_debugln("debug msg");
    EXPECT_OK(output_contains("debug msg"));
    return 0;
}

static int test_errfl_has_function_line(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_errfl("funcline");
    EXPECT_OK(output_contains("test_errfl_has_function_line"));
    EXPECT_OK(output_contains("funcline"));
    return 0;
}

static int test_sysfl_has_function_line(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_sysfl("sys func");
    EXPECT_OK(output_contains("test_sysfl_has_function_line"));
    return 0;
}

/* *_raw macros: these use flags=0 and no \n, so the output stays
   in the cache and never reaches platform_stdout_write.
   We test them indirectly by verifying the return value is correct,
   and that no [PREFIX] appears in what little we DO flush. */

static int test_debugraw_no_prefix(void)
{
    int n;

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    n = ns_debugraw("raw %s", "data");
    /* Return value is correct even before flush */
    EXPECT_EQ(n, 8); /* "raw data" = 8 */

    /* Nothing flushed yet — add \n to flush cache */
    ns_printf("\n");
    EXPECT_OK(output_contains("raw data"));
    /* No prefix brackets in raw mode */
    EXPECT_EQ(output_contains("["), 0);
    return 0;
}

static int test_warnraw_no_prefix(void)
{
    int n;

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    n = ns_warnraw("warn raw %d", 99);
    EXPECT_EQ(n, 11); /* "warn raw 99" = 11 */

    /* Force flush */
    ns_printf("\n");
    EXPECT_OK(output_contains("warn raw 99"));
    return 0;
}

/* ================================================================== */
/*  Tests — ns_dbg_hex                                                 */
/* ================================================================== */

static int test_dbg_hex_empty(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_dbg_hex(NS_DBG_ERR, 0, 0u, NULL);
    /* Empty buffer: only header and footer, no data lines */
    EXPECT_OK(output_contains("| 0| 1| 2|"));  /* header column labels */
    EXPECT_OK(output_contains("--------------"));  /* footer */
    return 0;
}

static int test_dbg_hex_5_bytes(void)
{
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_dbg_hex(NS_DBG_ERR, 0, sizeof(data), data);

    /* Should have one data line with offset 0x00000000 */
    EXPECT_OK(output_contains("0x00000000"));
    /* Bytes should be present in hex representation (space-separated via %q) */
    EXPECT_OK(output_contains("de ad be ef 42"));
    return 0;
}

static int test_dbg_hex_exact_16(void)
{
    const uint8_t data[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    };

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_dbg_hex(NS_DBG_ERR, 0, sizeof(data), data);

    EXPECT_OK(output_contains("0x00000000"));
    EXPECT_OK(output_contains("00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff"));
    return 0;
}

static int test_dbg_hex_48_bytes(void)
{
    uint8_t data[48];
    size_t i;

    for(i = 0u; i < sizeof(data); i++){
        data[i] = (uint8_t)i;
    }

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_dbg_hex(NS_DBG_ERR, 0, sizeof(data), data);

    /* Three full lines: offsets 0x00000000, 0x00000010, 0x00000020 */
    EXPECT_OK(output_contains("0x00000000"));
    EXPECT_OK(output_contains("0x00000010"));
    EXPECT_OK(output_contains("0x00000020"));

    /* First line should start with 00010203... */
    EXPECT_OK(output_contains("00 01 02 03 04 05 06 07"));
    return 0;
}

static int test_dbg_hex_suppressed(void)
{
    ns_dbg_set_level(NS_DBG_SUPPRESS);
    output_reset();
    ns_dbg_hex(NS_DBG_ERR, 0, 16u, (const void *)"ABCDEFGHIJKLMNOP");
    /* Suppressed level: no output */
    EXPECT_EQ(g_output_len, 0u);
    return 0;
}

static int test_debughex_macro(void)
{
    const uint8_t data[] = {0x01, 0x02, 0x03};

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_debughex(data, sizeof(data));
    EXPECT_OK(output_contains("| 0| 1| 2|"));  /* header */
    EXPECT_OK(output_contains("01 02 03"));
    return 0;
}

static int test_errhex_macro(void)
{
    const uint8_t data[] = {0xFF, 0xEE, 0xDD};

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_errhex(data, sizeof(data));
    EXPECT_OK(output_contains("ff ee dd"));
    return 0;
}

/* ================================================================== */
/*  Tests — Module-level macros                                         */
/* ================================================================== */

/* Define module level as 5 = NS_DBG_DEBUG for this test section */
#define NS_DBG_MODULE_LEVEL_DMOD 5

static int test_module_mdebugln(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_mdebugln(DMOD, "module debug %d", 42);
    EXPECT_OK(output_contains("[DMOD]"));
    EXPECT_OK(output_contains("module debug 42"));
    return 0;
}

static int test_module_merrln(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_merrln(DMOD, "err in module");
    EXPECT_OK(output_contains("[DMOD]"));
    EXPECT_OK(output_contains("err in module"));
    return 0;
}

static int test_module_msysln(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_msysln(DMOD, "sys in module");
    EXPECT_OK(output_contains("[DMOD]"));
    EXPECT_OK(output_contains("sys in module"));
    return 0;
}

static int test_module_mdebugfl_has_function(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_mdebugfl(DMOD, "func in module");
    EXPECT_OK(output_contains("[DMOD]"));
    EXPECT_OK(output_contains("test_module_mdebugfl_has_function"));
    EXPECT_OK(output_contains("func in module"));
    return 0;
}

static int test_module_minforaw(void)
{
    int n;

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    n = ns_minforaw(DMOD, "raw info");
    /* Return value correct before flush */
    EXPECT_EQ(n, 8); /* "raw info" = 8 */

    /* Force flush */
    ns_printf("\n");
    EXPECT_OK(output_contains("raw info"));
    /* No [DMOD] or other prefix in raw mode */
    EXPECT_EQ(output_contains("["), 0);
    return 0;
}

/* ================================================================== */
/*  Tests — level gating combined with flags                            */
/* ================================================================== */

static int test_level_filter_with_flags(void)
{
    ns_dbg_set_level(NS_DBG_WARNING);

    /* WARNING level passes with flags */
    output_reset();
    ns_dbg_raw(NS_DBG_WARNING, NS_DBG_FLAGS_DEBUG_TAG, "warn\n");
    EXPECT_OK(output_contains("[WARN]"));

    /* INFO is suppressed even with flags */
    output_reset();
    EXPECT_EQ(ns_dbg_raw(NS_DBG_INFO, NS_DBG_FLAGS_DEBUG_TAG, "info\n"), 0);
    EXPECT_EQ(g_output_len, 0u);

    return 0;
}

/* ================================================================== */
/*  Tests — NS_DBG_ERROR_EXEC macro                                     */
/* ================================================================== */

static volatile int g_error_exec_flag;

static int test_dbg_error_exec_true(void)
{
    g_error_exec_flag = 0;

    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    NS_DBG_ERROR_EXEC(1 == 1, g_error_exec_flag = 1);
    EXPECT_EQ(g_error_exec_flag, 1);
    /* Should have printed something */
    EXPECT_OK(g_output_len > 0u);

    return 0;
}

static int test_dbg_error_exec_false(void)
{
    g_error_exec_flag = 0;

    output_reset();
    NS_DBG_ERROR_EXEC(1 == 0, g_error_exec_flag = 1);
    EXPECT_EQ(g_error_exec_flag, 0);
    /* With false condition, the action is skipped AND
       the dbg_printfl is inside the if body. So output should be empty. */
    EXPECT_EQ(g_output_len, 0u);

    return 0;
}

/* ================================================================== */
/*  Tests — edge cases                                                  */
/* ================================================================== */

static int test_dbg_raw_nested_fmt(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_dbg_raw(NS_DBG_ERR, 0, "%% %d %s %x\n", 42, "ok", 0xFF);
    EXPECT_OK(output_contains("% 42 ok ff"));
    return 0;
}

/* WALL_CLOCK flag is a placeholder (no-op), verify it doesn't crash */
static int test_dbg_raw_wall_clock_flag(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_dbg_raw(NS_DBG_ERR, NS_DBG_FLAGS_WALL_CLOCK, "wall\n");
    EXPECT_OK(output_contains("wall"));
    return 0;
}

/* Level gating at SYS (3) and INFO (4) boundaries */
static int test_level_filter_at_sys(void)
{
    ns_dbg_set_level(NS_DBG_SYS);

    output_reset();
    ns_dbg_raw(NS_DBG_SYS, 0, "sys pass\n");
    EXPECT_OK(output_contains("sys pass"));

    output_reset();
    EXPECT_EQ(ns_dbg_raw(NS_DBG_INFO, 0, "info suppressed\n"), 0);
    EXPECT_EQ(g_output_len, 0u);
    return 0;
}

static int test_level_filter_at_info(void)
{
    ns_dbg_set_level(NS_DBG_INFO);

    output_reset();
    EXPECT_OK(ns_dbg_raw(NS_DBG_INFO, 0, "info pass\n") > 0);

    output_reset();
    EXPECT_EQ(ns_dbg_raw(NS_DBG_DEBUG, 0, "debug suppressed\n"), 0);
    EXPECT_EQ(g_output_len, 0u);
    return 0;
}

/* Empty format string — verify no crash and correct return value */
static int test_dbg_raw_empty_fmt(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    output_reset();
    ns_dbg_raw(NS_DBG_ERR, 0, "\n");
    EXPECT_OK(output_contains("\n"));
    return 0;
}

/* ns_dbg_hex + mutex path — call after init to exercise mutex path */
static int test_dbg_hex_after_init(void)
{
    const uint8_t data[] = {0xAA, 0xBB};

    ns_dbg_set_level(NS_DBG_DEBUG);
    EXPECT_OK(ns_init() == NS_OK);

    output_reset();
    ns_dbg_hex(NS_DBG_ERR, 0, sizeof(data), data);
    EXPECT_OK(output_contains("aa bb"));

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* ================================================================== */
/*  Tests — init / shutdown                                             */
/* ================================================================== */

static int test_init_shutdown_lifecycle(void)
{
    EXPECT_OK(ns_init() == NS_OK);
    /* Second init should return EXISTS */
    EXPECT_EQ(ns_init(), NS_E_EXISTS);
    EXPECT_OK(ns_shutdown() == NS_OK);
    /* Double shutdown is safe */
    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

static int test_dbg_output_after_init(void)
{
    ns_dbg_set_level(NS_DBG_DEBUG);
    EXPECT_OK(ns_init() == NS_OK);

    output_reset();
    ns_errln("after init %s", "works");
    EXPECT_OK(output_contains("after init works"));

    EXPECT_OK(ns_shutdown() == NS_OK);
    return 0;
}

/* Direct ns_debug_init/shutdown calls */
static int test_debug_init_direct(void)
{
    EXPECT_OK(ns_debug_init() == NS_OK);
    /* Double init returns EXISTS */
    EXPECT_EQ(ns_debug_init(), NS_E_EXISTS);
    /* Double shutdown is safe */
    ns_debug_shutdown();
    ns_debug_shutdown();
    return 0;
}

/* ================================================================== */
/*  main                                                                */
/* ================================================================== */

int main(void)
{
    /* All level-gating tests require baseline level setup */
    ns_dbg_set_level(NS_DBG_DEBUG);

    /* ===== Level gating ===== */
    if(test_set_level_returns_0() != 0) return 1;
    if(test_level_suppress_all() != 0) return 1;
    if(test_level_err_only() != 0) return 1;
    if(test_level_debug_all() != 0) return 1;

    /* ===== ns_dbg_raw output flags ===== */
    if(test_dbg_raw_no_flags() != 0) return 1;
    if(test_dbg_raw_debug_tag() != 0) return 1;
    if(test_dbg_raw_monotonic_clock() != 0) return 1;
    if(test_dbg_raw_multiple_flags() != 0) return 1;
    if(test_dbg_raw_wall_clock_flag() != 0) return 1;

    /* ===== ns_vdbg_raw ===== */
    if(test_vdbg_raw_via_helper() != 0) return 1;

    /* ===== Return value ===== */
    if(test_dbg_raw_return_value() != 0) return 1;
    if(test_vdbg_raw_return_value() != 0) return 1;

    /* ===== Convenience macros ===== */
    if(test_errln() != 0) return 1;
    if(test_warnln() != 0) return 1;
    if(test_sysln() != 0) return 1;
    if(test_infoln() != 0) return 1;
    if(test_debugln() != 0) return 1;
    if(test_errfl_has_function_line() != 0) return 1;
    if(test_sysfl_has_function_line() != 0) return 1;
    if(test_debugraw_no_prefix() != 0) return 1;
    if(test_warnraw_no_prefix() != 0) return 1;

    /* ===== ns_dbg_hex ===== */
    if(test_dbg_hex_empty() != 0) return 1;
    if(test_dbg_hex_5_bytes() != 0) return 1;
    if(test_dbg_hex_exact_16() != 0) return 1;
    if(test_dbg_hex_48_bytes() != 0) return 1;
    if(test_dbg_hex_suppressed() != 0) return 1;

    /* ===== hex macros ===== */
    if(test_debughex_macro() != 0) return 1;
    if(test_errhex_macro() != 0) return 1;

    /* ===== Module-level macros ===== */
    if(test_module_mdebugln() != 0) return 1;
    if(test_module_merrln() != 0) return 1;
    if(test_module_msysln() != 0) return 1;
    if(test_module_mdebugfl_has_function() != 0) return 1;
    if(test_module_minforaw() != 0) return 1;

    /* ===== Level gating + flags ===== */
    if(test_level_filter_with_flags() != 0) return 1;
    if(test_level_filter_at_sys() != 0) return 1;
    if(test_level_filter_at_info() != 0) return 1;

    /* ===== NS_DBG_ERROR_EXEC ===== */
    if(test_dbg_error_exec_true() != 0) return 1;
    if(test_dbg_error_exec_false() != 0) return 1;

    /* ===== Edge cases ===== */
    if(test_dbg_raw_nested_fmt() != 0) return 1;
    if(test_dbg_raw_empty_fmt() != 0) return 1;

    /* ===== init/shutdown integration — run last ===== */
    if(test_init_shutdown_lifecycle() != 0) return 1;
    if(test_debug_init_direct() != 0) return 1;
    if(test_dbg_output_after_init() != 0) return 1;
    if(test_dbg_hex_after_init() != 0) return 1;

    return 0;
}
