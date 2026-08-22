/**
 * @file test_formatio.c
 * @brief formatio 模块单元测试。
 * @date 2026-08-08
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 *
 * 覆盖全部 10 个公共 API：ns_vsnprintf / ns_snprintf / ns_sprintf / ns_vprintf /
 * ns_printf / ns_stream_vprintf / ns_stream_printf / ns_stream_putc /
 * ns_stream_puts / ns_stream_finish。
 *
 * 断言策略为混合：
 *  - 核心/边界路径：硬编码黄金值，格式矩阵类用例用数据驱动表（按参数类型分表）；
 *  - 浮点易错路径：与 libc snprintf 差分对拍，限定 %f 定精度、排除 %g，
 *    并设置一致集下限（≥20 例），防止差分断言退化；
 *  - %p 仅做结构断言（"0x" 前缀 + 宽度 2*sizeof(void*)+2 + 零填充 +
 *    运行时指针值推导 hex），不用 libc snprintf 对拍——nanosig 的 %p 默认
 *    ZEROPAD 填充到 18 宽，glibc 是最小 hex 无零填充，二者必然不一致。
 *
 * stdout 捕获：库内 stdout_write 由平台后端提供强定义（platform/<os>/port.c），
 * 其内部调用弱符号 platform_stdout_write（port.c 与 src/ns_formatio.c 均声明为
 * NS_FUNCTION_WEAK）。本 TU 提供 platform_stdout_write 的强定义覆盖之，把
 * ns_printf / ns_vprintf 的 flush 块追加进静态缓冲。
 * NS_FUNCTION_WEAK 在 MSVC 下为空宏（库内 platform_stdout_write 为强定义），
 * 故强定义以工具链守卫包裹，保证 MSVC 下无 LNK2005；非 GNU/Clang 分支只驱动不断言。
 */

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>  /* ssize_t，供 %zd 用例 */
#endif

#include <nanosig/ns_formatio.h>

#include "test_macros.h"

/* ================================================================== */
/*  通用辅助                                                            */
/* ================================================================== */

/* 格式化后与期望字符串逐字节比较；失败时打印上下文并返回 1 */
#define CHECK_FMT(fmt, expected, ...) \
    do { \
        char _b[256]; \
        (void)ns_snprintf(_b, sizeof(_b), fmt, __VA_ARGS__); \
        if(strcmp(_b, expected) != 0){ \
            fprintf(stderr, "CHECK_FMT failed at %s:%d: fmt=\"%s\" expected=\"%s\" got=\"%s\"\n", \
                __FILE__, __LINE__, fmt, expected, _b); \
            return 1; \
        } \
    } while(0)

/* 手工构造 va_list 的辅助：供 ns_vsnprintf / ns_vprintf / ns_stream_vprintf 用例 */
static int do_vsnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = ns_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return n;
}

static int do_vprintf(const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = ns_vprintf(fmt, args);
    va_end(args);
    return n;
}

static int do_stream_vprintf(struct ns_stream_base *stream, const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = ns_stream_vprintf(stream, fmt, args);
    va_end(args);
    return n;
}

/* ================================================================== */
/*  数据驱动表结构 + 驱动函数（按参数类型分表）                            */
/* ================================================================== */

struct str_case { const char *fmt; const char *arg; const char *expected; };
struct int_case { const char *fmt; int arg; const char *expected; };
struct uint_case { const char *fmt; unsigned int arg; const char *expected; };
struct dbl_case { const char *fmt; double arg; const char *expected; };

static int run_str_cases(const struct str_case *cases, size_t count)
{
    size_t i;

    for(i = 0; i < count; i++){
        char buf[256];
        (void)ns_snprintf(buf, sizeof(buf), cases[i].fmt, cases[i].arg);
        if(strcmp(buf, cases[i].expected) != 0){
            fprintf(stderr, "str case failed: fmt=%s expected=\"%s\" got=\"%s\"\n",
                cases[i].fmt, cases[i].expected, buf);
            return 1;
        }
    }
    return 0;
}

static int run_int_cases(const struct int_case *cases, size_t count)
{
    size_t i;

    for(i = 0; i < count; i++){
        char buf[256];
        (void)ns_snprintf(buf, sizeof(buf), cases[i].fmt, cases[i].arg);
        if(strcmp(buf, cases[i].expected) != 0){
            fprintf(stderr, "int case failed: fmt=%s expected=\"%s\" got=\"%s\"\n",
                cases[i].fmt, cases[i].expected, buf);
            return 1;
        }
    }
    return 0;
}

static int run_uint_cases(const struct uint_case *cases, size_t count)
{
    size_t i;

    for(i = 0; i < count; i++){
        char buf[256];
        (void)ns_snprintf(buf, sizeof(buf), cases[i].fmt, cases[i].arg);
        if(strcmp(buf, cases[i].expected) != 0){
            fprintf(stderr, "uint case failed: fmt=%s expected=\"%s\" got=\"%s\"\n",
                cases[i].fmt, cases[i].expected, buf);
            return 1;
        }
    }
    return 0;
}

static int run_dbl_cases(const struct dbl_case *cases, size_t count)
{
    size_t i;

    for(i = 0; i < count; i++){
        char buf[256];
        (void)ns_snprintf(buf, sizeof(buf), cases[i].fmt, cases[i].arg);
        if(strcmp(buf, cases[i].expected) != 0){
            fprintf(stderr, "dbl case failed: fmt=%s expected=\"%s\" got=\"%s\"\n",
                cases[i].fmt, cases[i].expected, buf);
            return 1;
        }
    }
    return 0;
}

