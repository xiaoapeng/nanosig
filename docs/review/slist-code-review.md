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

### SLIST-003: `ns_slist_del_node_in_for_each_safe` 不重置被删除节点的 `next` 指针

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: cleanup

#### 问题描述
`ns_slist_del_node_in_for_each_safe` 将节点从链表中摘除，但不调用 `ns_slist_node_init` 重置被删除节点的 `next` 指针。而 `ns_slist_pop_front` 和 `ns_slist_remove_after` 均调用了 `ns_slist_node_init(node)`。被删除的非尾节点仍持有 stale `next` 指针，`ns_slist_node_is_on_list` 检查会错误返回 true。

#### review 建议
在函数末尾添加对被删除节点的 `next` 清零，与 `pop_front` / `remove_after` 保持一致。或在 docstring 中说明此行为差异。

#### 作者建议
（待作者补充）

#### 定位
`include/nanosig/nanosig_slist.h:276-285`

---

### SLIST-004: `ns_slist_for_each` 宏双重求值 `head` 参数

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: cleanup

#### 问题描述
`ns_slist_for_each` 宏的 init 表达式中，`head` 被求值两次（NULL 检查 + `->next` 访问）。若 `head` 是带副作用的表达式，副作用执行两次。

#### review 建议
将 `head` 缓存到临时变量，仅求值一次。或在文档中声明 `head` 不得是带副作用的表达式。

#### 作者建议
（待作者补充）

#### 定位
`include/nanosig/nanosig_slist.h:367`

---

### SLIST-005: `ns_slist_for_each` 文档描述与实际遍历起始位置不符

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
docstring 写道"是节点时从该节点开始遍历"，但实际 `pos` 的起始值是 `(head)->next`（即给定节点的下一个节点），给定节点被跳过。

#### review 建议
修改为"是节点时从该节点的 next 开始遍历（跳过该节点本身）"。

#### 作者建议
（待作者补充）

#### 定位
`include/nanosig/nanosig_slist.h:358-359`

---

### SLIST-006: `ns_slist_append_list` 自拼接导致数据损坏

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_slist_append_list(list, other)` 在 `list == other` 时：若链表非空，`list->last->next = other->first` 成为自引用环，然后 `ns_slist_init(other)` 清空整个链表，数据全部丢失。

#### review 建议
在函数入口添加 `if(list == other) return;` 防御性检查，或在 docstring 中显式声明 `@pre list != other`。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
ns_slist_append_list(&list, &list);  // 自拼接 → 数据全部丢失
assert(ns_slist_empty(&list));  // true — 链表数据丢失
```

#### 定位
`include/nanosig/nanosig_slist.h:290-303`

---

## 现在关闭的问题

### SLIST-002: `ns_slist_for_each_entry_safe` 在非空链表末尾迭代时解引用空指针 (UB/crash)

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: `ns_slist_entry()` 替换为 `ns_slist_entry_safe()`（NULL 安全版），循环条件改为 `pos != NULL`，init/increment 中对 `pos` 做 NULL 守卫后再派生 `n`。源自 Linux 内核循环链表模式移植到 NULL 终止链表的 UB 已消除。
- **关闭日期**: 2026-07-05

---

### SLIST-001: `ns_slist_for_each` 宏在 C++ 下无法编译 (CRITICAL) `bug`

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: 添加 `#ifdef __cplusplus` 框架：C++ 分支用 `reinterpret_cast` 读公共首指针，避免 `_Generic` 和 `({})` 语法。修复还修正了 C 分支中 memcpy 放到 condition 导致每次迭代重置 `list.first` 的死循环 bug。C++17 编译通过 + 运行时验证通过。
- **关闭日期**: 2026-07-04
