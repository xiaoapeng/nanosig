# ringbuf review 归档 2026-07

<a id="RINGBUF-004"></a>
### RINGBUF-024: `peek`/`peek_copy` 绕回路径及 `draft_write` 非零 offset 路径无测试覆盖

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: test

#### 问题描述
以下关键路径完全未测试：
1. `ns_ringbuf_peek` 绕回路径（`rl > read_size_first_max` 分支）
2. `ns_ringbuf_peek_copy` 绕回路径
3. `ns_ringbuf_draft_write` 非零 offset
4. `ns_ringbuf_peek` 数据不足返回 NULL 路径

#### review 建议
在 `test_ds_ringbuf.c` 中增加绕回测试、数据不足测试、draft_write 非零 offset 测试。

#### 作者建议
已添加 `test_wrap_paths` 测试函数，覆盖：
- peek 绕回路径（size=5, r=3, rl=3 > read_size_first_max=2）
- peek_copy 绕回路径（同布局）
- peek 数据不足返回 NULL
- peek_copy 数据不足返回 0
- draft_write 非零 offset（offset=1）
→ 关闭-已修复

#### 关闭原因
`test/unit/test_ds_ringbuf.c` 新增 `test_wrap_paths` 函数，覆盖全部 4 条缺失路径。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已修复

#### 定位
test/unit/test_ds_ringbuf.c
src/ds/ns_ringbuf.c:220-238
src/ds/ns_ringbuf.c:260-268
src/ds/ns_ringbuf.c:128

---

<a id="RINGBUF-002"></a>

---

### RINGBUF-003: 读写位置 + offset 溢出（peek/peek_copy/draft_write）

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: 同 RINGBUF-002 根因，被 init 的 `size ≤ UINT32_MAX/3` 上限检查封住。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-004"></a>

---

### RINGBUF-004: 文档说 len 是 int32_t 但代码已是 uint32_t

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 头文件 `@note` 更新，size 上限同步更新为 `UINT32_MAX/3`。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-006"></a>

---

### RINGBUF-006: `ns_ringbuf_clear` 的非原子访问

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 文档标注 reader-thread-only。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-007"></a>

---

### RINGBUF-007: peek 的 *len 是 int32_t* 而其余是 uint32_t

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `peek` 的 `*len` 改为 `uint32_t*`。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-008"></a>

---

### RINGBUF-008: draft_write 返回 -1 与其他函数不一致

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 改用 `NS_E_RANGE` 替代硬编码 -1，属于 nanosig 状态码体系。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-009"></a>

---

### RINGBUF-009: (int32_t)wl 返回值强制转换依赖 size ≤ INT32_MAX

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 编译期静态断言 + 头文件 @note 双重保证。详见修复历史。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-010"></a>

---

### RINGBUF-010: w/r 冗余重载

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 七个内部读/写函数全部预加载 w/r 到局部变量，消除双重内存读取。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-014"></a>

---

### RINGBUF-014: NULL→0 契约变更破坏 backward compatibility

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: `ns_ringbuf_size`/`free_size`/`total_size` 改为 `int32_t` 返回，NULL 时返回 -1；头文件文档同步标注 NULL 行为。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-017"></a>

---

### RINGBUF-017: `ns_ringbuf_data_size` 过度耦合 (LOW) `cleanup`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **定位**: `src/ds/ns_ringbuf.c:56-61`
- **关闭原因**: 签名改为 `(uint32_t size, uint32_t w, uint32_t r)`，返回值改为 `int32_t`，消除结构体耦合。详见修复历史。
- **关闭日期**: 2026-07-04

---

<a id="RINGBUF-002"></a>

---

### RINGBUF-002: `ns_ringbuf_fix` 宏加法溢出（size > 1.43 GiB）

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: init 已增加 `size ≤ UINT32_MAX/3` 上限检查，`w + wl < 3*size ≤ UINT32_MAX` 不再溢出。
- **关闭日期**: 2026-07-04

---