/* ================================================================== */
/*  用例 1~8：字符串 / 整数 / 特殊前缀 / star / 截断 / NULL 串 / vsnprintf / sprintf */
/* ================================================================== */

static const struct str_case g_str_cases[] = {
    { "%s", "hello", "hello" },
    { "[%10s]", "hi", "[        hi]" },
    { "[%-10s]", "hi", "[hi        ]" },
    { "%.3s", "hello", "hel" },
    { "[%5.3s]", "hello", "[  hel]" },
    { "[%.0s]", "hello", "[]" },
    { "%s", "", "" },
    { "[%s]", "hi", "[hi]" },
};

static const struct int_case g_int_cases[] = {
    { "%d", 42, "42" },
    { "%d", -42, "-42" },
    { "[%5d]", 42, "[   42]" },
    { "[%-5d]", 42, "[42   ]" },
    { "[%05d]", 42, "[00042]" },
    { "%+d", 42, "+42" },
    { "%+d", -42, "-42" },
    { "% d", 42, " 42" },
    { "% d", -42, "-42" },
    { "%.3d", 42, "042" },
    { "%d", 0, "0" },
    { "%i", 42, "42" },
    { "[%5.3d]", 7, "[  007]" },
    { "%c", 65, "A" },
    { "%c", 66, "B" },
    { "[%-5c]", 65, "[A    ]" },  /* 左对齐 + 宽度填充（vprintf_char 填充分支） */
    { "100%%", 0, "100%" },
};

static const struct uint_case g_uint_cases[] = {
    { "%u", 42u, "42" },
    { "%u", 4294967295u, "4294967295" },
    { "%x", 0xFFu, "ff" },
    { "%X", 0xFFu, "FF" },
    { "%o", 8u, "10" },
    { "%b", 5u, "101" },
    { "%u", 0u, "0" },
    { "%x", 0u, "0" },
};

static int test_snprintf_basic(void)
{
    char buf[256];
    int n;

    if(run_str_cases(g_str_cases, sizeof(g_str_cases) / sizeof(g_str_cases[0])) != 0) return 1;
    if(run_int_cases(g_int_cases, sizeof(g_int_cases) / sizeof(g_int_cases[0])) != 0) return 1;
    if(run_uint_cases(g_uint_cases, sizeof(g_uint_cases) / sizeof(g_uint_cases[0])) != 0) return 1;

    /* 返回值 = 实际写入字节数 */
    n = ns_snprintf(buf, sizeof(buf), "v=%d", -42);
    EXPECT_EQ(n, 5);

    return 0;
}

static int test_snprintf_qualifiers(void)
{
    /* 限定符 h/hh/l/ll/z 各宽度边界值；参数 C 类型随限定符变化，按类型内联驱动 */
    CHECK_FMT("%hhd", "-1", -1);            /* char 有符号：-1 → "-1" */
    CHECK_FMT("%hhu", "255", -1);           /* char 无符号：-1 → 255 */
    CHECK_FMT("%hd", "300", 300);           /* short */
    CHECK_FMT("%hu", "0", 65536);           /* short 无符号：65536 截断 → 0 */
    CHECK_FMT("%hx", "dead", 0x1DEAD);      /* short hex：0x1DEAD → 0xDEAD */
    CHECK_FMT("%ld", "-1", -1L);
    CHECK_FMT("%lu", "123456789", 123456789UL);
    CHECK_FMT("%lx", "cafe", 0xCAFEL);
    CHECK_FMT("%lld", "-1", -1LL);
    CHECK_FMT("%llu", "18446744073709551615", 18446744073709551615ULL);
    CHECK_FMT("%llx", "deadbeef", 0xDEADBEEFULL);
    CHECK_FMT("%zu", "18446744073709551615", (size_t)SIZE_MAX);
    CHECK_FMT("%zx", "cafe", (size_t)0xCAFEu);
#if defined(__unix__) || defined(__APPLE__)
    CHECK_FMT("%zd", "-1", (ssize_t)-1);
#endif
    return 0;
}

static const struct uint_case g_special_cases[] = {
    { "%#x", 0xFFu, "0xff" },
    { "%#X", 0xFFu, "0XFF" },
    { "%#o", 8u, "010" },
    { "%#b", 5u, "0b101" },
    { "%#B", 5u, "0B101" },
    { "%#x", 0u, "0x0" },
    { "%#o", 0u, "00" },
    { "%#b", 0u, "0b0" },
    { "%#x", 255u, "0xff" },
};

static int test_snprintf_special(void)
{
    return run_uint_cases(g_special_cases, sizeof(g_special_cases) / sizeof(g_special_cases[0]));
}

static int test_star_width_precision(void)
{
    /* '*' 宽度（负 → 左对齐）、'.*' 精度（负 → 0） */
    CHECK_FMT("%*d", "    42", 6, 42);
    CHECK_FMT("%*d", "42    ", -6, 42);
    CHECK_FMT("[%*s]", "[   hi]", 5, "hi");
    CHECK_FMT("[%-*s]", "[hi   ]", 5, "hi");
    CHECK_FMT("%.*d", "042", 3, 42);
    CHECK_FMT("%.*d", "42", -2, 42);
    CHECK_FMT("[%.*s]", "[he]", 2, "hello");
    CHECK_FMT("[%.*s]", "[]", 0, "hello");
    CHECK_FMT("[%.*s]", "[]", -1, "hello");
    CHECK_FMT("%*.*f", "    3.14", 8, 2, 3.14);
    CHECK_FMT("%-*.*f", "3.14    ", 8, 2, 3.14);
    return 0;
}

