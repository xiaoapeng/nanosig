# Formatio 模块 Code Review

> 审查范围：`ns_formatio.h/c` + `ns_debug.h/c` + `CMakeLists.txt`（新增源文件注册）
> 审查日期：2026-07-11
> 审查等级：max（10 角度查找 + 6 验证器 + sweep）
> 审查 ID：FORMATIO-REVIEW-20260711

---

## 修复历史

### 2026-07-11

| ID | 摘要 | 修复 | 日期 |
|----|------|------|------|
| FORMATIO-004 | `ns_sprintf`/`ns_vsprintf` 指针相减 UB → 改用 `UINTPTR_MAX - (uintptr_t)buf` | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-002 | `ns_dbg_vprintf` 无条件时钟 syscall → 移入 `MONOTONIC_CLOCK` 分支 | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-003 | `%zd` va_arg 类型不匹配 → 确认已使用 `ssize_t` | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-005 | printf 热路径堆分配 → 设计权衡，拒绝 | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-016 | `%p` 截断指针 → 确认已使用 `(uintptr_t)` | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-015 | `vprintf_float_decimalism_or_normalized` 加 alloc NULL 检查 | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-013 | `vprintf_number` LLONG_MIN 边界加注释 | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-011 | `ns_double_union` 加 static_assert + 注释文档化假设 | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-010 | `float_decentralized` %e 路径 UB → 误报，拒绝 | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-014 | `vprintf_array` item_size 类型 → 设计权衡，拒绝 | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-006 | 头文件守卫 `NS_*_H` → `NANOSIG_*_H` | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-008 | 删除未使用的 `#include <stdio.h>` | 工作区（待 commit） | 2026-07-11 |
| FORMATIO-009 | `bastardized_floor` double→int UB → 设计权衡，拒绝 | 工作区（待 commit） | 2026-07-11 |

---


### 2026-08-09

| ID | 摘要 | 修复 | 日期 |
|----|------|------|------|
| FORMATIO-017 | `%e`/`%.0e`/`%E` 对 0.0 输出垃圾值 → 0.0 分支补 raw_factor=1.0 | 已修复 | 2026-08-09 |
| FORMATIO-018 | `ns_snprintf(buf,0,...)` 写 buf[-1] → finish 加 size==0 守卫 | 已修复 | 2026-08-09 |
| FORMATIO-019 | `ns_sprintf` 指针越界 UB → 无界模式(end==NULL)+ns_vsprintf 移入 .c | 已修复 | 2026-08-09 |
| FORMATIO-007 | `ns_stream_puts` FUNCTION 逐字节 → 设计权衡(缓存已摊销 flush)，关闭-已拒绝 | 关闭-已拒绝 | 2026-08-09 |

---
## 现在打开的问题

### FORMATIO-012: `NS_MACRO_DEBUG_LEVEL` 仅检查首字符，多位数等级会错误

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **关闭原因**: C 预处理器在函数式宏中无法检测宏是否已定义。若去掉 `NS_MACRO_DEBUG_LEVEL`、直接比较 `NS_DBG_MODULE_LEVEL_##name`，则未定义模块等级时编译报错。当前方案在"未定义时编译通过"和"已定义时等级正确"之间做了最佳取舍。内置等级只有 0-5（个位数），不受此问题影响。多位数等级在本项目中不会出现。
- **关闭日期**: 2026-07-12

#### 问题描述

`include/nanosig/ns_debug.h:78` 中 `NS_MACRO_DEBUG_LEVEL(name)` 将参数字符串化后取 `[0] - '0'`。内置等级（0-5）都是个位数，所以碰巧正确。但等级值 `10` 会被评估为 `'1' - '0' = 1`，导致等级门控错误地开启或关闭输出。

#### review 建议

去掉 `NS_MACRO_DEBUG_LEVEL`，让模块级宏直接比较 `NS_DBG_MODULE_LEVEL_##name` 与 `(level)` 的值：

```c
#define ns_mprintln(name, tag, level, fmt, ...) ({ \
    int n = 0; \
    if ((uint32_t)NS_DBG_MODULE_LEVEL_##name >= (uint32_t)(level)) { \
        n = ns_dbg_println((level), "[" #tag "] ", fmt, ##__VA_ARGS__); \
    } \
    _ns_dbg_feign_return(n); \
})
```

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
#define NS_DBG_MODULE_LEVEL_FOO 10
// 预期: 等级 10 >= NS_DBG_ERR(1) → 输出
// 实际: NS_MACRO_DEBUG_LEVEL(10) = '1' - '0' = 1, 1 >= 1 → 碰巧输出
// 但如果等级设为 12: '1' - '0' = 1, 1 >= 5(NS_DBG_DEBUG) 为假 → 错误地不输出
```

#### 定位

`include/nanosig/ns_debug.h:78`

---

### FORMATIO-020: `ns_snprintf(NULL, 0, ...)` 把 buf 写空指针（无界哨兵与包 size==0 冲突）

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述

`ns_vsnprintf` 用 `ns_stream_memory_init(&stream, buf, size)` 初始化内存流（src/ns_formatio.c:1157），
`ns_stream_memory_init` 内部执行 `stream->end = buf + size`（include/nanosig/ns_formatio.h:115）。
当 `buf == NULL` 且 `size == 0` 时得到 `end == NULL`，而 `end == NULL` 正是 FORMATIO-019
为 `ns_vsprintf` 引入的"无界流"哨兵（src/ns_formatio.c:1169、src/ns_formatio.c:240、src/ns_formatio.c:269-271）。
于是 `ns_snprintf(NULL, 0, ...)` 被误判为无界流，`streamout_in_byte` 的 MEMORY 分支
无条件执行 `*m->pos = ch`（src/ns_formatio.c:241-243），对 NULL 解引用写入；收尾时
`*m->pos = '\0'` 同样写 NULL（src/ns_formatio.c:269-271）。

C99 明确允许 `snprintf(NULL, 0, fmt, ...)` 用于探测所需长度（size 为 0 时 buf 可以是空指针），
公共头 include/nanosig/ns_formatio.h:70-75 没有任何 docstring 记录"buf 不得为 NULL"这一前提，
因此不属于 §4.4 合同前提保护。FORMATIO-018 修好了 `buf != NULL, size == 0`，
但没有覆盖 `buf == NULL, size == 0` 这一 C99 合法输入。

同一根因的第二个入口：`size == 0` 的探测式调用若配合 `ns_vsnprintf` 直接传 NULL 也走同一分支。

#### review 建议

把"无界"与"零容量"两种状态区分开，不要共用 `end == NULL`：

- 方案 A（最小改动）：在 `ns_vsnprintf` 入口对 `buf == NULL || size == 0` 走计数型丢弃路径
  （end 指向一个内部哑缓冲，或给 stream 增加显式 `unbounded` 标志），返回值仍为应写全长；
- 方案 B：给 `struct ns_stream_memory` 增加 `bool unbounded` 字段，`ns_vsprintf` 显式置位，
  `streamout_in_byte` / `streamout_finish` 改为判断该标志而非 `end == NULL`；
- 无论哪种方案，都应在 ns_formatio.h 为 `ns_snprintf` / `ns_vsnprintf` 补 docstring，
  写明 `size == 0` 时 buf 可为 NULL、返回值为应写全长。

修复后建议在测试中补 `ns_snprintf(NULL, 0, "hello %d", 42)` 与 `ns_snprintf(NULL, 0, "%f", 1.5)`
的探测用例（期望返回应写全长且不崩溃）。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
/* ASAN 实测：SEGV on unknown address 0x000000000000，WRITE memory access */
ns_snprintf(NULL, 0, "hello %d", 42);
/* 崩溃栈：
 *   #0 streamout_in_byte src/ns_formatio.c:242
 *   #1 streamout_vprintf src/ns_formatio.c:944
 *   #2 ns_vsnprintf      src/ns_formatio.c:1158
 *   #3 ns_snprintf       src/ns_formatio.c:1181
 */
```

