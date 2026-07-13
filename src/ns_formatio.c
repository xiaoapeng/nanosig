/**
 * @file ns_formatio.c
 * @brief nanosig 格式化输出实现 — 移植自 eventhub_os eh_formatio.c。
 * @date 2026-07-11
 *
 * 轻量级 printf/snprintf/vprintf 实现，自带浮点数、十六进制数组格式化。
 * 提供可插拔 stream 抽象层（function/memory/no-cache）。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
// #include <stdio.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_port.h>
#include <nanosig/ns_formatio.h>

/* ================================================================== */
/*  内部常量                                                            */
/* ================================================================== */

#define FORMAT_FLOAT_F_RANGE_MAX        (1.e+18)
#define FORMAT_FLOAT_F_RANGE_MIN        (-(1.e+18))
#define FORMAT_FLOAT_POWERS_TAB_SIZE    19
#define FORMAT_STACK_CACHE_SIZE         (16)
#define FORMAT_LOG10_TAYLOR_TERMS       (4)
#define FORMAT_DBL_EXP_OFFSET           (1023)
#define FORMAT_DBL_MIN_POW10            (1.e-308)

#define FORMAT_LEFT         0x00000001u
#define FORMAT_PLUS         0x00000002u
#define FORMAT_SPACE        0x00000004u
#define FORMAT_SPECIAL      0x00000008u
#define FORMAT_ZEROPAD      0x00000010u
#define FORMAT_LARGE        0x00000020u
#define FORMAT_SIGNED       0x00000040u
#define FORMAT_FLOAT_E      0x00000080u
#define FORMAT_FLOAT_F      0x00000100u
#define FORMAT_FLOAT_G      0x00000200u

#ifndef NS_CONFIG_STDOUT_MEM_CACHE_SIZE
#define NS_CONFIG_STDOUT_MEM_CACHE_SIZE 512
#endif

/* ================================================================== */
/*  内部类型                                                            */
/* ================================================================== */

enum ns_format_qualifier {
    NS_FORMAT_QUALIFIER_NONE,
    NS_FORMAT_QUALIFIER_LONG,
    NS_FORMAT_QUALIFIER_LONG_LONG,
    NS_FORMAT_QUALIFIER_SHORT,
    NS_FORMAT_QUALIFIER_CHAR,
    NS_FORMAT_QUALIFIER_SIZE_T,
};

enum ns_base_type {
    NS_BASE_TYPE_BIN = 2,
    NS_BASE_TYPE_OCT = 8,
    NS_BASE_TYPE_DEC = 10,
    NS_BASE_TYPE_HEX = 16,
};

/* ================================================================== */
/*  静态数据                                                            */
/* ================================================================== */

static const char small_digits[] = "0123456789abcdef";
static const char large_digits[] = "0123456789ABCDEF";
static const double powers_of_10[FORMAT_FLOAT_POWERS_TAB_SIZE] = {
  1e00, 1e01, 1e02, 1e03, 1e04, 1e05, 1e06, 1e07, 1e08,
  1e09, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18
};
static uint8_t _stdout_cache[NS_CONFIG_STDOUT_MEM_CACHE_SIZE];

NS_FUNCTION_WEAK void stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    (void)stream;
    (void)buf;
    (void)size;
}

struct ns_stream_function _ns_stdout = {
    .base = {
        .type = NS_STREAM_TYPE_FUNCTION,
    },
    .write = stdout_write,
    .cache = _stdout_cache,
    .pos = _stdout_cache,
    .end = _stdout_cache + NS_CONFIG_STDOUT_MEM_CACHE_SIZE,
};

struct ns_double_components {
    uint64_t integral;
    uint64_t fractional;
    bool     is_negative;
};

union ns_double_union {
    double d;
    struct {
        uint64_t mantissa : 52;
        uint64_t exponent : 11;
        uint64_t sign     : 1;
    };
    struct {
        uint64_t v64;
    };
};

NS_STATIC_ASSERT(sizeof(union ns_double_union) == sizeof(double),
    "ns_double_union must match double size");

/* NOTE: Bitfield layout (mantissa:52, exponent:11, sign:1) assumes MSB-first
 * allocation order matching IEEE 754. This is correct on x86, ARM, and all
 * mainstream little-endian targets. Not portable to platforms with different
 * bitfield allocation order (C11 J.3.9). */

struct ns_scaling_factor {
    double raw_factor;
    bool   multiply;
};

/* ================================================================== */
/*  内部辅助函数                                                        */
/* ================================================================== */

static int bastardized_floor(double x)
{
    int n;
    if (x >= 0) { return (int)x; }
    n = (int)x;
    return (((double)n) == x) ? n : n - 1;
}

static double pow10_of_int(int floored_exp10)
{
    union ns_double_union du = { .v64 = 0 };
    int exp2;

    if (floored_exp10 == -DBL_MAX_10_EXP) {
        return FORMAT_DBL_MIN_POW10;
    }
    exp2 = bastardized_floor(floored_exp10 * 3.321928094887362 + 0.5);
    const double z  = floored_exp10 * 2.302585092994046 - exp2 * 0.6931471805599453;
    const double z2 = z * z;
    du.exponent = (uint16_t)(((exp2) + FORMAT_DBL_EXP_OFFSET) & 0x7FF);
    du.d *= 1 + 2 * z / (2 - z + (z2 / (6 + (z2 / (10 + z2 / 14)))));
    return du.d;
}