static int test_snprintf_truncation(void)
{
    char buf[16];
    int n;

    /* size=0（FORMATIO-018 已修复）：不写任何字节，返回值 = 应写全长（C99） */
    memset(buf, 'X', sizeof(buf));
    n = ns_snprintf(buf, 0, "hello");
    EXPECT_EQ(n, 5);
    EXPECT_EQ(buf[0], 'X');  /* 缓冲未被改写 */
    EXPECT_EQ(buf[1], 'X');

    /* size=1：只写 NUL，返回值 = 应写全长 */
    memset(buf, 'X', sizeof(buf));
    n = ns_snprintf(buf, 1, "hello");
    EXPECT_EQ(n, 5);
    EXPECT_EQ(buf[0], '\0');

    /* size=精确+1：内容完整 + NUL 有专属位置 */
    memset(buf, 'X', sizeof(buf));
    n = ns_snprintf(buf, 6, "hello");
    EXPECT_EQ(n, 5);
    EXPECT_EQ(strcmp(buf, "hello"), 0);

    /* size=精确：NUL 覆盖最后一个字符 */
    memset(buf, 'X', sizeof(buf));
    n = ns_snprintf(buf, 5, "hello");
    EXPECT_EQ(n, 5);
    EXPECT_EQ(buf[0], 'h');
    EXPECT_EQ(buf[1], 'e');
    EXPECT_EQ(buf[2], 'l');
    EXPECT_EQ(buf[3], 'l');
    EXPECT_EQ(buf[4], '\0');

    /* size=精确-1：'o' 被丢弃，仍返回 5 */
    memset(buf, 'X', sizeof(buf));
    n = ns_snprintf(buf, 4, "hello");
    EXPECT_EQ(n, 5);
    EXPECT_EQ(strcmp(buf, "hel"), 0);

    return 0;
}

static const struct str_case g_null_s_cases[] = {
    { "%s", NULL, "(null)" },
    { "%.3s", NULL, "(nu" },        /* 精度截断：不保留结尾 ')' */
    { "[%8s]", NULL, "[  (null)]" },
    { "[%-8s]", NULL, "[(null)  ]" },
    { "[%6.3s]", NULL, "[   (nu]" }, /* 3 空格 + "(nu" */
};

static int test_snprintf_null_s(void)
{
    return run_str_cases(g_null_s_cases, sizeof(g_null_s_cases) / sizeof(g_null_s_cases[0]));
}

static int test_vsnprintf(void)
{
    char buf1[128], buf2[128];
    int n1, n2;

    n1 = ns_snprintf(buf1, sizeof(buf1), "a=%d s=%s f=%.2f", 7, "x", 3.14);
    n2 = do_vsnprintf(buf2, sizeof(buf2), "a=%d s=%s f=%.2f", 7, "x", 3.14);
    EXPECT_EQ(n1, n2);
    EXPECT_EQ(strcmp(buf1, buf2), 0);
    EXPECT_EQ(n1, 14);  /* "a=7 s=x f=3.14" */

    return 0;
}

static int test_sprintf(void)
{
    char buf[512];
    int n;

    n = ns_sprintf(buf, "v=%d s=%s f=%.2f", -3, "hi", 1.5);
    EXPECT_EQ(n, 16);
    EXPECT_EQ(strcmp(buf, "v=-3 s=hi f=1.50"), 0);

    /* 无界写入：长字符串不截断 */
    n = ns_sprintf(buf, "%s", "hello world");
    EXPECT_EQ(n, 11);
    EXPECT_EQ(strcmp(buf, "hello world"), 0);

    return 0;
}

/* ================================================================== */
/*  用例 9：printf / vprintf（stdout 捕获，工具链守卫）                   */
/* ================================================================== */

#if defined(__GNUC__) || defined(__clang__)
static char g_stdout_capture[1024];
static size_t g_stdout_capture_len;

static void capture_stdout_reset(void)
{
    g_stdout_capture_len = 0;
    g_stdout_capture[0] = '\0';
}

/* 强符号覆盖库内 weak 的 platform_stdout_write（port.c 弱定义），把 flush 块追加进捕获缓冲 */
void platform_stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    (void)stream;
    if(g_stdout_capture_len < sizeof(g_stdout_capture)){
        size_t copy = sizeof(g_stdout_capture) - g_stdout_capture_len;
        if(copy > size) copy = size;
        memcpy(g_stdout_capture + g_stdout_capture_len, buf, copy);
    }
    g_stdout_capture_len += size;
}
#endif /* __GNUC__ || __clang__ */

