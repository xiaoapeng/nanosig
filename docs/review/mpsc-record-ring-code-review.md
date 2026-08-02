# MPSC Record Ring 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### MPSC-001: MSVC meta 位布局不兼容

- **修复 commit**: 当前 diff（`__SIZEOF_SIZE_T__ == 8u` → `SIZE_MAX > UINT32_MAX`）
- **修复说明**: MSVC 不定义 `__SIZEOF_SIZE_T__`，64 位 MSVC 选错 32 位布局。改用标准 `SIZE_MAX` 宏，兼容 GCC/Clang/MSVC。
- **来源**: 深度代码审查 3.1

### MPSC-004: 文档与实现不一致：`part_count == 0`

- **修复 commit**: 当前 diff
- **修复说明**: 头文件 `@param part_count` 从"必须大于 0"改为"允许为 0（推送零大小记录）"；`@retval NS_E_INVAL` 删除 `part_count 为 0` 的表述。文档与实现统一：`part_count == 0` 允许，行为等价于 `ns_mpsc_record_ring_try_push(ring, NULL, 0u)`。

### MPSC-002: 注释语法问题 "perform 3 step"

- **修复 commit**: 当前 diff（注释重写为"下面的顺序是高性能铁律，不能动"）
- **修复说明**: 英语语法问题随注释整体改写解决。
- **来源**: 深度代码审查 7.2

---

## 现在打开的问题

### MPSC-011: `MPSC_MEMORY_ORDER.md` 描述已删除的固定容量 MPSC 队列，未文档化 record ring 的内存序协议

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
`docs/MPSC_MEMORY_ORDER.md` 描述的是已删除的固定容量 MPSC 队列（`nanosig_mpsc.h`），包括 `ns_mpsc_try_push` / `ns_mpsc_try_pop` 等已不存在的函数。当前仓库只有 MPSC record ring，其内存序协议（reserve_pos CAS + valid-bit release + consumer acquire）仅在内联代码注释中描述，无独立文档。

#### review 建议
创建 `docs/MPSC_RECORD_RING_MEMORY_ORDER.md` 或更新 `MPSC_MEMORY_ORDER.md` 添加 record ring 章节，文档化三阶段发布链。

#### 作者建议
已重写 `docs/MPSC_MEMORY_ORDER.md`，删除旧版固定容量 MPSC 队列描述，替换为 MPSC record ring 的三阶段发布链（reserve → write → commit）和完整内存序协议文档。
→ 关闭-已修复

#### 关闭原因
`docs/MPSC_MEMORY_ORDER.md` 已重写为 record ring 内存序协议文档。

- 关闭日期: 2026-08-02
- 状态: 关闭-已修复

#### 定位
docs/MPSC_MEMORY_ORDER.md（全文）

## 现在关闭的问题

### MPSC-003: `try_pushv` CAS 重试循环未刷新 `read_pos`

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
误报。代码第 365 行的 `read_pos = ns_atomic_load_explicit(...)` 实际位于 `for(;;)` 循环体内（与 `write_pos` 并列加载），CAS 失败后下一次迭代会重新读取。不存在"陈旧 read_pos"的问题。

#### 关闭日期
2026-07-04

---

### MPSC-005: init 循环用原子 release-store 逐槽清零

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
设计意图。`mark_uncommitted` 的原子 release-store 是文档化的防御性初始化策略，保证未提交 header 的 `valid=0` 在多线程下即时可见，杜绝消费者读取残留数据。改为 `memset` 在调用方已 zero-init 的 buffer 上等价，但失去"任意输入 buffer 都被正确初始化"的保证。

#### 关闭日期
2026-07-04

---

### MPSC-006: `release()` 上界检查使用 `>` 而非 `>=`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计意图。`record_addr == storage_end` 表示"缓冲区末尾之后一个地址"，类似 C 数组的 one-past-end iterator 语义，永不解引用但作为合法哨兵存在。改为 `>=` 会拒绝合法边界地址。

#### 关闭日期
2026-07-04

---

### MPSC-007: `ns_capacity_t` 枚举限制容量范围

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计约束。`ns_capacity_t` 是枚举类型，受 C 标准约束须在 `int` 范围内，已在头文件文档中明确说明。改为 `size_t` 会破坏 API 二进制兼容性，且 1 GiB 上限已远超实际使用场景。

#### 关闭日期
2026-07-04

---

### MPSC-008: `plan_push` 从 `const` 参数强制转换 `ring`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
内部静态辅助函数。`plan_push` 是仅在本文件内部使用的辅助函数，`const` 仅作为设计意图提示（不修改 ring 的逻辑字段），需要调用 `header_at` 时 cast 是不可避免的（非 const 重载会破坏 API 对称性）。

#### 关闭日期
2026-07-04

---

### MPSC-009: 注释使用非正式指令语气

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
风格偏好，非缺陷。热路径代码注释有意保持简洁以避免阅读者注意力的分散。如需详细解释，应放在头文件中的函数文档而非内联注释。

#### 关闭日期
2026-07-04

---

### MPSC-010: `free_capacity` 通过双重强制转换丢弃 `const`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
C 语言限制。C 原子变量不可声明为 const 但物理上可修改。`free_capacity` 的 const cast 是读取原子值的必要操作，逻辑上仍为"观察而非修改"。分离读写路径会显著增加代码量而无实际收益。

#### 关闭日期
2026-07-04

### MPSC-004: 文档与实现不一致：`part_count == 0`

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 头文件文档统一：`@param part_count` 改为"允许为 0（推送零大小记录）"，`@retval NS_E_INVAL` 删除 `part_count 为 0` 的表述。
- **关闭日期**: 2026-07-04