static double log10_of_positive(double positive_number)
{
    union ns_double_union du = { .d = positive_number };
    int exp2 = du.exponent - FORMAT_DBL_EXP_OFFSET;
    double z;

    du.exponent = FORMAT_DBL_EXP_OFFSET;
    z = du.d - 1.5;
    return (
        0.1760912590556812420
        + z     * 0.2895296546021678851
#if FORMAT_LOG10_TAYLOR_TERMS > 2
        - z*z   * 0.0965098848673892950
#if FORMAT_LOG10_TAYLOR_TERMS > 3
        + z*z*z * 0.0428932821632841311
#endif
#endif
        + exp2 * 0.30102999566398119521
    );
}

static int num_rsh(unsigned long long *number, int base)
{
    int res;
    res = (int)((*number) % (unsigned long long)base);
    *number = (*number) / (unsigned long long)base;
    return res;
}

static int num_bit_count(unsigned long long number, int base)
{
    int res = 0;
    do {
        number = number / (unsigned long long)base;
        res++;
    } while (number);
    return res;
}

/* ================================================================== */
/*  Stream 输出原语                                                      */
/* ================================================================== */

static inline void streamout_in_byte(struct ns_stream_base *stream, char ch)
{
    bool out = false;

    switch (stream->type) {
        case NS_STREAM_TYPE_FUNCTION: {
            struct ns_stream_function *f = (struct ns_stream_function *)stream;
            if (f->pos < f->end) {
                *f->pos = (uint8_t)ch;
                f->pos++;
                if (ch == '\n') out = true;
            }
            if (f->pos == f->end || out) {
                f->write(stream, f->cache, (size_t)(f->pos - f->cache));
                f->pos = f->cache;
            }
            break;
        }
        case NS_STREAM_TYPE_FUNCTION_NO_CACHE: {
            struct ns_stream_function_no_cache *f =
                (struct ns_stream_function_no_cache *)stream;
            f->write(stream, (uint8_t *)&ch, 1);
            break;
        }
        case NS_STREAM_TYPE_MEMORY: {
            struct ns_stream_memory *m = (struct ns_stream_memory *)stream;
            if (m->pos < m->end) {
                *m->pos = (uint8_t)ch;
                m->pos++;
            }
            break;
        }
    }
}

static NS_NOINLINE void streamout_finish(struct ns_stream_base *stream)
{
    switch (stream->type) {
        case NS_STREAM_TYPE_FUNCTION: {
            struct ns_stream_function *f = (struct ns_stream_function *)stream;
            if (f->pos > f->cache) {
                f->write(stream, f->cache, (size_t)(f->pos - f->cache));
                f->pos = f->cache;
            }
            break;
        }
        case NS_STREAM_TYPE_FUNCTION_NO_CACHE: {
            struct ns_stream_function_no_cache *f =
                (struct ns_stream_function_no_cache *)stream;
            f->finish(stream);
            break;
        }
        case NS_STREAM_TYPE_MEMORY: {
            struct ns_stream_memory *m = (struct ns_stream_memory *)stream;
            if (m->pos < m->end) {
                *m->pos = '\0';
            } else {
                *(m->end - 1) = '\0';
            }
            break;
        }
    }
}

static inline int skip_atoi(const char **s)
{
    int i = 0;
    while (isdigit((int)(**s)))
        i = i * 10 + (*((*s)++) - '0');
    return i;
}

/* ================================================================== */
/*  格式化原语                                                          */
/* ================================================================== */

static inline int vprintf_char(struct ns_stream_base *stream, char ch,
    int field_width, unsigned int flags)
{
    int n = 0;
    n = field_width <= 1 ? 1 : field_width;
    if (flags & FORMAT_LEFT)
        streamout_in_byte(stream, ch);
    while (field_width-- > 1)
        streamout_in_byte(stream, ' ');
    if (!(flags & FORMAT_LEFT))
        streamout_in_byte(stream, ch);
    return n;
}

static inline int vprintf_string(struct ns_stream_base *stream, char *s,
    int field_width, int precision, unsigned int flags)
{
    int n = 0;
    int len;
    int diff;

    if (s == NULL)
        s = "(null)";
    if (field_width < 0) {
        for (len = 0; (len != precision) && (s[len]); len++, n++)
            streamout_in_byte(stream, s[len]);
        return n;
    }
    for (len = 0; (len != precision) && (s[len]); len++) { }

    if (field_width <= len) {
        for (int i = 0; i < len; i++, n++)
            streamout_in_byte(stream, s[i]);
        return n;
    }
    diff = field_width - len;
    if (!(flags & FORMAT_LEFT)) {
        for (int i = 0; i < diff; i++, n++)
            streamout_in_byte(stream, ' ');
    }
    for (int i = 0; i < len; i++, n++)
        streamout_in_byte(stream, s[i]);

    if (flags & FORMAT_LEFT) {
        for (int i = 0; i < diff; i++, n++)
            streamout_in_byte(stream, ' ');
    }
    return n;
}