对照（已修复、不崩溃）：

```c
char buf[16];
ns_snprintf(buf, 0, "hello %d", 42);  /* 返回 8，buf 不被写入 */
```

#### 定位
src/ns_formatio.c:1157
src/ns_formatio.c:241-243
src/ns_formatio.c:269-271
src/ns_formatio.c:1168-1169
include/nanosig/ns_formatio.h:109-116

---

### FORMATIO-021: `%e`/`%E` 对 subnormal（0 < |x| < DBL_MIN）指数错误、数值丢失

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述

`log10_of_positive`（src/ns_formatio.c:171-190）通过 `du.exponent - FORMAT_DBL_EXP_OFFSET`
取二进制指数，并把 `du.exponent` 强行写回 1023 后计算尾数。该推导隐含"IEEE 754 规格化数、
尾数有隐含前导 1"的假设；subnormal（exponent 字段为 0）不满足该假设，于是同一函数对
所有 subnormal 返回几乎相同的估计值（实测恒为约 -307.95），与真实十进制指数无关。

`vprintf_float_e` 随后用该估计值做"整数化 + 修正"（src/ns_formatio.c:647-653）：

```c
double exp10 = log10_of_positive(abs_number);
floored_exp10 = bastardized_floor(exp10);
double p10 = pow10_of_int(floored_exp10);
if (abs_number < p10) { floored_exp10--; p10 /= 10; }
```

对 subnormal 而言 `floored_exp10` 停在 -308/-309，导致 `%e` 输出的指数与尾数全错，
甚至在默认精度下把有效数值完全丢掉（输出 0.000000）：

| 输入 | nanosig 输出 | libc snprintf |
|------|--------------|---------------|
| 5e-324 | `0.000000e-309` | `4.940656e-324` |
| 1e-310 | `0.100000e-309` | `1.000000e-310` |
| 2.5e-310 | `0.250000e-309` | `2.500000e-310` |
| 1e-315 | `0.000001e-309` | `1.000000e-315` |
| 1e-309 | `1.000000e-309`（碰巧正确） | `1.000000e-309` |

`%g`/`%G` 走同一路径（`vprintf_float` 对超范围回退 `%e`，src/ns_formatio.c:705-711），
因此 subnormal 的 `%g` 输出同样错误（5e-324 → `0.000000`）。DBL_MIN 及以上
（1e-308、2.2250738585072014e-308）正常，问题边界是 `(|x| < DBL_MIN) && (|x| > 0)`。

#### review 建议

在指数估计前显式处理 subnormal：

- 方案 A：在 `vprintf_float_e`（以及任何复用 `log10_of_positive` 的路径）入口判断
  `du.exponent == 0 && du.mantissa != 0`，先把输入按 2^52 归一化（`x * 4503599627370496.0`
  或用 `frexp`/`ldexp` 组合）再进入原有十进制估计，并在最终指数上扣回该缩放量；
- 方案 B：`log10_of_positive` 内部对 `exp2 == 0` 单独分支，先用 `x * 2^52` 规格化后再套用
  现有泰勒展开，避免"把 0 指数当作 -1023 并使用隐含 1.0 尾数"；