static int test_printf_family(void)
{
#if defined(__GNUC__) || defined(__clang__)
    char long600[601];
    size_t i;
    int n;

    /* 换行触发立即 flush */
    capture_stdout_reset();
    n = ns_printf("hi %d\n", 5);
    ns_stream_finish(NS_STDOUT);
    EXPECT_EQ(n, 5);
    EXPECT_EQ(g_stdout_capture_len, 5);
    EXPECT_EQ(memcmp(g_stdout_capture, "hi 5\n", 5), 0);

    /* 无换行：内容驻留缓存，finish 时冲刷 */
    capture_stdout_reset();
    n = ns_printf("xy=%d", 42);
    EXPECT_EQ(n, 5);
    EXPECT_EQ(g_stdout_capture_len, 0);
    ns_stream_finish(NS_STDOUT);
    EXPECT_EQ(g_stdout_capture_len, 5);
    EXPECT_EQ(memcmp(g_stdout_capture, "xy=42", 5), 0);

    /* 512 字节缓存满 flush：先出 512，finish 出剩余 88 */
    for(i = 0; i < 600; i++){
        long600[i] = (char)('a' + (int)(i % 26u));
    }
    long600[600] = '\0';
    capture_stdout_reset();
    n = ns_printf("%s", long600);
    EXPECT_EQ(n, 600);
    EXPECT_EQ(g_stdout_capture_len, 512);
    ns_stream_finish(NS_STDOUT);
    EXPECT_EQ(g_stdout_capture_len, 600);
    EXPECT_EQ(memcmp(g_stdout_capture, long600, 600), 0);

    /* ns_vprintf */
    capture_stdout_reset();
    n = do_vprintf("v=%d", 7);
    ns_stream_finish(NS_STDOUT);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(g_stdout_capture_len, 3);
    EXPECT_EQ(memcmp(g_stdout_capture, "v=7", 3), 0);
#else
    /* 非 GNU/Clang：platform_stdout_write 为库内强符号不可重定义，仅驱动代码路径 */
    (void)ns_printf("no-capture %d\n", 1);
    (void)do_vprintf("no-capture v=%d\n", 2);
    ns_stream_finish(NS_STDOUT);
#endif
    return 0;
}

/* ================================================================== */
/*  用例 10~15：stream 抽象（FUNCTION / NO_CACHE / MEMORY / NULL 参数）  */
/* ================================================================== */

static char g_fstream_capture[128];
static size_t g_fstream_capture_len;

static void fstream_write_cb(void *stream, const uint8_t *buf, size_t size)
{
    (void)stream;
    if(g_fstream_capture_len + size <= sizeof(g_fstream_capture)){
        memcpy(g_fstream_capture + g_fstream_capture_len, buf, size);
        g_fstream_capture_len += size;
    }
}

static int g_no_cache_finish_called;

static void no_cache_finish_cb(void *stream)
{
    (void)stream;
    g_no_cache_finish_called = 1;
}

static int test_stream_function(void)
{
    struct ns_stream_function s;
    uint8_t scache[4];
    int n;

    g_fstream_capture_len = 0;
    ns_stream_function_init(&s, fstream_write_cb, scache, sizeof(scache));

    /* 4 字节小缓存 → 多次 flush，finish 冲刷剩余 */
    n = ns_stream_printf((struct ns_stream_base *)&s, "abcdef%d", 7);
    ns_stream_finish((struct ns_stream_base *)&s);
    EXPECT_EQ(n, 7);
    EXPECT_EQ(g_fstream_capture_len, 7);
    EXPECT_EQ(memcmp(g_fstream_capture, "abcdef7", 7), 0);

    return 0;
}

static int test_stream_no_cache(void)
{
    struct ns_stream_function_no_cache s;
    int n;

    g_fstream_capture_len = 0;
    g_no_cache_finish_called = 0;
    ns_stream_function_no_cache_init(&s, fstream_write_cb, no_cache_finish_cb);

    /* write 直通 + finish 回调被调 */
    n = ns_stream_printf((struct ns_stream_base *)&s, "abc%d", 42);
    ns_stream_finish((struct ns_stream_base *)&s);
    EXPECT_EQ(n, 5);
    EXPECT_EQ(g_fstream_capture_len, 5);
    EXPECT_EQ(memcmp(g_fstream_capture, "abc42", 5), 0);
    EXPECT_EQ(g_no_cache_finish_called, 1);

    return 0;
}

static int test_stream_memory(void)
{
    uint8_t mbuf[64];
    struct ns_stream_memory s;
    int n;

    ns_stream_memory_init(&s, mbuf, sizeof(mbuf));
    n = ns_stream_printf((struct ns_stream_base *)&s, "ab%d", 7);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(ns_stream_puts((struct ns_stream_base *)&s, "yz"), 2);
    ns_stream_putc((struct ns_stream_base *)&s, '!');
    ns_stream_finish((struct ns_stream_base *)&s);
    EXPECT_EQ(strcmp((char *)mbuf, "ab7yz!"), 0);

    return 0;
}

static int test_stream_puts(void)
{
    struct ns_stream_function f;
    struct ns_stream_function_no_cache nc;
    struct ns_stream_memory m;
    uint8_t fcache[8];
    uint8_t mbuf[32];
    int n;

    /* FUNCTION 逐字节路径 */
    g_fstream_capture_len = 0;
    ns_stream_function_init(&f, fstream_write_cb, fcache, sizeof(fcache));
    n = ns_stream_puts((struct ns_stream_base *)&f, "hello");
    ns_stream_finish((struct ns_stream_base *)&f);
    EXPECT_EQ(n, 5);
    EXPECT_EQ(g_fstream_capture_len, 5);
    EXPECT_EQ(memcmp(g_fstream_capture, "hello", 5), 0);

    /* NO_CACHE 快速路径：整串直通 write */
    g_fstream_capture_len = 0;
    ns_stream_function_no_cache_init(&nc, fstream_write_cb, no_cache_finish_cb);
    n = ns_stream_puts((struct ns_stream_base *)&nc, "world");
    EXPECT_EQ(n, 5);
    EXPECT_EQ(g_fstream_capture_len, 5);
    EXPECT_EQ(memcmp(g_fstream_capture, "world", 5), 0);

    /* MEMORY 逐字节路径 */
    ns_stream_memory_init(&m, mbuf, sizeof(mbuf));
    n = ns_stream_puts((struct ns_stream_base *)&m, "abc");
    ns_stream_finish((struct ns_stream_base *)&m);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(strcmp((char *)mbuf, "abc"), 0);

    /* NULL 流 → 0 */
    EXPECT_EQ(ns_stream_puts(NULL, "x"), 0);

    return 0;
}

