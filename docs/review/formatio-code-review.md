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

## 现在打开的问题

### FORMATIO-007: `ns_stream_puts` 对 FUNCTION 类型逐个字节输出

- **状态**: 打开
- **严重度**: 🟢 较低（cleanup）

#### 问题描述

`src/ns_formatio.c:1216-1218` 中，`ns_stream_puts` 对缓存的 `NS_STREAM_TYPE_FUNCTION` 流使用逐个字节循环输出字符串。每字符调用一次 `streamout_in_byte`，触发 switch 分发、边界检查、指针递增和换行扫描。而 `NS_STREAM_TYPE_FUNCTION_NO_CACHE` 类型在 `src/ns_formatio.c:1209-1214` 有批量写入快速路径（单次 `f->write`）。

#### review 建议

为 `NS_STREAM_TYPE_FUNCTION` 类型添加缓存批量写入快速路径：用 `memcpy` 一次性复制到缓存剩余空间，然后 flush。仅在需要换行检测时回退到逐字节路径。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
// 80 字节字符串，FUNCTION 流
ns_stream_puts(stream, "0123456789... (80 chars)");
// 80 次 streamout_in_byte 调用
// 每次包含 switch + bounds check + newline scan
```

#### 定位
src/ns_formatio.c:1216-1218

---

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

## 现在关闭的问题

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