- 补一条 subnormal 的表驱动用例（5e-324 / 1e-310 / 1e-315 / DBL_MIN/2）并与 libc 对拍。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
char buf[64];
ns_snprintf(buf, sizeof(buf), "%e", 5e-324);   /* 实际 "0.000000e-309"，libc "4.940656e-324" */
ns_snprintf(buf, sizeof(buf), "%e", 1e-310);   /* 实际 "0.100000e-309"，libc "1.000000e-310" */
ns_snprintf(buf, sizeof(buf), "%e", 1e-315);   /* 实际 "0.000001e-309"，libc "1.000000e-315" */
ns_snprintf(buf, sizeof(buf), "%g", 5e-324);   /* 实际 "0.000000"，libc "4.94066e-324" */
ns_snprintf(buf, sizeof(buf), "%e", 1e-320);   /* 实际 "0.000000e-309"（数值全部丢失） */
```

`log10_of_positive` 对 1e-310 的估计值为 -307.94997197808431（真实值 -310），
对 5e-324 为 -307.95184826398543（真实值 -323.306），二者几乎相同，可见与输入无关。

#### 定位
src/ns_formatio.c:171-190
src/ns_formatio.c:646-657
src/ns_formatio.c:705-711

---

### FORMATIO-022: `float_decentralized` 半途(tie)分支进位丢失，`%.1f` 0.95 输出 "0.10"

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述

`float_decentralized`（src/ns_formatio.c:391-427）的 `remainder > 0.5` 分支在自增
`fractional` 后会检查是否越过 `powers_of_10[precision]` 并进位到 `integral`
（src/ns_formatio.c:410-415）；但紧随其后的 `remainder == 0.5` 分支（src/ns_formatio.c:416-419）
只做 `++components->fractional`，**没有**同样的越界进位检查：

```c
} else if ((remainder == 0.5) &&
           ((components->fractional == 0U) || (components->fractional & 1U))) {
    ++components->fractional;          /* 可为 powers_of_10[precision]，无进位处理 */
}
```

结果 `fractional` 变成等于 `powers_of_10[precision]`，输出位数比精度多一位且数值错误：

实测 0.95（双精度实际值 0.94999999999999995559）在 `%.1f` 下：
`integral=0`、`fractional=9`、`remainder=0.5` → tie 分支把 `fractional` 增到 10 →
`fractional_valid_len = num_bit_count(10) = 2` → 输出 `"0.10"`。
正确结果应为 `"0.9"`（libc）或按进位写成 `"1.0"`；`"0.10"` 与两者都不符，且读起来是 0.10 而非 0.9。

#### review 建议

在 tie 分支的 `++components->fractional` 之后补上与 `remainder > 0.5` 分支相同的进位处理，
或把两个分支合并为"先算出按精度缩放后的四舍五入值，再做一次越界进位检查"的统一形式：

```c
if ((double)components->fractional >= powers_of_10[precision]) {
    components->fractional = 0;
    ++components->integral;
}
```

修复后补 `%.1f`/`%.2f`/`%.3f` 在 0.95、0.995、0.9995、0.05、1.95、9.95 等 tie 邻域
与 libc 的对拍用例。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
char buf[64];
ns_snprintf(buf, sizeof(buf), "%.1f", 0.95);    /* 实际 "0.10"（n=4），libc "0.9" */
ns_snprintf(buf, sizeof(buf), "%.2f", 0.995);   /* 实际 "0.100"（n=5），libc "0.99" */
ns_snprintf(buf, sizeof(buf), "%.3f", 0.9995);  /* 实际 "0.1000"（n=6），libc "1.000" */
ns_snprintf(buf, sizeof(buf), "%.1f", 1.95);    /* 实际 "1.10"（n=4），libc "1.9" */
```

#### 定位
src/ns_formatio.c:416-419
src/ns_formatio.c:410-415

---

### FORMATIO-023: `%.0e` 半途(tie)分支把进位清零，1.5 → "1e+00"、3.5 → "3e+00"

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述

`float_normalized_decentralized`（src/ns_formatio.c:429-470）的 tie 处理为：

```c
components->fractional = (uint64_t)scaled_remainder;
scaled_remainder -= (double)components->fractional;
components->fractional += (scaled_remainder >= rounding_threshold);   /* :462 */
if (scaled_remainder == rounding_threshold) {
    components->fractional &= ~((uint64_t)0x1);                       /* :464 把刚加上的 1 清掉 */
}
if ((double)components->fractional >= prec_power_of_10) { ... ++integral; }  /* :466 进不了 */
```

恰等 0.5 时先 `+1` 再用 `&= ~1` 清掉，且清尾之后 `integral` 不再变化，
等价于"tie 时永远向下取整"（只在整数部分恰为偶数时碰巧与银行家舍入一致）。
实测：`%.0e` 1.5 → `"1e+00"`、3.5 → `"3e+00"`、5.5 → `"5e+00"`、7.5 → `"7e+00"`，
而 libc 分别给出 `2e+00`/`4e+00`/`6e+00`/`8e+00`（round-half-even）。

同一模块内 `%.0f` 走 `float_decentralized` 的 precision==0 分支（src/ns_formatio.c:421-426），
1.5 → `"2"`、3.5 → `"4"`，与 `%.0e` 结果自相矛盾——同一数值同一精度，
`%f` 与 `%e` 得到不同舍入方向，可判定 `%e` 侧为缺陷而非有意权衡。

注：test/unit/test_formatio.c:693-694 的黄金值把 `"2e+00"`/`"3e+00"` 固化为期望
（注释称"银行家舍入清零奇位"），3.5 → 4 才是银行家舍入，现有黄金值是缺陷快照，需随修复更新。

#### review 建议

把 tie 分支改为先做尾数清位、再判断是否需要进位到 `integral`，例如：

```c
if (scaled_remainder == rounding_threshold) {
    components->fractional &= ~((uint64_t)0x1);
}
if ((double)components->fractional >= prec_power_of_10) {
    components->fractional = 0;
    ++components->integral;
}
```

或统一改为"按 `integral` 奇偶决定 tie 是否进位"，使其与 `%.0f` 的银行家舍入语义一致，
并同步修正 test/unit/test_formatio.c:693-694 的黄金值。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
char buf[64];
ns_snprintf(buf, sizeof(buf), "%.0e", 1.5);   /* 实际 "1e+00"，libc "2e+00" */
ns_snprintf(buf, sizeof(buf), "%.0e", 3.5);   /* 实际 "3e+00"，libc "4e+00" */
ns_snprintf(buf, sizeof(buf), "%.0e", 5.5);   /* 实际 "5e+00"，libc "6e+00" */
ns_snprintf(buf, sizeof(buf), "%.0e", 2.5);   /* "2e+00"（碰巧正确） */
/* 同模块内不自洽： */
ns_snprintf(buf, sizeof(buf), "%.0f", 3.5);   /* "4"，而 %.0e 3.5 给 "3e+00" */
```

#### 定位
src/ns_formatio.c:459-469
src/ns_formatio.c:421-426

---

### FORMATIO-024: `%q`/`%Q` 字段宽度在 precision 小于单个元素时下溢，宽度反而变大

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述

`vprintf_array`（src/ns_formatio.c:718-810）用 precision 反推可见长度：

```c
array_len = precision / item_size;                       /* :754 */
remainder = precision % item_size;
array_reality_len = array_len - (remainder ? 1 : 0);     /* :756 可为 0 或 -1 */
valid_len = (array_reality_len * (item_size * 2)) + (array_reality_len - 1);  /* :757 */
if (field_width > valid_len)
    space_pad_len = field_width - valid_len;