/* ================================================================== */
/*  浮点数辅助                                                          */
/* ================================================================== */

static double apply_scaling(double num, struct ns_scaling_factor normalization)
{
    return normalization.multiply
        ? num * normalization.raw_factor
        : num / normalization.raw_factor;
}

static double unapply_scaling(double normalized, struct ns_scaling_factor normalization)
{
    return normalization.multiply
        ? normalized / normalization.raw_factor
        : normalized * normalization.raw_factor;
}

static struct ns_scaling_factor update_normalization(
    struct ns_scaling_factor sf, double extra_multiplicative_factor)
{
    struct ns_scaling_factor result;

    if (sf.multiply) {
        result.multiply = true;
        result.raw_factor = sf.raw_factor * extra_multiplicative_factor;
    } else {
        union ns_double_union du = { .d = sf.raw_factor };
        int factor_exp2 = du.exponent - FORMAT_DBL_EXP_OFFSET;
        du.d = extra_multiplicative_factor;
        int extra_factor_exp2 = du.exponent - FORMAT_DBL_EXP_OFFSET;

        if (abs(factor_exp2) > abs(extra_factor_exp2)) {
            result.multiply = false;
            result.raw_factor = sf.raw_factor / extra_multiplicative_factor;
        } else {
            result.multiply = true;
            result.raw_factor = extra_multiplicative_factor / sf.raw_factor;
        }
    }
    return result;
}

static void float_decentralized(double num, struct ns_double_components *components,
    int precision)
{
    union ns_double_union du = { .d = num };
    double abs_number;
    double remainder;

    components->is_negative = du.sign;
    abs_number = (components->is_negative) ? -num : num;
    components->integral = (uint64_t)abs_number;

    if (precision >= FORMAT_FLOAT_POWERS_TAB_SIZE)
        precision = FORMAT_FLOAT_POWERS_TAB_SIZE - 1;
    remainder = (abs_number - (double)components->integral) * powers_of_10[precision];
    components->fractional = (uint64_t)remainder;

    remainder -= (double)components->fractional;

    /* 银行家舍入法：四舍六入五取偶 */
    if (remainder > 0.5) {
        ++components->fractional;
        if ((double)components->fractional >= powers_of_10[precision]) {
            components->fractional = 0;
            ++components->integral;
        }
    } else if ((remainder == 0.5) &&
               ((components->fractional == 0U) || (components->fractional & 1U))) {
        ++components->fractional;
    }

    if (precision == 0U) {
        remainder = abs_number - (double)components->integral;
        if ((!(remainder < 0.5) || (remainder > 0.5)) && (components->integral & 1)) {
            ++components->integral;
        }
    }
}

static void float_normalized_decentralized(
    struct ns_double_components *components,
    bool negative, int precision, double non_normalized,
    struct ns_scaling_factor normalization, int floored_exp10)
{
    double scaled = apply_scaling(non_normalized, normalization);
    bool close_to_representation_extremum =
        (-floored_exp10 + (int)precision) >= DBL_MAX_10_EXP - 1;
    double remainder;
    double prec_power_of_10;
    struct ns_scaling_factor account_for_precision;
    double scaled_remainder;
    double rounding_threshold;

    components->is_negative = negative;

    if (precision >= FORMAT_FLOAT_POWERS_TAB_SIZE)
        precision = FORMAT_FLOAT_POWERS_TAB_SIZE - 1;

    if (close_to_representation_extremum) {
        float_decentralized(negative ? -scaled : scaled, components, precision);
        return;
    }
    components->integral = (uint64_t)scaled;
    remainder = non_normalized - unapply_scaling((double)components->integral, normalization);
    prec_power_of_10 = powers_of_10[precision];
    account_for_precision = update_normalization(normalization, prec_power_of_10);
    scaled_remainder = apply_scaling(remainder, account_for_precision);
    rounding_threshold = 0.5;

    components->fractional = (uint64_t)scaled_remainder;
    scaled_remainder -= (double)components->fractional;

    components->fractional += (scaled_remainder >= rounding_threshold);
    if (scaled_remainder == rounding_threshold) {
        components->fractional &= ~((uint64_t)0x1);
    }
    if ((double)components->fractional >= prec_power_of_10) {
        components->fractional = 0;
        ++components->integral;
    }
}

/* ================================================================== */
/*  浮点数格式化输出                                                     */
/* ================================================================== */

