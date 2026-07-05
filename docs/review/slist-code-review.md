# Slist 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### SLIST-001: `ns_slist_for_each` 宏在 C++ 下无法编译 (CRITICAL) `bug`

- **修复 commit**: 当前 diff
- **修复说明**: 在 `include/nanosig/nanosig_slist.h` 中添加 `#ifdef __cplusplus` 分支：C++17 下使用 `reinterpret_cast` 读取公共首指针，避免 `_Generic` 和 `({})` 语法；C11 下恢复原始 `->first` 读取方式（头指针 init 只执行一次，避免死循环）。C++ 编译和运行时均已验证通过。

---

## 现在打开的问题

（无）

---

## 现在关闭的问题

### SLIST-001: `ns_slist_for_each` 宏在 C++ 下无法编译 (CRITICAL) `bug`

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: 添加 `#ifdef __cplusplus` 框架：C++ 分支用 `reinterpret_cast` 读公共首指针，避免 `_Generic` 和 `({})` 语法。修复还修正了 C 分支中 memcpy 放到 condition 导致每次迭代重置 `list.first` 的死循环 bug。C++17 编译通过 + 运行时验证通过。
- **关闭日期**: 2026-07-04
