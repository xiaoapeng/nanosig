# List (双向链表) 代码审查报告

**日期**: 2026-07-05
**审查者**: claude-code-review

---

## 修复历史

(无)

---

## 现在打开的问题

### LIST-003: `ns_list_insert_between` 缺少 Doxygen 文档注释

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
`ns_list_insert_between`（第 60-69 行）是公开的 `static inline` 函数，被 `ns_list_push_front` 和 `ns_list_push_back` 调用，也直接被测试用例使用。但该函数没有 Doxygen 注释，而文件中所有其他公开函数均有完整文档。

#### review 建议
在 `ns_list_insert_between` 函数前添加 `/** @brief */` 风格的 Doxygen 注释。

#### 作者建议
已添加 Doxygen 注释，包括 @brief、@param 和 @note。
→ 关闭-已修复

#### 关闭原因
`include/nanosig/nanosig_list.h:60-69` 已添加完整 Doxygen 注释。

- 关闭日期: 2026-08-02
- 状态: 关闭-已修复

#### 定位
include/nanosig/nanosig_list.h:60-69

---

### LIST-004: 缺少 `ns_list_entry_safe`（NULL 安全版反查宏）

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: design

#### 问题描述
`nanosig_slist.h` 提供了 `ns_slist_entry_safe(ptr, type, member)` 宏（NULL 安全），但 `nanosig_list.h` 没有对应的 `ns_list_entry_safe` 宏。虽然 list 的遍历宏使用 `head` 哨兵而非 NULL 终止，但用户在 `ns_list_front`/`ns_list_back` 等返回值处理时可能需要 NULL 安全的 entry 反查。slist 已有此模式，list 侧缺失造成 API 对称性不一致。

#### review 建议
添加 `ns_list_entry_safe` 宏以保持与 slist 的 API 对称性：
```c
#define ns_list_entry_safe(ptr, type, member) \
    NS_CONTAINER_OF_SAFE((ptr), type, member)
```

#### 作者建议
已添加 `ns_list_entry_safe` 到 `include/nanosig/nanosig_list.h:214-216`，与 `ns_list_entry` 相邻，与 slist 的 `ns_slist_entry_safe` 语义一致。→ 关闭-已修复

#### 关闭原因
已在 `include/nanosig/nanosig_list.h:214-216` 添加。

- 关闭日期: 2026-07-18
- 状态: 关闭-已修复

#### 定位
include/nanosig/nanosig_list.h:214-216

---

## 现在关闭的问题

### LIST-001: `ns_list_for_each_entry_continue_safe` 初始化 `n` 与 `pos` 指向同一节点，遍历中移除当前节点导致无限循环或 NULL 解引用

- **状态**: 关闭-已拒绝
- **严重度**: 🔴 关键
- **关闭原因**: 宏实际正确。update 子句 `(pos) = (n), (n) = ns_list_entry((pos)->member.next, ...)` 中逗号运算符保证从左到右执行：先 `pos = n`（pos 指向旧的 n，有效节点），再从**新的 pos**（即旧的 n）取 next 派生 `n`。`n` 始终从旧 `n` 独立推进，不依赖被删除的旧 `pos`。review 中"初始化子句 pos 和 n 被赋值为同一个节点"的描述也不成立——init 子句中 `pos` 先更新为 `pos->next`，然后 `n` 从更新后的 `pos` 取 next，两者不同。
- **关闭日期**: 2026-07-05

---

### LIST-002: `ns_list_for_each_prev_entry_continue_safe` 同样存在 `n` 未独立预取的 bug

- **状态**: 关闭-已拒绝
- **严重度**: 🔴 关键
- **关闭原因**: 与 LIST-001 同类误判。update 子句先 `pos = n` 再从新 `pos`（旧 `n`）取 prev 派生 `n`，`n` 始终从有效节点推进。
- **关闭日期**: 2026-07-05

---