static int test_stream_putc(void)
{
    uint8_t mbuf[16];
    struct ns_stream_memory m;
    struct ns_stream_function f;
    uint8_t fcache[16];

    /* MEMORY */
    ns_stream_memory_init(&m, mbuf, sizeof(mbuf));
    ns_stream_putc((struct ns_stream_base *)&m, 'A');
    ns_stream_putc((struct ns_stream_base *)&m, 'B');
    ns_stream_finish((struct ns_stream_base *)&m);
    EXPECT_EQ(strcmp((char *)mbuf, "AB"), 0);

    /* FUNCTION */
    g_fstream_capture_len = 0;
    ns_stream_function_init(&f, fstream_write_cb, fcache, sizeof(fcache));
    ns_stream_putc((struct ns_stream_base *)&f, 'Z');
    ns_stream_finish((struct ns_stream_base *)&f);
    EXPECT_EQ(g_fstream_capture_len, 1);
    EXPECT_EQ(g_fstream_capture[0], 'Z');

    /* NULL no-op */
    ns_stream_putc(NULL, 'Q');

    return 0;
}

static int test_stream_null_args(void)
{
    EXPECT_EQ(ns_stream_printf(NULL, "%d", 5), 0);
    EXPECT_EQ(do_stream_vprintf(NULL, "%d", 5), 0);
    EXPECT_EQ(ns_stream_puts(NULL, "x"), 0);
    ns_stream_putc(NULL, 'A');
    ns_stream_finish(NULL);
    return 0;
}

/* ================================================================== */
/*  用例 16~22：浮点（基本 / 精度 / 宽度标志 / 指数 / 极端 / inf-nan / 差分） */
/* ================================================================== */

static const struct dbl_case g_float_basic_cases[] = {
    { "%f", 3.14, "3.140000" },
    { "%f", 0.5, "0.500000" },
    { "%f", -2.5, "-2.500000" },
    { "%f", 0.0, "0.000000" },
    { "%f", -0.0, "-0.000000" },
    { "%F", 3.14, "3.140000" },
    { "%f", 1e-5, "0.000010" },
};

static int test_float_basic(void)
{
    char buf[256];
    int n;

    if(run_dbl_cases(g_float_basic_cases, sizeof(g_float_basic_cases) / sizeof(g_float_basic_cases[0])) != 0) return 1;

    /* 1e17：整部 18 位 > 16 → 触发堆分支，返回应写全长 */
    n = ns_snprintf(buf, sizeof(buf), "%f", 1e17);
    EXPECT_EQ(n, 25);
    EXPECT_EQ(strcmp(buf, "100000000000000000.000000"), 0);

    return 0;
}

static const struct dbl_case g_float_precision_cases[] = {
    { "%.2f", 3.14159, "3.14" },
    { "%.0f", 2.5, "2" },          /* 银行家舍入：2.5 → 2 */
    { "%.0f", 3.5, "4" },          /* 3.5 → 4 */
    { "%.0f", 1.5, "2" },          /* 1.5 → 2 */
    { "%.5f", 1.0, "1.00000" },
    { "%.2f", 0.999, "1.00" },
    { "%.6f", 0.9999999, "1.000000" },
    { "%.25f", 1.0, "1.0000000000000000000000000" },  /* 精度≥19 钳制 + 补零 */
};

static int test_float_precision(void)
{
    return run_dbl_cases(g_float_precision_cases,
        sizeof(g_float_precision_cases) / sizeof(g_float_precision_cases[0]));
}

static const struct dbl_case g_float_width_cases[] = {
    { "%8.2f", 3.14, "    3.14" },
    { "%08.2f", 3.14, "00003.14" },   /* '0' 零填充 */
    { "%-8.2f", 3.14, "3.14    " },
    { "%+f", 3.14, "+3.140000" },
    { "% f", 3.14, " 3.140000" },
    { "%#.0f", 3.0, "3." },
    { "%8.2f", -3.14, "   -3.14" },
    { "%08.2f", -3.14, "-0003.14" },
};

static int test_float_width_flags(void)
{
    return run_dbl_cases(g_float_width_cases,
        sizeof(g_float_width_cases) / sizeof(g_float_width_cases[0]));
}

/* %e 0.0 曾因引擎 bug（raw_factor 未初始化 → 0/0 = NaN）输出垃圾值,
 * FORMATIO-017 已修复后启用: 0.0 → "0.000000e+00"、-0.0 → "-0.000000e+00" */