```

当 `precision < item_size`（例如 `%.0hhq`、`%.1q`）时 `array_reality_len` 为 0 乃至负数
（`array_len == 0` 且 `remainder != 0` 时是 -1），`valid_len` 计算为 -1 或更小的负数，
`space_pad_len = field_width - valid_len` 因此**超出请求宽度**：

- `"%20.0hhq"` → 输出 21 个字符（20 空格 + 换行不算，实测 n=21、strlen=21），宽度 20 被突破；
- `"%10.0hhq"` → n=11；
- `"%20.1q"`（`q` 的 item_size = sizeof(unsigned int) = 4）→ 30 个填充空格 + `" ??????44"`，
  n=39，远超请求的宽度 20。

另外 partial item 分支（src/ns_formatio.c:781-803）会额外输出 `2*remainder+1` 个字符
（`" ??"` 占位 + 分隔空格），这部分没有被计入 `valid_len`，因此 `precision % item_size != 0`
时宽度一律偏大。数据字节本身输出正确，问题集中在 padding 计算。

#### review 建议

不要从 precision 反推可见长度，改为在输出过程中累加实际字符数（与 `vprintf_number` 的
`reality_count` 同思路），或至少在计算 padding 前修正：

- `array_reality_len` 为 0 时应令 `valid_len = 0`（避免负数与 `-1` 下溢）；
- partial item 的 `2 * remainder + 1` 个字符要计入 `valid_len`；
- `space_pad_len` 计算后防御性钳制 `>= 0`。

补 `%20.0hhq`、`%10.1hhq`、`%20.1q`、`%20.5q` 的宽度断言用例。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
char buf[128];
int n = ns_snprintf(buf, sizeof(buf), "%20.0hhq", "");
/* 实际 n=21、strlen=21（全空格），请求宽度 20 */
n = ns_snprintf(buf, sizeof(buf), "%20.1q",
                (const unsigned[]){0x11223344u, 0u, 0u, 0u});
/* 实际 n=39、strlen=39，请求宽度 20；多出 30 个填充空格 */
```

#### 定位
src/ns_formatio.c:754-759
src/ns_formatio.c:781-803


### FORMATIO-027: 格式串宽度解析存在有符号溢出 / 取负 UB

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述

两处宽度解析缺少边界保护，均已用 UBSAN 复现：

1. `skip_atoi`（src/ns_formatio.c:286-292）逐位累加进 `int i`，无溢出检查：

```c
while (isdigit((int)(**s)))
    i = i * 10 + (*((*s)++) - '0');   /* :290 */
```

`ns_snprintf(buf, 64, "%99999999999d", 1)` 触发
`runtime error: signed integer overflow: 999999999 * 10 cannot be represented in type 'int'`，
得到的 `field_width` 是 1215752191，随后 `vprintf_number` 的填充循环按该宽度迭代
（有界流只是被逐字节截断并返回 12 亿级的 n；若走 `ns_sprintf`/`ns_vsprintf` 的无界流
（end == NULL），会向调用方缓冲区写入 10 亿级字节 → 越界写）。

2. `*` 宽度取负（src/ns_formatio.c:969-976）：

```c
field_width = va_arg(args, int);
if (field_width < 0) {
    field_width = -field_width;    /* :973 INT_MIN 取负 UB */
```

`ns_snprintf(buf, 64, "%*d", INT_MIN, 1)` 触发
`runtime error: negation of -2147483648 cannot be represented in type 'int'`。

两者都在"格式串来自外部输入"时可达（`ns_printf(fmt)` / `ns_stream_printf(stream, fmt)`
的 fmt 完全可由调用方传入）。ns_formatio.h:67-81 未对宽度上限做任何文档化约束。

#### review 建议

- `skip_atoi` 改为在 `i > (INT_MAX - d) / 10` 时停止累加（或饱和到 INT_MAX），
  并在 `streamout_vprintf` 中对最终 `field_width` / `precision` 做上限钳制
  （例如拒绝 > 1<<20 的宽度并转为忽略该字段，或直接返回 0）；
- `*` 宽度取负改为安全写法：`field_width = -(unsigned)field_width` 后钳制，
  或先判断 `field_width == INT_MIN` 时按 0 处理；
- 若选择"不限制宽度"，则必须在公共头文档化"宽度由调用方格式串保证合理"这一前提
  （但即便如此，`%99999999999d` 的 UB 本身仍应消除）。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
char buf[64];
ns_snprintf(buf, sizeof(buf), "%99999999999d", 1);
/* UBSAN: src/ns_formatio.c:290:15 signed integer overflow: 999999999 * 10 ... */
/* 实际 n=1215752191（64 字节缓冲），填充循环迭代 10 亿次 */