static int vprintf_float_decimalism_or_normalized(struct ns_stream_base *stream,
    struct ns_double_components *components_num,
    int field_width, int precision, unsigned int flags, int floored_exp10)
{
    char _number_buf[FORMAT_STACK_CACHE_SIZE];
    char *number_buf = _number_buf;
    char sign = 0;
    char dot = 0;
    char exponent_sign_e = 0;
    char exponent_sign = 0;
    int n = 0;
    int need_buf_min_size = 0;
    int integral_valid_len = 0;
    int fractional_pad_len = 0;
    int fractional_valid_len = 0;
    int fractional_precision_pad_len = 0;
    int exponent_valid_len = 0;
    int valid_len = 0;
    int space_or_zero_pad_len = 0;
    unsigned long long number = 0;
    const char *digits = small_digits;

    if (precision >= FORMAT_FLOAT_POWERS_TAB_SIZE) {
        fractional_precision_pad_len = precision - (FORMAT_FLOAT_POWERS_TAB_SIZE - 1);
        precision = FORMAT_FLOAT_POWERS_TAB_SIZE - 1;
    }

    if (components_num->is_negative) {
        sign = '-';
    } else if (flags & FORMAT_PLUS) {
        sign = '+';
    } else if (flags & FORMAT_SPACE) {
        sign = ' ';
    }
    if (precision > 0 || flags & FORMAT_SPECIAL) {
        dot = '.';
    }

    if (flags & FORMAT_FLOAT_E) {
        exponent_sign_e = flags & FORMAT_LARGE ? 'E' : 'e';
        exponent_sign = floored_exp10 >= 0 ? '+' : '-';
        floored_exp10 = abs(floored_exp10);
        exponent_valid_len = num_bit_count((unsigned long long)floored_exp10, NS_BASE_TYPE_DEC);
    }

    integral_valid_len = num_bit_count(components_num->integral, NS_BASE_TYPE_DEC);
    if (precision > 0) {
        fractional_valid_len = num_bit_count(components_num->fractional, NS_BASE_TYPE_DEC);
        if (precision > fractional_valid_len)
            fractional_pad_len = precision - fractional_valid_len;
    }

    need_buf_min_size = integral_valid_len > fractional_valid_len
        ? integral_valid_len : fractional_valid_len;
    need_buf_min_size = need_buf_min_size > exponent_valid_len
        ? need_buf_min_size : exponent_valid_len;

    if (need_buf_min_size > FORMAT_STACK_CACHE_SIZE) {
        number_buf = (char *)ns_platform_alloc((size_t)need_buf_min_size);
        if (number_buf == NULL) return 0;
    }

    if (flags & FORMAT_FLOAT_E) {
        valid_len = (sign ? 1 : 0) + integral_valid_len +
            (dot ? 1 : 0) + fractional_pad_len + fractional_valid_len +
            fractional_precision_pad_len +
            (exponent_sign_e ? 1 : 0) + (exponent_sign ? 1 : 0) +
            ((exponent_valid_len < 2) ? 2 : exponent_valid_len);
    } else {
        valid_len = (sign ? 1 : 0) + integral_valid_len +
            (dot ? 1 : 0) + fractional_pad_len + fractional_valid_len +
            fractional_precision_pad_len;
    }

    if (field_width > valid_len)
        space_or_zero_pad_len = field_width - valid_len;

    /* 右对齐填充 */
    if (!(flags & FORMAT_LEFT) && !(flags & FORMAT_ZEROPAD)) {
        for (int i = 0; i < space_or_zero_pad_len; i++, n++)
            streamout_in_byte(stream, ' ');
    }

    /* 符号位 */
    if (sign) {
        streamout_in_byte(stream, sign);
        n++;
    }

    /* 0 填充 */
    if (flags & FORMAT_ZEROPAD) {
        for (int i = 0; i < space_or_zero_pad_len; i++, n++)
            streamout_in_byte(stream, '0');
    }

    /* 整数部分 */
    number = (unsigned long long)components_num->integral;
    for (int i = (integral_valid_len - 1); i >= 0; i--)
        number_buf[i] = digits[num_rsh(&number, NS_BASE_TYPE_DEC)];

    for (int i = 0; i < integral_valid_len; i++, n++)
        streamout_in_byte(stream, number_buf[i]);

    /* 小数点 */
    if (dot) {
        streamout_in_byte(stream, dot);
        n++;
    }

    /* 小数部分 0 填充 */
    for (int i = 0; i < fractional_pad_len; i++, n++)
        streamout_in_byte(stream, '0');

    /* 小数部分 */
    number = (unsigned long long)components_num->fractional;
    for (int i = (fractional_valid_len - 1); i >= 0; i--)
        number_buf[i] = digits[num_rsh(&number, NS_BASE_TYPE_DEC)];

    for (int i = 0; i < fractional_valid_len; i++, n++)
        streamout_in_byte(stream, number_buf[i]);

    /* 精度不足部分补 0 */
    for (int i = 0; i < fractional_precision_pad_len; i++, n++)
        streamout_in_byte(stream, '0');

    /* 指数部分 */
    if (flags & FORMAT_FLOAT_E) {
        streamout_in_byte(stream, exponent_sign_e);
        streamout_in_byte(stream, exponent_sign);
        n += 2;

        number = (unsigned long long)floored_exp10;
        for (int i = (exponent_valid_len - 1); i >= 0; i--)
            number_buf[i] = digits[num_rsh(&number, NS_BASE_TYPE_DEC)];

        if (exponent_valid_len < 2) {
            streamout_in_byte(stream, '0');
            n++;
        }
        for (int i = 0; i < exponent_valid_len; i++, n++)
            streamout_in_byte(stream, number_buf[i]);
    }

    /* 左对齐填充 */
    if (flags & FORMAT_LEFT && !(flags & FORMAT_ZEROPAD)) {
        for (int i = 0; i < space_or_zero_pad_len; i++, n++)
            streamout_in_byte(stream, ' ');
    }
    if (number_buf != _number_buf)
        ns_platform_free(number_buf);

    return n;
}

