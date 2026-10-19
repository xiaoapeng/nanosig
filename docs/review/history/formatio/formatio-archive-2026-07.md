# formatio review 归档 2026-07

<a id="FORMATIO-002"></a>
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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

src/ns_debug.c:52

---

<a id="FORMATIO-003"></a>

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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

src/ns_formatio.c:1092-1093

---

<a id="FORMATIO-004"></a>

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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

- include/nanosig/ns_formatio.h:74 → 已改
- src/ns_formatio.c:1149 → 已改

---

<a id="FORMATIO-006"></a>

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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

- include/nanosig/ns_formatio.h:12 → 已改
- include/nanosig/ns_debug.h:13 → 已改

---

<a id="FORMATIO-008"></a>

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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

- src/ns_formatio.c:16 → 已改
- src/ns_debug.c:11 → 已改

---

<a id="FORMATIO-011"></a>

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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

src/ns_formatio.c:108-118

---

<a id="FORMATIO-013"></a>

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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

src/ns_formatio.c:811-813

---

<a id="FORMATIO-015"></a>

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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

src/ns_formatio.c:507-509

---

<a id="FORMATIO-016"></a>

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

- **关闭日期**: 2026-07-11
- **状态**: 关闭-已修复

#### 定位

src/ns_formatio.c:1030-1032

---