ns_snprintf(buf, sizeof(buf), "%*d", INT_MIN, 1);
/* UBSAN: src/ns_formatio.c:973:31 negation of -2147483648 cannot be represented in type 'int' */
```

#### 定位
src/ns_formatio.c:286-292
src/ns_formatio.c:969-976


## 现在关闭的问题

### FORMATIO-029: `ns_stream_puts` 对 NULL 字符串不设防，与同族 API 的 NULL 容忍不一致

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **类型**: cleanup

#### 问题描述
`ns_stream_puts` 对 `s` 的 NULL 不做处理，必然崩溃。公共头未文档化任何 NULL 约束，属行为不一致。

#### 关闭原因
合同前提保护：已在 `include/nanosig/ns_formatio.h:81` 的 `@param s` 中文档化"不可为 NULL"。调用方传 NULL 属违反文档化前置条件，行为未定义，不属库缺陷。

- 关闭日期: 2026-10-19
- 状态: 关闭-已拒绝

---
### FORMATIO-028: `%g` 等多处 C99 语义差异未在公共头文档化

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
printf 族未文档化与 C99 的差异（`%g` 等价于 `%f`、负精度钳为 0、`%.0d` 零值输出 "0"、`%#x` 零值加前缀等）。

#### 关闭原因
已在 `include/nanosig/ns_formatio.h:67-81` 的 printf 族 docstring 中列出与 C99 的差异清单。调用方按文档差异使用即可，不属库缺陷。

- 关闭日期: 2026-10-19
- 状态: 关闭-已拒绝

---
### FORMATIO-026: doc 推荐的 `NS_DBG_MODULE_LEVEL_DEFAULT` 从未定义，门控恒为 30

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: doc

#### 问题描述
`NS_DBG_MODULE_LEVEL_DEFAULT` 在仓库中从未定义，按 doc 推荐写法接入的模块门控恒为 30（无条件输出所有等级）。

#### 关闭原因
已在 `include/nanosig/ns_debug.h:56` 补 `#ifndef NS_DBG_MODULE_LEVEL_DEFAULT ... #define NS_DBG_MODULE_LEVEL_DEFAULT NS_DBG_SYS ... #endif`，带保护避免与 CMake/外部定义冲突。

- 关闭日期: 2026-10-19
- 状态: 关闭-已修复

