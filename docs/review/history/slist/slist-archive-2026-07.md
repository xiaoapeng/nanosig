# slist review 归档 2026-07

<a id="SLIST-001"></a>
### SLIST-002: `ns_slist_for_each_entry_safe` 在非空链表末尾迭代时解引用空指针 (UB/crash)

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: `ns_slist_entry()` 替换为 `ns_slist_entry_safe()`（NULL 安全版），循环条件改为 `pos != NULL`，init/increment 中对 `pos` 做 NULL 守卫后再派生 `n`。源自 Linux 内核循环链表模式移植到 NULL 终止链表的 UB 已消除。
- **关闭日期**: 2026-07-05

---

<a id="SLIST-001"></a>

---

### SLIST-001: `ns_slist_for_each` 宏在 C++ 下无法编译 (CRITICAL) `bug`

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: 添加 `#ifdef __cplusplus` 框架：C++ 分支用 `reinterpret_cast` 读公共首指针，避免 `_Generic` 和 `({})` 语法。修复还修正了 C 分支中 memcpy 放到 condition 导致每次迭代重置 `list.first` 的死循环 bug。C++17 编译通过 + 运行时验证通过。
- **关闭日期**: 2026-07-04

---