static int vprintf_float_e(struct ns_stream_base *stream, double num,
    int field_width, int precision, unsigned int flags)
{
    union ns_double_union du = { .d = num };
    double abs_number = du.sign ? -num : num;
    int floored_exp10;
    bool abs_exp10_covered_by_powers_table = false;
    struct ns_double_components components;
    struct ns_scaling_factor normalization = { 0 };

    if (precision < 0)
        precision = 6;

    if (abs_number == 0.0) {
        floored_exp10 = 0;
    } else {
        double exp10 = log10_of_positive(abs_number);
        floored_exp10 = bastardized_floor(exp10);
        double p10 = pow10_of_int(floored_exp10);
        if (abs_number < p10) {
            floored_exp10--;
            p10 /= 10;
        }
        abs_exp10_covered_by_powers_table = abs(floored_exp10) < FORMAT_FLOAT_POWERS_TAB_SIZE;
        normalization.raw_factor = abs_exp10_covered_by_powers_table
            ? powers_of_10[abs(floored_exp10)] : p10;
    }
    normalization.multiply = (floored_exp10 < 0 && abs_exp10_covered_by_powers_table);
    float_normalized_decentralized(&components, du.sign, precision,
        abs_number, normalization, floored_exp10);
    return vprintf_float_decimalism_or_normalized(stream, &components,
        field_width, precision, flags, floored_exp10);
}

static inline int vprintf_float_f_or_g(struct ns_stream_base *stream, double num,
    int field_width, int precision, unsigned int flags)
{
    struct ns_double_components components_num;

    if (num < FORMAT_FLOAT_F_RANGE_MIN || num > FORMAT_FLOAT_F_RANGE_MAX)
        return 0;
    if (precision < 0)
        precision = 6;
    float_decentralized(num, &components_num, precision);
    return vprintf_float_decimalism_or_normalized(stream, &components_num,
        field_width, precision, flags, 0);
}

static int vprintf_float(struct ns_stream_base *stream, double num,
    int field_width, int precision, unsigned int flags)
{
    int n = 0;

    if (isinf(num) || isnan(num)) {
        char *out_str = NULL;
        if (isinf(num)) {
            if (num < 0) {
                out_str = flags & FORMAT_LARGE ? "-INF" : "-inf";
            } else {
                if (flags & FORMAT_PLUS) {
                    out_str = flags & FORMAT_LARGE ? "+INF" : "+inf";
                } else if (flags & FORMAT_SPACE) {
                    out_str = flags & FORMAT_LARGE ? " INF" : " inf";
                } else {
                    out_str = flags & FORMAT_LARGE ? "INF" : "inf";
                }
            }
        } else if (isnan(num)) {
            out_str = flags & FORMAT_LARGE ? "NAN" : "nan";
        }
        n += vprintf_string(stream, out_str, field_width, -1, flags);
        return n;
    }

    if (flags & FORMAT_FLOAT_F || flags & FORMAT_FLOAT_G) {
        n += vprintf_float_f_or_g(stream, num, field_width, precision, flags);
        if (n > 0) return n;
    }
    flags &= (~(FORMAT_FLOAT_F | FORMAT_FLOAT_G));
    flags |= FORMAT_FLOAT_E;
    return vprintf_float_e(stream, num, field_width, precision, flags);
}

/* ================================================================== */
/*  数组格式化输出（%q / %Q）                                            */
/* ================================================================== */