---
### FORMATIO-025: 调试层互斥锁不可重入，输出 sink 回灌调试宏即自死锁

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_dbg_raw` / `ns_vdbg_raw` 在持 `ns_dbg_mutex` 期间完成整条输出，sink 内回灌调试宏会在同一线程重复加锁 → 自死锁。

#### 关闭原因
合同前提保护：已在 `include/nanosig/ns_debug.h:140` 的 `ns_debug_init` docstring 中补充 `@warning`，明确"输出 sink 内禁止调用任何 ns_dbg_* / ns_errln / ns_m*ln 调试宏"。sink 回灌属调用方违反文档化前提，不属库缺陷。

- 关闭日期: 2026-10-19
- 状态: 关闭-已拒绝

---
### FORMATIO-007: `ns_stream_puts` 对 FUNCTION 类型逐个字节输出

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低（cleanup）

#### 问题描述

`src/ns_formatio.c:1216-1218` 中，`ns_stream_puts` 对缓存的 `NS_STREAM_TYPE_FUNCTION` 流使用逐个字节循环输出字符串。每字符调用一次 `streamout_in_byte`，触发 switch 分发、边界检查、指针递增和换行扫描。而 `NS_STREAM_TYPE_FUNCTION_NO_CACHE` 类型在 `src/ns_formatio.c:1209-1214` 有批量写入快速路径（单次 `f->write`）。

#### review 建议

为 `NS_STREAM_TYPE_FUNCTION` 类型添加缓存批量写入快速路径：用 `memcpy` 一次性复制到缓存剩余空间，然后 flush。仅在需要换行检测时回退到逐字节路径。

#### 作者建议

（2026-08-09 作者决策：关闭-已拒绝。逐字节实现是有意设计——缓存型 FUNCTION 流的 f->write 只在缓存满或换行时调用，缓存已批量摊销 I/O，逐字节的真实代价仅是每字符一次 streamout_in_byte 的 CPU 分发开销；MCU 场景短字符串下该开销可忽略，且无缓存场景已由 FUNCTION_NO_CACHE 的批量路径覆盖。批量 memcpy 优化仅省 CPU 指令、不省 I/O，投入产出比低，保持现状。）

#### 关闭原因

设计权衡：缓存已摊销 flush（仅缓存满/换行调用），逐字节仅 CPU 级开销，MCU 短串可忽略；无缓存场景已有 NO_CACHE 批量路径（src/ns_formatio.c:1249-1253）。作者决策保持现状。
- 关闭日期: 2026-08-09
- 状态: 关闭-已拒绝

#### 可重现的失败场景

```c
// 80 字节字符串，FUNCTION 流
ns_stream_puts(stream, "0123456789... (80 chars)");
// 80 次 streamout_in_byte 调用
// 每次包含 switch + bounds check + newline scan
```

#### 定位
src/ns_formatio.c:1254-1256
src/ns_formatio.c:1249-1253

---


### FORMATIO-001: `streamout_in_byte` 缓存满时输入字符静默丢弃

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 问题描述

`src/ns_formatio.c:196-212` 中 `streamout_in_byte` 的 `NS_STREAM_TYPE_FUNCTION` 分支在 `f->pos == f->end`（缓存已满）时字符可能丢失。

#### review 建议

将 flush 逻辑移到写入之前。

#### 作者建议

`f->pos == f->end` 在入口处不可能出现：每次写入填满最后槽位（`end-1→end`）后立即 flush 并重置 `pos = cache`。`if (f->pos < f->end)` 只是防御性守卫（`cache_size == 0` 属用户传参错误），不是 bug。→ 关闭-已拒绝

#### 关闭原因

误报。`f->pos == f->end` 在正常执行流中不会出现在 `streamout_in_byte` 入口处，因为每次写入填满最后一个槽位后立即触发 flush 并重置 `pos = cache`。第 203 行的 `if (f->pos < f->end)` 是防御性边界检查，而非错误的分支逻辑。

- 关闭日期: 2026-07-11
- 状态: 关闭-已拒绝

#### 定位

`src/ns_formatio.c:203-211`

---

### FORMATIO-002: `ns_dbg_vprintf` 无条件单调时钟系统调用

- **状态**: 关闭-已修复
- **严重度**: 🟠 高

#### 问题描述

`src/ns_debug.c:52` 无条件调用 `(void)ns_platform_clock_monotonic_us(&now_us)`，但 `now_us` 仅在第 57-61 行的 `NS_DBG_FLAGS_MONOTONIC_CLOCK` 分支内使用。

#### review 建议

将时钟获取移到 `if (flags & NS_DBG_FLAGS_MONOTONIC_CLOCK)` 分支内部。

#### 作者建议

已将时钟变量声明和调用移入 `MONOTONIC_CLOCK` 分支内，消除无条件系统调用。同时将 `now_us` 初始化为 0，防止时钟失败时输出垃圾值。→ 关闭-已修复

#### 关闭原因

已修复。`ns_dbg_vprintf` 的时钟获取现在仅在 `NS_DBG_FLAGS_MONOTONIC_CLOCK` 标志设置时执行。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

`src/ns_debug.c:52` → 已改

---

### FORMATIO-003: `%zd` 格式化 `va_arg` 类型不匹配

- **状态**: 关闭-已修复
- **严重度**: 🟡 中

#### 问题描述

`src/ns_formatio.c:1091-1094` 中，`%zd` 路径使用 `va_arg(args, long long)`。在 LP64 上 `ssize_t` 是 `long`，`va_arg` 类型标记不匹配是 UB（C11 7.16.1.1p2）。在 LLP64（Windows x64）上 `long` 是 4 字节而 `long long` 是 8 字节，会损坏 `va_list`。

#### review 建议

改用 `va_arg(args, ssize_t)`。

#### 作者建议

代码已为 `va_arg(args, ssize_t)`，与原始 eventhub_os 一致。确认正确。→ 关闭-已修复

#### 关闭原因

代码实际已是 `va_arg(args, ssize_t)`，reviewer 误读。已确认正确。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

`src/ns_formatio.c:1092-1093` → 已确认

---

### FORMATIO-004: `ns_sprintf`/`ns_vsprintf` 指针相减未定义行为

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键

#### 问题描述

`include/nanosig/ns_formatio.h:74` 和 `src/ns_formatio.c:1149` 中，`ns_vsprintf`/`ns_sprintf` 使用 `(char *)(LONG_MAX) - buf` 计算缓冲区大小，违反 C11 6.5.6p9。

#### review 建议

改用 `SIZE_MAX` 或 `(size_t)-1` 作为安全上限。

#### 作者建议

改为 `UINTPTR_MAX - (uintptr_t)buf`，将指针转 `uintptr_t` 做整数减法，消除 UB。两处已同步修复。→ 关闭-已修复

#### 关闭原因

已修复。两处已改为 `(size_t)(UINTPTR_MAX - (uintptr_t)buf)`，不再涉及无关指针相减。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

- `include/nanosig/ns_formatio.h:74` → 已改
- `src/ns_formatio.c:1149` → 已改

---

### FORMATIO-005: printf 热路径堆分配

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 问题描述

`FORMAT_STACK_CACHE_SIZE` 仅为 16 字节（`src/ns_formatio.c:33`）。当格式化输出需要的数字位数超过 16 时，`vprintf_number`（第 826 行）和 `vprintf_float_decimalism_or_normalized`（第 507 行）会调用 `ns_platform_alloc` 分配堆内存。

#### review 建议

将 `FORMAT_STACK_CACHE_SIZE` 从 16 增大到 64 或 128。

#### 作者建议

超过 16 字节的情况很少（仅二进制格式和超大十进制），堆分配不是性能瓶颈，且 `ns_platform_alloc` 不在信号 emit 路径上。→ 关闭-已拒绝

#### 关闭原因

设计权衡：16 字节栈缓存覆盖了绝大多数场景（%d/%u 小值、%p、%x），仅二进制格式和超大十进制会触发分配，这些不属于热路径。`ns_platform_alloc` 的"emit 路径不得调用"约束针对信号 emit，printf 路径不在其列。

- 关闭日期: 2026-07-11
- 状态: 关闭-已拒绝

#### 定位

- `src/ns_formatio.c:826-829`（`vprintf_number`）
- `src/ns_formatio.c:507-509`（`vprintf_float_decimalism_or_normalized`）

---

### FORMATIO-006: 头文件包含守卫不符合项目约定

- **状态**: 关闭-已修复
- **严重度**: 🟡 中

#### 问题描述

所有 17 个现有头文件使用 `NANOSIG_*_H` 模式。两个新文件使用 `NS_*_H` 模式。

#### review 建议

改为 `NANOSIG_FORMATIO_H` 和 `NANOSIG_DEBUG_H`。

#### 作者建议

改吧 → 关闭-已修复

#### 关闭原因

两处头文件守卫和末尾 `#endif` 注释已同步改为 `NANOSIG_FORMATIO_H` / `NANOSIG_DEBUG_H`。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

- `include/nanosig/ns_formatio.h:12` → 已改
- `include/nanosig/ns_debug.h:13` → 已改

---

### FORMATIO-008: 未使用的 `#include <stdio.h>`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低（cleanup）

#### 问题描述

`src/ns_formatio.c:16` 和 `src/ns_debug.c:11` 包含 `<stdio.h>`，但未使用任何 stdio 符号。`ns_formatio.c` 实现了自己的 printf 族，`ns_debug.c` 使用 `ns_printf`/`ns_vprintf`。

#### review 建议

删除两个文件中未使用的 `#include <stdio.h>`。

#### 作者建议

改吧 → 关闭-已修复

#### 关闭原因

`ns_formatio.c` 的 `<stdio.h>` 已注释掉，`ns_debug.c` 的已删除。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

- `src/ns_formatio.c:16` → 已改
- `src/ns_debug.c:11` → 已改

---

### FORMATIO-009: `bastardized_floor` 将超范围 double 转为 int 是未定义行为

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 问题描述

`src/ns_formatio.c:129-135` 中 `bastardized_floor` 无条件执行 `(int)x` 转换。当前所有调用点传入的值均在约 [-308, 308] 范围内（log10 结果或 10 的幂），不会触发问题，但函数契约未文档化。

#### review 建议