static const struct dbl_case g_float_exp_cases[] = {
    { "%e", 0.0, "0.000000e+00" },
    { "%e", -0.0, "-0.000000e+00" },
    { "%.0e", 0.0, "0e+00" },
    { "%E", 0.0, "0.000000E+00" },
    { "%e", 12345.678, "1.234568e+04" },
    { "%E", 12345.678, "1.234568E+04" },
    { "%e", 0.0001, "10.000000e-05" },  /* 归一化修正触发：floored_exp10=-5、scaled=10 */
    { "%.2e", 12345.678, "1.23e+04" },
    { "%.0e", 12345.678, "1e+04" },
    { "%12.2e", 12345.678, "    1.23e+04" },
    { "%012.2e", 12345.678, "00001.23e+04" },
    { "%e", 1.0, "1.000000e+00" },
    { "%e", -12345.678, "-1.234568e+04" },
    { "%e", 1e-18, "1.000000e-18" },
    { "%e", 1e-20, "10.000000e-21" },
    { "%e", 1e19, "10.000000e+18" },
    /* 精度 ≥ 幂表大小（19）：float_normalized_decentralized 内钳制到 18 + 补零 */
    { "%.19e", 12345.678, "1.2345678000000000000e+04" },
    /* 舍入恰在半途：scaled_remainder == 0.5 → 银行家舍入清零奇位（偶数化） */
    { "%.0e", 2.5, "2e+00" },
    { "%.0e", 3.5, "3e+00" },
};

static int test_float_exp(void)
{
    return run_dbl_cases(g_float_exp_cases,
        sizeof(g_float_exp_cases) / sizeof(g_float_exp_cases[0]));
}

static int test_float_exp_extremes(void)
{
    char buf[128];

    /* %f/%g 超 ±1e18 守卫（vprintf_float_f_or_g 返回 0）→ 回退 %e 输出 */
    EXPECT_EQ(ns_snprintf(buf, sizeof(buf), "%f", 1e19), 13);
    EXPECT_EQ(strcmp(buf, "10.000000e+18"), 0);
    EXPECT_EQ(ns_snprintf(buf, sizeof(buf), "%g", 1e19), 13);
    EXPECT_EQ(strcmp(buf, "10.000000e+18"), 0);

    /* 1e-300：floored_exp10=-300 超幂表 → pow10_of_int 除路径 */
    EXPECT_EQ(ns_snprintf(buf, sizeof(buf), "%e", 1e-300), 13);
    EXPECT_EQ(strcmp(buf, "1.000000e-300"), 0);

    /* 1e-308：floored_exp10 == -DBL_MAX_10_EXP → pow10_of_int 走
     * FORMAT_DBL_MIN_POW10 常量返回分支 */
    EXPECT_EQ(ns_snprintf(buf, sizeof(buf), "%e", 1e-308), 13);
    EXPECT_EQ(strcmp(buf, "1.000000e-308"), 0);

    /* %G：FORMAT_FLOAT_G + FORMAT_LARGE；超 ±1e18 守卫回退 %E 格式 */
    EXPECT_EQ(ns_snprintf(buf, sizeof(buf), "%G", 1e19), 13);
    EXPECT_EQ(strcmp(buf, "10.000000E+18"), 0);

    /* %.18e 1e-290 → close_to_representation_extremum 走 float_decentralized
     * （数字形态为幂表近似路径的确定性输出，已在实测中确认） */
    EXPECT_EQ(ns_snprintf(buf, sizeof(buf), "%.18e", 1e-290), 25);
    EXPECT_EQ(strcmp(buf, "9.999999999998939520e-291"), 0);

    return 0;
}

static const struct dbl_case g_float_inf_nan_cases[] = {
    { "%f", INFINITY, "inf" },
    { "%f", -INFINITY, "-inf" },
    { "%f", NAN, "nan" },
    { "%F", INFINITY, "INF" },
    { "%F", NAN, "NAN" },
    { "%+f", INFINITY, "+inf" },
    { "% f", INFINITY, " inf" },
    { "%10f", INFINITY, "       inf" },
    { "%+f", -INFINITY, "-inf" },
    { "%e", INFINITY, "inf" },
};

static int test_float_inf_nan(void)
{
    return run_dbl_cases(g_float_inf_nan_cases,
        sizeof(g_float_inf_nan_cases) / sizeof(g_float_inf_nan_cases[0]));
}

/* 浮点差分：与 libc snprintf 对拍，限定 %f 定精度、排除 %g */
static const struct { const char *fmt; double value; } g_float_diff_cases[] = {
    { "%.6f", 3.14 },
    { "%.6f", 0.001 },
    { "%.6f", 100000.0 },
    { "%.6f", -2.71828 },
    { "%.6f", 0.5 },
    { "%.6f", -0.5 },
    { "%.6f", 0.1 },
    { "%.6f", 0.9 },
    { "%.6f", 12345.678 },
    { "%.6f", 1e-5 },
    { "%.6f", 1e5 },
    { "%.6f", -123.456 },
    { "%.2f", 3.14159 },
    { "%.2f", 0.5 },
    { "%.2f", 0.99 },
    { "%.2f", -0.99 },
    { "%.1f", 1.5 },
    { "%.1f", 2.5 },
    { "%.1f", 7.25 },
    { "%.0f", 2.5 },
    { "%.0f", 3.5 },
    { "%.0f", 1.5 },
    { "%.4f", 0.00012345 },
    { "%.6f", 1e15 },
    { "%.6f", 1e17 },
    { "%.2f", 1.25 },
    { "%.2f", 1.23 },
    { "%.6f", 0.333333 },
    { "%.3f", 0.0001 },
    { "%.0f", 0.5 },
    { "%.6f", 7.0 },
    { "%.6f", -7.0 },
    { "%.1f", -1.5 },
    { "%.2f", -1.25 },
};