static int vprintf_array(struct ns_stream_base *stream, const uint8_t *array,
    int field_width, int precision, unsigned int flags, enum ns_format_qualifier qualifier)
{
    const char *digits = small_digits;
    const uint8_t *item;
    char item_size;
    int valid_len;
    int array_len;
    int array_reality_len;
    int remainder;
    int space_pad_len = 0;
    int n = 0;

    if (precision < 0) return 0;

    if (flags & FORMAT_LARGE)
        digits = large_digits;

    switch (qualifier) {
        case NS_FORMAT_QUALIFIER_LONG:
            item_size = sizeof(unsigned long);
            break;
        case NS_FORMAT_QUALIFIER_LONG_LONG:
            item_size = sizeof(unsigned long long);
            break;
        case NS_FORMAT_QUALIFIER_SHORT:
            item_size = sizeof(unsigned short);
            break;
        case NS_FORMAT_QUALIFIER_CHAR:
            item_size = sizeof(unsigned char);
            break;
        default:
            item_size = sizeof(unsigned int);
            break;
    }

    array_len = precision / item_size;
    remainder = precision % item_size;
    array_reality_len = array_len - (remainder ? 1 : 0);
    valid_len = (array_reality_len * (item_size * 2)) + (array_reality_len - 1);
    if (field_width > valid_len)
        space_pad_len = field_width - valid_len;

    if (!(flags & FORMAT_LEFT)) {
        for (int i = 0; i < space_pad_len; i++, n++)
            streamout_in_byte(stream, ' ');
    }

    item = array;
    for (int i = 0; i < array_len; i++, item += item_size) {
        if (i) {
            streamout_in_byte(stream, ' ');
            n++;
        }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        for (int j = item_size - 1; j >= 0; j--, n += 2) {
#else
        for (int j = 0; j < item_size; j++, n += 2) {
#endif
            streamout_in_byte(stream, digits[(item[j] >> 4) & 0x0f]);
            streamout_in_byte(stream, digits[(item[j]) & 0x0f]);
        }
    }
    if (remainder) {
        streamout_in_byte(stream, ' ');
        n++;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        for (int i = 0; i < (item_size - remainder); i++, n += 2) {
            streamout_in_byte(stream, '?');
            streamout_in_byte(stream, '?');
        }
        for (int j = remainder - 1; j >= 0; j--, n += 2) {
            streamout_in_byte(stream, digits[(item[j] >> 4) & 0x0f]);
            streamout_in_byte(stream, digits[(item[j]) & 0x0f]);
        }
#else
        for (int j = 0; j < remainder; j++, n += 2) {
            streamout_in_byte(stream, digits[(item[j] >> 4) & 0x0f]);
            streamout_in_byte(stream, digits[(item[j]) & 0x0f]);
        }
        for (int i = 0; i < (item_size - remainder); i++, n += 2) {
            streamout_in_byte(stream, '?');
            streamout_in_byte(stream, '?');
        }
#endif
    }

    if (flags & FORMAT_LEFT) {
        for (int i = 0; i < space_pad_len; i++, n++)
            streamout_in_byte(stream, ' ');
    }
    return n;
}

/* ================================================================== */
/*  整数格式化输出                                                       */
/* ================================================================== */

static inline int vprintf_number(struct ns_stream_base *stream,
    unsigned long long num, int field_width, int precision, unsigned int flags,
    enum ns_base_type base)
{
    char _number_buf[FORMAT_STACK_CACHE_SIZE];
    char *number_buf = _number_buf;
    char sign = 0;
    char *special = NULL;
    const char *digits = small_digits;
    int bit_count;
    int n = 0;
    int special_count = 0;
    int reality_count = 0;
    int zeropad_count = 0;
    int spacepad_count = 0;

    if (base > NS_BASE_TYPE_HEX) return 0;

    if (flags & FORMAT_LARGE)
        digits = large_digits;

    /* 正数化 */
    if (flags & FORMAT_SIGNED) {
        if ((long long)num < 0) {
            sign = '-';
            /* LLONG_MIN (-9223372036854775808): -num wraps in unsigned
             * arithmetic back to 0x8000000000000000, but unsigned division
             * produces the correct digit sequence ("9223372036854775808"). */
            num = -num;
        }
    }
    if (sign != '-') {
        if (flags & FORMAT_PLUS) {
            sign = '+';
        } else if (flags & FORMAT_SPACE) {
            sign = ' ';
        }
    }

    bit_count = num_bit_count(num, (int)base);

    if (bit_count > FORMAT_STACK_CACHE_SIZE) {
        number_buf = (char *)ns_platform_alloc((size_t)bit_count);
        if (number_buf == NULL) return 0;
    }
    for (int i = (int)(bit_count - 1); i >= 0; i--)
        number_buf[i] = digits[num_rsh(&num, (int)base)];

    if (flags & FORMAT_SPECIAL) {
        if (base == NS_BASE_TYPE_OCT) {
            special = "0";
            special_count = 1;
        } else if (base == NS_BASE_TYPE_HEX) {
            special = flags & FORMAT_LARGE ? "0X" : "0x";
            special_count = 2;
        } else if (base == NS_BASE_TYPE_BIN) {
            special = flags & FORMAT_LARGE ? "0B" : "0b";
            special_count = 2;
        }
    }

    reality_count = ((sign) ? 1 : 0) + special_count + bit_count;

    if (precision >= 0) {
        flags &= ~FORMAT_ZEROPAD;
        if (precision > bit_count)
            zeropad_count = precision - bit_count;
        reality_count = reality_count + zeropad_count;
    }

    if (field_width > reality_count) {
        if (flags & FORMAT_ZEROPAD) {
            zeropad_count = field_width - reality_count;
        } else {
            spacepad_count = field_width - reality_count;
        }
    }

    /* 右对齐空格填充 */
    if (!(flags & FORMAT_LEFT)) {
        for (int i = 0; i < spacepad_count; i++, n++)
            streamout_in_byte(stream, ' ');
    }

    /* 符号位 */
    if (sign) {
        streamout_in_byte(stream, sign);
        n++;
    }
    /* 特殊字符 0x 0X 0 */
    for (int i = 0; i < special_count; i++, n++)
        streamout_in_byte(stream, special[i]);
    /* 中间 0 填充 */
    for (int i = 0; i < zeropad_count; i++, n++)
        streamout_in_byte(stream, '0');
    /* 数字有效位 */
    for (int i = 0; i < bit_count; i++, n++)
        streamout_in_byte(stream, number_buf[i]);

    /* 左对齐空格填充 */
    if (flags & FORMAT_LEFT) {
        for (int i = 0; i < spacepad_count; i++, n++)
            streamout_in_byte(stream, ' ');
    }
    if (number_buf != _number_buf)
        ns_platform_free(number_buf);
    return n;
}

/* ================================================================== */
/*  主格式化引擎                                                        */
/* ================================================================== */

static int streamout_vprintf(struct ns_stream_base *stream,
    const char *fmt, va_list args)
{
    int n = 0;
    unsigned int flags;
    int field_width;
    int precision;
    enum ns_base_type base;
    enum ns_format_qualifier qualifier;
    unsigned long long num;
    double double_num;
    const char *fmt_start;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            streamout_in_byte(stream, *fmt);
            n++;
            continue;
        }
        fmt_start = fmt;
        flags = 0x00;
        while (1) {
            ++fmt;
            if (*fmt == '-')
                flags |= FORMAT_LEFT;
            else if (*fmt == '+')
                flags |= FORMAT_PLUS;
            else if (*fmt == ' ')
                flags |= FORMAT_SPACE;
            else if (*fmt == '#')
                flags |= FORMAT_SPECIAL;
            else if (*fmt == '0')
                flags |= FORMAT_ZEROPAD;
            else
                break;
        }

        field_width = -1;
        if (isdigit((int)(*fmt))) {
            field_width = skip_atoi(&fmt);
        } else if (*fmt == '*') {
            ++fmt;
            field_width = va_arg(args, int);
            if (field_width < 0) {
                field_width = -field_width;
                flags |= FORMAT_LEFT;
            }
        }

        precision = -1;
        if (*fmt == '.') {
            ++fmt;
            if (isdigit((int)(*fmt))) {
                precision = skip_atoi(&fmt);
            } else if (*fmt == '*') {
                ++fmt;
                precision = va_arg(args, int);
            }
            if (precision < 0)
                precision = 0;
        }

        qualifier = NS_FORMAT_QUALIFIER_NONE;
        if (*fmt == 'h') {
            fmt++;
            qualifier = NS_FORMAT_QUALIFIER_SHORT;
            if (*fmt == 'h') {
                fmt++;
                qualifier = NS_FORMAT_QUALIFIER_CHAR;
            }
        } else if (*fmt == 'l') {
            fmt++;
            qualifier = NS_FORMAT_QUALIFIER_LONG;
            if (*fmt == 'l') {
                fmt++;
                qualifier = NS_FORMAT_QUALIFIER_LONG_LONG;
            }
        } else if (*fmt == 'L') {
            fmt++;
            qualifier = NS_FORMAT_QUALIFIER_LONG_LONG;
        } else if (*fmt == 'z') {
            fmt++;
            qualifier = NS_FORMAT_QUALIFIER_SIZE_T;
        }

        base = NS_BASE_TYPE_DEC;

        switch (*fmt) {
            case 's': {
                n += vprintf_string(stream, va_arg(args, char *),
                    field_width, precision, flags);
                continue;
            }
            case 'd':
                NS_FALLTHROUGH;
            case 'i':
                flags |= FORMAT_SIGNED;
                NS_FALLTHROUGH;
            case 'u':
                goto _print_number;
            case 'X':
                flags |= FORMAT_LARGE;
                NS_FALLTHROUGH;
            case 'x':
                base = NS_BASE_TYPE_HEX;
                goto _print_number;
            case 'c': {
                n += vprintf_char(stream, (char)va_arg(args, int),
                    field_width, flags);
                continue;
            }
            case '%': {
                streamout_in_byte(stream, '%');
                n++;
                continue;
            }
            case 'B':
                flags |= FORMAT_LARGE;
                NS_FALLTHROUGH;
            case 'b': {
                base = NS_BASE_TYPE_BIN;
                goto _print_number;
            }
            case 'o': {
                base = NS_BASE_TYPE_OCT;
                goto _print_number;
            }
            case 'p': {
                if (field_width < 0) {
                    field_width = (int)((sizeof(void *) << 1) + 2);
                    flags |= FORMAT_ZEROPAD | FORMAT_SPECIAL;
                }
                n += vprintf_number(stream,
                    (uintptr_t)va_arg(args, void *),
                    field_width, precision, flags, NS_BASE_TYPE_HEX);
                continue;
            }
            case 'E':
                flags |= FORMAT_LARGE;
                NS_FALLTHROUGH;
            case 'e':
                flags |= FORMAT_FLOAT_E;
                goto _print_float;
            case 'G':
                flags |= FORMAT_LARGE;
                NS_FALLTHROUGH;
            case 'g':
                flags |= FORMAT_FLOAT_G;
                goto _print_float;
            case 'F':
                flags |= FORMAT_LARGE;
                NS_FALLTHROUGH;
            case 'f':
                flags |= FORMAT_FLOAT_F;
                goto _print_float;
            case 'Q':
                flags |= FORMAT_LARGE;
                NS_FALLTHROUGH;
            case 'q':
                n += vprintf_array(stream, va_arg(args, void *),
                    field_width, precision, flags, qualifier);
                break;
            default: {
                streamout_in_byte(stream, '%');
                fmt = fmt_start;
                continue;
            }
        }
        continue;

    _print_number:
        {
            switch (qualifier) {
                case NS_FORMAT_QUALIFIER_LONG:
                    num = (flags & FORMAT_SIGNED)
                        ? (unsigned long long)va_arg(args, signed long)
                        : (unsigned long long)va_arg(args, unsigned long);
                    break;
                case NS_FORMAT_QUALIFIER_LONG_LONG:
                    num = (flags & FORMAT_SIGNED)
                        ? (unsigned long long)va_arg(args, signed long long)
                        : (unsigned long long)va_arg(args, unsigned long long);
                    break;
                case NS_FORMAT_QUALIFIER_SHORT:
                    num = (flags & FORMAT_SIGNED)
                        ? (unsigned long long)((signed short)va_arg(args, int))
                        : (unsigned long long)((unsigned short)va_arg(args, int));
                    break;
                case NS_FORMAT_QUALIFIER_CHAR:
                    num = (flags & FORMAT_SIGNED)
                        ? (unsigned long long)((signed char)va_arg(args, int))
                        : (unsigned long long)((unsigned char)va_arg(args, int));
                    break;
                case NS_FORMAT_QUALIFIER_SIZE_T:
                    num = (flags & FORMAT_SIGNED)
                        ? (unsigned long long)va_arg(args, ssize_t)
                        : (unsigned long long)va_arg(args, size_t);
                    break;
                default:
                    num = (flags & FORMAT_SIGNED)
                        ? (unsigned long long)va_arg(args, signed int)
                        : (unsigned long long)va_arg(args, unsigned int);
                    break;
            }
            n += vprintf_number(stream, num, field_width, precision, flags, base);
            continue;
        }
    _print_float:
        {
            if (NS_FORMAT_QUALIFIER_LONG_LONG == qualifier)
                continue;
            double_num = va_arg(args, double);
            n += vprintf_float(stream, double_num, field_width, precision, flags);
            continue;
        }
    }
    return n;
}