在转换前添加范围检查，或为函数添加前置条件文档。

#### 作者建议

不修，拒绝 → 关闭-已拒绝

#### 关闭原因

作者认为当前所有调用点传入的值在约 [-308, 308] 范围内，不会超出 int 范围，不修。

- 关闭日期: 2026-07-11
- 状态: 关闭-已拒绝

#### 定位

`src/ns_formatio.c:129-135`

---

### FORMATIO-010: `float_decentralized` 中大 double 转 uint64_t 可能 UB

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 问题描述

`src/ns_formatio.c:374` 中 `components->integral = (uint64_t)abs_number` 在 `abs_number > UINT64_MAX` 时是未定义行为。`%e` 路径可能通过 `close_to_representation_extremum` 进入此路径。

#### review 建议

在转换前添加守卫，或确保 `%e` 路径不走 `float_decentralized`。

#### 作者建议

误报请说明原因 → 关闭-已拒绝

#### 关闭原因

误报。`%e` 路径在调用 `float_decentralized` 前已归一化：`scaled = apply_scaling(non_normalized, normalization)` 后 `scaled` 在约 [1, 10) 范围。`close_to_representation_extremum` 触发需 `floored_exp10` 极负（≈ -301），对应极小数，归一化后约 1.0。`%f` 路径已有 `FORMAT_FLOAT_F_RANGE_MAX = 1e18 < UINT64_MAX` 守卫。所有路径安全。

- 关闭日期: 2026-07-11
- 状态: 关闭-已拒绝

#### 定位

`src/ns_formatio.c:374`

---

### FORMATIO-011: `ns_double_union` 位域布局隐含 IEEE 754 字节序假设

- **状态**: 关闭-已修复
- **严重度**: 🟡 中

#### 问题描述

`src/ns_formatio.c:108-118` 的 `union ns_double_union` 位域分配顺序是 C11 实现定义的（J.3.9）。代码假设位域按 IEEE 754 的 MSB-to-LSB 顺序排列。

#### review 建议

添加编译期 `static_assert` 检查布局，或文档化假设。

#### 作者建议

文档化假设 → 关闭-已修复

#### 关闭原因

已添加 `NS_STATIC_ASSERT(sizeof(union ns_double_union) == sizeof(double))` 和注释文档化位域分配顺序假设。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

`src/ns_formatio.c:108-118` → 已改

---

### FORMATIO-013: `vprintf_number` 中 `LLONG_MIN` 取负后的无符号环绕

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低（cleanup）

#### 问题描述

`src/ns_formatio.c:811-813` 中，`num = -num` 在 `num == LLONG_MIN` 时依赖无符号环绕语义。

#### review 建议

添加注释说明此边界情况。

#### 作者建议

文档说明 → 关闭-已修复

#### 关闭原因

已在 `num = -num` 前添加注释说明 LLONG_MIN 边界情况。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

`src/ns_formatio.c:811-813` → 已改

---

### FORMATIO-014: `vprintf_array` 中 `item_size` 类型为 `char` 导致窄化转换

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低（cleanup）

#### 问题描述

`src/ns_formatio.c:697` 中 `char item_size` 被赋值为 `sizeof(...)` 的返回值（`size_t`）。

#### review 建议

将 `char item_size` 改为 `int item_size`。

#### 作者建议

不修，拒绝 → 关闭-已拒绝

#### 关闭原因

作者认为 sizeof 值始终正且小（1/2/4/8），`char` 类型不影响正确性。

- 关闭日期: 2026-07-11
- 状态: 关闭-已拒绝

#### 定位

`src/ns_formatio.c:697`

---

### FORMATIO-015: `vprintf_float_decimalism_or_normalized` 缺少 `ns_platform_alloc` NULL 检查

- **状态**: 关闭-已修复
- **严重度**: 🟠 高

#### 问题描述

`src/ns_formatio.c:507-509` 中 `ns_platform_alloc` 调用后未检查返回值，OOM 时栈缓冲区溢出。

#### review 建议

在 `ns_platform_alloc` 调用后添加 NULL 检查。

#### 作者建议

修掉吧 → 关闭-已修复

#### 关闭原因

已添加 `if (number_buf == NULL) return 0;`，与 `vprintf_number` 的写法对齐。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

`src/ns_formatio.c:507-509` → 已改

---

### FORMATIO-016: `%p` 在 LLP64 Windows 上截断指针

- **状态**: 关闭-已修复
- **严重度**: 🟠 高

#### 问题描述

`src/ns_formatio.c:1030-1032` 中 `%p` 使用 `(unsigned long)va_arg(args, void *)`，LLP64 上 `unsigned long` 是 4 字节。

#### review 建议

使用 `uintptr_t` 代替 `unsigned long`。

#### 作者建议

代码已为 `(uintptr_t)va_arg(args, void *)`，`uintptr_t` 在任何平台上都等于指针宽度。→ 关闭-已修复

#### 关闭原因

代码实际已是 `(uintptr_t)va_arg(args, void *)`，reviewer 误读。已确认正确。

- 关闭日期: 2026-07-11
- 状态: 关闭-已修复

#### 定位

`src/ns_formatio.c:1030-1032` → 已确认

### FORMATIO-017: `%e`/`%.0e` 对精确 0.0/-0.0 输出垃圾值（NaN→uint64_t 转换 UB）

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **标签**: bug

#### 问题描述

`vprintf_float_e`（src/ns_formatio.c:621）的 `abs_number == 0.0` 分支（:634-635）只设置
`floored_exp10 = 0`，没有设置 `normalization.raw_factor`。`normalization` 在 :629 初始化为
`{ 0 }`，因此 raw_factor 保持 0。随后 `float_normalized_decentralized` → `apply_scaling(0.0,
{raw_factor=0})` 内部做 `x / raw_factor`，0.0/0.0 = NaN，再把 NaN 赋给 `(uint64_t)`（
`components->integral = (uint64_t)scaled`，:449 附近）触发未定义行为，输出
"9223372036854775809.000000e+00"（2^63+1）这样的垃圾值。