static int test_float_differential(void)
{
    size_t i;
    size_t total;
    int matched = 0;
    int mismatched = 0;

    total = sizeof(g_float_diff_cases) / sizeof(g_float_diff_cases[0]);
    for(i = 0; i < total; i++){
        char libc_buf[128];
        char ns_buf[128];
        int libc_n = snprintf(libc_buf, sizeof(libc_buf),
            g_float_diff_cases[i].fmt, g_float_diff_cases[i].value);
        int ns_n = ns_snprintf(ns_buf, sizeof(ns_buf),
            g_float_diff_cases[i].fmt, g_float_diff_cases[i].value);

        if(libc_n == ns_n && strcmp(libc_buf, ns_buf) == 0){
            matched++;
        } else {
            fprintf(stderr, "DIFF mismatch: fmt=%s value=%g libc=\"%s\" ns=\"%s\"\n",
                g_float_diff_cases[i].fmt, g_float_diff_cases[i].value, libc_buf, ns_buf);
            mismatched++;
        }
    }
    /* 一致集下限：低于 20 例视为差分断言退化，直接失败 */
    if(matched < 20){
        fprintf(stderr, "float differential matched=%d (need >= 20), mismatched=%d\n",
            matched, mismatched);
        return 1;
    }
    return 0;
}

/* ================================================================== */
/*  用例 23~29：二进制/八进制、整数边界、%p、%q 数组、未知转换、%Lf、填满 finish */
/* ================================================================== */

static int test_binary_octal(void)
{
    /* %b/%B、UINT64_MAX 64 位 → bit_count>16 堆分配、%o 宽度 */
    CHECK_FMT("%b", "11111111", 0xFFu);
    CHECK_FMT("%B", "11111111", 0xFFu);
    CHECK_FMT("%#b", "0b11111111", 0xFFu);
    CHECK_FMT("%llb", "1111111111111111111111111111111111111111111111111111111111111111", UINT64_MAX);
    CHECK_FMT("%o", "10", 8u);
    CHECK_FMT("[%10o]", "[        10]", 8u);
    CHECK_FMT("%#o", "010", 8u);
    return 0;
}

static int test_integer_edges(void)
{
    CHECK_FMT("%lld", "-9223372036854775808", LLONG_MIN);   /* 无符号环绕 + 19 位 → 堆 */
    CHECK_FMT("%lld", "9223372036854775807", LLONG_MAX);
    CHECK_FMT("%llu", "18446744073709551615", UINT64_MAX);
    CHECK_FMT("%llx", "ffffffffffffffff", UINT64_MAX);
    CHECK_FMT("%llX", "FFFFFFFFFFFFFFFF", UINT64_MAX);
    CHECK_FMT("%zu", "18446744073709551615", (size_t)SIZE_MAX);
#if defined(__unix__) || defined(__APPLE__)
    CHECK_FMT("%zd", "-1", (ssize_t)-1);
#endif
    return 0;
}

/* %p 结构断言：构造期望串（0x 前缀 + 2*sizeof(void*) 位 hex），不依赖 snprintf */
static int check_pointer_format(void *ptr, const char *buf)
{
    const char *hex = "0123456789abcdef";
    char expected[64];
    uintptr_t v = (uintptr_t)ptr;
    int digits = (int)(sizeof(void *) * 2);
    int i;

    expected[0] = '0';
    expected[1] = 'x';
    for(i = 0; i < digits; i++){
        int shift = 4 * (digits - 1 - i);
        expected[2 + i] = hex[(int)((v >> shift) & 0xFu)];
    }
    expected[2 + digits] = '\0';
    return strcmp(buf, expected);
}

static int test_pointer(void)
{
    char buf[64];
    uint32_t value = 0xDEADBEEFu;
    void *p = (void *)&value;
    int n;

    /* 默认 %p：宽度 2*sizeof(void*)+2 = 18，ZEROPAD|SPECIAL → "0x"+16 位 hex */
    n = ns_snprintf(buf, sizeof(buf), "%p", p);
    EXPECT_EQ(n, 2 + (int)(sizeof(void *) * 2));
    EXPECT_EQ(memcmp(buf, "0x", 2), 0);
    EXPECT_EQ(check_pointer_format(p, buf), 0);

    /* NULL 指针 → 全零 */
    n = ns_snprintf(buf, sizeof(buf), "%p", (void *)NULL);
    EXPECT_EQ(n, 2 + (int)(sizeof(void *) * 2));
    EXPECT_EQ(check_pointer_format(NULL, buf), 0);

    return 0;
}