/* ================================================================== */
/*  公开 API                                                             */
/* ================================================================== */

int ns_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    int n;
    struct ns_stream_memory stream;

    ns_stream_memory_init(&stream, (uint8_t *)buf, size);
    n = streamout_vprintf((struct ns_stream_base *)&stream, fmt, args);
    streamout_finish((struct ns_stream_base *)&stream);
    return n;
}

int ns_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    int n;
    va_list args;

    va_start(args, fmt);
    n = ns_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return n;
}

int ns_sprintf(char *buf, const char *fmt, ...)
{
    int n;
    va_list args;

    va_start(args, fmt);
    n = ns_vsnprintf(buf, (size_t)(UINTPTR_MAX - (uintptr_t)buf), fmt, args);
    va_end(args);
    return n;
}

int ns_vprintf(const char *fmt, va_list args)
{
    int n;
    n = streamout_vprintf(NS_STDOUT, fmt, args);
    return n;
}

int ns_printf(const char *fmt, ...)
{
    int n;
    va_list args;

    va_start(args, fmt);
    n = streamout_vprintf(NS_STDOUT, fmt, args);
    va_end(args);
    return n;
}

int ns_stream_vprintf(struct ns_stream_base *stream, const char *fmt, va_list args)
{
    int n;

    if (stream == NULL)
        return 0;
    n = streamout_vprintf(stream, fmt, args);
    return n;
}

int ns_stream_printf(struct ns_stream_base *stream, const char *fmt, ...)
{
    int n;
    va_list args;

    if (stream == NULL)
        return 0;
    va_start(args, fmt);
    n = ns_stream_vprintf(stream, fmt, args);
    va_end(args);
    return n;
}

void ns_stream_putc(struct ns_stream_base *stream, int c)
{
    if (stream == NULL)
        return;
    streamout_in_byte(stream, (char)c);
}

int ns_stream_puts(struct ns_stream_base *stream, const char *s)
{
    const char *p;
    size_t n = 0;

    if (stream == NULL)
        return 0;
    if (stream->type == NS_STREAM_TYPE_FUNCTION_NO_CACHE) {
        struct ns_stream_function_no_cache *f =
            (struct ns_stream_function_no_cache *)stream;
        n = strlen(s);
        f->write(stream, (uint8_t *)s, n);
        return (int)n;
    }
    p = s;
    while (*p)
        streamout_in_byte(stream, (char)*p++);
    return (int)(p - s);
}

void ns_stream_finish(struct ns_stream_base *stream)
{
    if (stream == NULL)
        return;
    streamout_finish(stream);
}