测试暴露：本测试套件（test/unit/test_formatio.c）在构造用例时发现，按"只记录不修"原则将
`%e` 0.0 用例排除（见 test_formatio.c 用例 19 注释），未作为通过预期。

#### review 建议

在 `abs_number == 0.0` 分支内显式设置 `normalization.raw_factor = 1`（并使
`abs_exp10_covered_by_powers_table = true`），使 apply_scaling 得到 0.0/1.0=0.0，
输出正确的 "0.000000e+00"；或在该分支直接跳过归一化。修复后可在测试中补回
`%e`/`%.0e`/`%E` 的 0.0 与 -0.0 黄金值用例。

#### 作者建议

（2026-08-09 作者修复：vprintf_float_e 的 0.0 分支补充 normalization.raw_factor = 1.0，避免 apply_scaling 0.0/0.0 = NaN → (uint64_t)NaN UB；0.0 输出 "0.000000e+00"、-0.0 输出 "-0.000000e+00"。）

#### 可重现的失败场景

```c
char buf[64];
ns_snprintf(buf, sizeof(buf), "%e", 0.0);
// 实际: "9223372036854775809.000000e+00"（n=30）
// 期望: "0.000000e+00"
ns_snprintf(buf, sizeof(buf), "%.0e", 0.0);
// 实际: "9223372036854775809e+00"
ns_snprintf(buf, sizeof(buf), "%e", -0.0);
// 实际: "-9223372036854775809.000000e+00"
```

#### 定位
src/ns_formatio.c:634-647

---

### FORMATIO-018: `ns_snprintf(buf, 0, ...)` 在 size=0 时 `streamout_finish` 写 `buf[-1]` 下溢

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **标签**: bug

#### 问题描述

`ns_snprintf(buf, 0, ...)` → `ns_stream_memory_init(&stream, buf, 0)`（:1147）使
`m->end == buf`、`m->pos == buf`。`streamout_finish` 的 `NS_STREAM_TYPE_MEMORY` 分支
（:265-273）中 `m->pos < m->end` 为假，走 else 分支 `*(m->end - 1) = '\0'` 写入
`buf[-1]`——调用方缓冲区前 1 字节下溢。公共头 include/nanosig/ns_formatio.h 未文档化
size>0 前提，且属内存安全问题（违反前提仍值得防御性加固，见 review 规范 §4.4.5）。

已用 ASAN 实测确认：`streamout_finish` 在 src/ns_formatio.c:270 触发 stack-buffer-underflow，
WRITE of size 1。

测试暴露：本测试套件按计划 R2 禁用 size=0 作为通过用例（test_formatio.c 用例 5 注释），
以最小复现确认后录入。

#### review 建议

在 `streamout_finish` 的 MEMORY 分支对 `m->end > m->pos` 之外的 `m->end > 起始地址`
情形做防御：当 `size == 0`（`m->end == m->pos == buf`）时跳过写 NUL，或
`ns_stream_memory_init` 对 size==0 提前返回空输出。修复后可在测试中补回 size=0
边界用例（期望返回应写全长、缓冲区不被写入）。

#### 作者建议

（2026-08-09 作者修复：streamout_finish MEMORY 分支增加 size==0 守卫，size=0 时不写任何字节；ns_snprintf(buf,0,...) 返回应写全长，符合 C99。测试新增 size=0 用例。）

#### 可重现的失败场景

```c
char buf[16];
ns_snprintf(buf, 0, "hello %d", 42);
// ASAN: stack-buffer-underflow, WRITE of size 1 at src/ns_formatio.c:270
// 正常返回 n=8，但调用方缓冲区前 1 字节被写入 '\0'
```

#### 定位
src/ns_formatio.c:265-273

---

### FORMATIO-019: `ns_vsprintf`/`ns_sprintf` 经 `buf + size` 指针越界（FORMATIO-004 修复不完整，UBSAN 可见）

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **标签**: bug

#### 问题描述

FORMATIO-004 的修复把 `ns_vsprintf` 的指针相减改为整数减法
`(size_t)(UINTPTR_MAX - (uintptr_t)buf)`（include/nanosig/ns_formatio.h:74），
但该 size 随后传入 `ns_vsnprintf` → `ns_stream_memory_init`，
后者执行 `stream->end = buf + size`（include/nanosig/ns_formatio.h:118）——
`buf + (UINTPTR_MAX - (uintptr_t)buf)` 使指针索引溢出地址空间，
仍是 C11 6.5.6p8 未定义行为。UBSAN 实测报错：
"runtime error: pointer index expression with base ... overflowed to 0xffffffffffffffff"。

测试暴露：本测试套件 test_formatio.c 用例 8（test_sprintf）调用公共 API
`ns_sprintf` 时触发。该 UB 源自生产代码（ns_formatio.h:74,118），非测试引入。

#### review 建议

由作者决策：① 为内存流增加"无界"模式（end 不做边界限制），使 `ns_sprintf` 不走
`buf + size` 越界计算；② 或在头文件明确文档化 `ns_sprintf`/`ns_vsprintf`
"将地址空间视为缓冲区、指针算术可能溢出"的设计前提；③ 或改由 caller 提供足够大
的 size 的 `ns_snprintf` 替代。三选一消除 UBSAN 可见的指针溢出。

#### 作者建议

（2026-08-09 作者修复：采用无界模式——内存流 end==NULL 表示无界；ns_vsprintf 由 header static inline 改为 .c 实现，不再计算假 size；ns_sprintf 改调 ns_vsprintf；streamout_in_byte/streamout_finish 对 end==NULL 短路，消除 buf+size 指针越界 UB。）

#### 可重现的失败场景

```c
char buf[512];
ns_sprintf(buf, "%s", "hello");   /* ns_vsprintf → size = UINTPTR_MAX - (uintptr_t)buf */
/* UBSAN: include/nanosig/ns_formatio.h:118 pointer index expression overflowed */
```

#### 定位
include/nanosig/ns_formatio.h:74
include/nanosig/ns_formatio.h:118

---