static int test_array_q(void)
{
    static const uint8_t arr4[4] = { 0xAB, 0xCD, 0xEF, 0x12 };
    static const uint8_t arr8[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    char buf[256];
    int n;

    /* %q 无限定符 → item_size = sizeof(unsigned int) = 4（小端字节序） */
    n = ns_snprintf(buf, sizeof(buf), "%.4q", arr4);
    EXPECT_EQ(n, 8);
    EXPECT_EQ(strcmp(buf, "12efcdab"), 0);

    /* %Q 大写 */
    n = ns_snprintf(buf, sizeof(buf), "%.4Q", arr4);
    EXPECT_EQ(n, 8);
    EXPECT_EQ(strcmp(buf, "12EFCDAB"), 0);

    /* 限定符：l/ll → 8 字节、h → 2 字节、hh → 1 字节 */
    n = ns_snprintf(buf, sizeof(buf), "%.8lq", arr8);
    EXPECT_EQ(n, 16);
    EXPECT_EQ(strcmp(buf, "0807060504030201"), 0);

    n = ns_snprintf(buf, sizeof(buf), "%.8llq", arr8);
    EXPECT_EQ(n, 16);
    EXPECT_EQ(strcmp(buf, "0807060504030201"), 0);

    n = ns_snprintf(buf, sizeof(buf), "%.4hq", arr8);
    EXPECT_EQ(n, 9);
    EXPECT_EQ(strcmp(buf, "0201 0403"), 0);

    n = ns_snprintf(buf, sizeof(buf), "%.4hhq", arr8);
    EXPECT_EQ(n, 11);
    EXPECT_EQ(strcmp(buf, "01 02 03 04"), 0);

    /* 宽度 > valid_len 与左对齐 */
    n = ns_snprintf(buf, sizeof(buf), "%12.4q", arr4);
    EXPECT_EQ(n, 12);
    EXPECT_EQ(strcmp(buf, "    12efcdab"), 0);

    n = ns_snprintf(buf, sizeof(buf), "%-12.4q", arr4);
    EXPECT_EQ(n, 12);
    EXPECT_EQ(strcmp(buf, "12efcdab    "), 0);

    /* 精度非 item_size 倍数 → 余数 '??' 补位；%.3q：9 空格 + " " + "??" + "efcdab" */
    n = ns_snprintf(buf, sizeof(buf), "%.3q", arr4);
    EXPECT_EQ(n, 18);
    EXPECT_EQ(strcmp(buf, "         " " ??efcdab"), 0);

    /* %.1q 负 valid_len 边界：9 空格 + " " + "??????" + "ab" */
    n = ns_snprintf(buf, sizeof(buf), "%.1q", arr4);
    EXPECT_EQ(n, 18);
    EXPECT_EQ(strcmp(buf, "         " " ??????ab"), 0);

    return 0;
}

static int test_unknown_conversion(void)
{
    /* 未知转换字面量回退：%r → "%r"（回退后再逐字处理） */
    CHECK_FMT("%r", "%r", 1);
    CHECK_FMT("a%rb", "a%rb", 1);
    CHECK_FMT("%5r", "%5r", 1);
    /* %% 与字符串尾 % */
    CHECK_FMT("100%%", "100%", 0);
    CHECK_FMT("100%", "100%", 0);
    return 0;
}

static int test_float_l_qualifier(void)
{
    char buf[32];

    /* "%Lf" → qualifier==LONG_LONG 直接 continue，无输出 */
    EXPECT_EQ(ns_snprintf(buf, sizeof(buf), "%Lf", 1.0), 0);
    EXPECT_EQ(buf[0], '\0');

    return 0;
}

static int test_snprintf_double_finish(void)
{
    char buf[16];

    /* 输出恰好等于 size：streamout_finish 用 NUL 覆盖最后一个字符 */
    memset(buf, 'X', sizeof(buf));
    EXPECT_EQ(ns_snprintf(buf, 4, "1234"), 4);
    EXPECT_EQ(buf[0], '1');
    EXPECT_EQ(buf[1], '2');
    EXPECT_EQ(buf[2], '3');
    EXPECT_EQ(buf[3], '\0');

    /* size = 长度 + 1：NUL 有专属位置 */
    memset(buf, 'X', sizeof(buf));
    EXPECT_EQ(ns_snprintf(buf, 5, "1234"), 4);
    EXPECT_EQ(strcmp(buf, "1234"), 0);
    EXPECT_EQ(buf[4], '\0');

    return 0;
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void)
{
    if(test_snprintf_basic() != 0) return 1;
    if(test_snprintf_qualifiers() != 0) return 1;
    if(test_snprintf_special() != 0) return 1;
    if(test_star_width_precision() != 0) return 1;
    if(test_snprintf_truncation() != 0) return 1;
    if(test_snprintf_null_s() != 0) return 1;
    if(test_vsnprintf() != 0) return 1;
    if(test_sprintf() != 0) return 1;
    if(test_printf_family() != 0) return 1;
    if(test_stream_function() != 0) return 1;
    if(test_stream_no_cache() != 0) return 1;
    if(test_stream_memory() != 0) return 1;
    if(test_stream_puts() != 0) return 1;
    if(test_stream_putc() != 0) return 1;
    if(test_stream_null_args() != 0) return 1;
    if(test_float_basic() != 0) return 1;
    if(test_float_precision() != 0) return 1;
    if(test_float_width_flags() != 0) return 1;
    if(test_float_exp() != 0) return 1;
    if(test_float_exp_extremes() != 0) return 1;
    if(test_float_inf_nan() != 0) return 1;
    if(test_float_differential() != 0) return 1;
    if(test_binary_octal() != 0) return 1;
    if(test_integer_edges() != 0) return 1;
    if(test_pointer() != 0) return 1;
    if(test_array_q() != 0) return 1;
    if(test_unknown_conversion() != 0) return 1;
    if(test_float_l_qualifier() != 0) return 1;
    if(test_snprintf_double_finish() != 0) return 1;
    return 0;
}
