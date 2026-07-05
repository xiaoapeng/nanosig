# Rbtree 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### 2026-07-04 — 第一轮修复

- **RBTREE-002**: `ns_rbtree_node_t` 添加 `NS_ALIGNED(sizeof(long))` 对齐属性，
  确保自指针 sentinel 与 RED/BLACK color bit 不冲突。头文件文档说明
  嵌入节点的用户结构体应避免 packed 修饰导致奇数偏移。
- **RBTREE-004**: 提取 `ns_rb_sibling_left` / `ns_rb_sibling_right` helper 函数，
  消除 `ns_rb_delete_fixup` 中 6 处 `sibling == NULL ? NULL : sibling->left`
  冗余三元表达式。
- **RBTREE-005**: 头文件级别 `@section encoding` 注释解释空节点 sentinel
  编码为 RED 的原因（自指针低 bit 为 0 → RED，与直觉相反但安全）。
- **RBTREE-007**: 在 `ns_rbtree_remove` 中 `original_color` 旁添加 inline 注释，
  说明 `original_color` 是被移除槽位的原始颜色（决定是否调用 delete_fixup），
  以及 successor 继承 node 颜色的不变量含义。

---

## 现在打开的问题

无 — 全部已关闭。

---

## 现在关闭的问题

### RBTREE-001: `ns_rbtree_remove` 中 `size` 下溢防护静默掩盖错误

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 作者决定保持现状。`ns_rb_node_can_remove` + `ns_rbtree_node_init`
  已能阻止重复移除，size 下溢在正确使用时不会触发。
- **关闭日期**: 2026-07-04

### RBTREE-002: `ns_rbtree_node_init` 自指针哨兵依赖指针 ≥2 字节对齐

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: `ns_rbtree_node_t` 添加 `NS_ALIGNED(sizeof(long))`，
  编译期保证结构体按 long 对齐，地址低 bit 可安全用作颜色编码。
  头文件文档说明 packed 嵌入约束。
- **关闭日期**: 2026-07-04

### RBTREE-003: `ns_rbtree_find_new_add` 中 `match` 与 `cmp` 不一致会损坏 leftmost 缓存

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `nanosig_rbtree.h` 头文件已有 `@attention` 说明约束；
  本轮修复在 `ns_rbtree.c` 顶部 `@section match_vs_cmp` 中重申语义约束
  与典型做法（直接传递 cmp 的包装作为 match）。
- **关闭日期**: 2026-07-04

### RBTREE-004: `ns_rb_delete_fixup` 中冗余的 NULL 三元表达式与可读性问题

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 提取 `ns_rb_sibling_left` / `ns_rb_sibling_right` helper 函数，
  删除函数体中所有 `sibling == NULL ? NULL : sibling->left` 三元模式，
  改为 `ns_rb_sibling_left(sibling)` 调用。
- **关闭日期**: 2026-07-04

### RBTREE-005: `ns_rbtree_node_is_empty` 中 RED/BLACK 编码不直观

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 头文件 `@section encoding` 注释解释空节点 sentinel
  `parent_and_color == self` 自指针的最低位为 0（因 sizeof(long) 对齐），
  因此 sentinel 自然编码为 RED，并说明这与"空 = BLACK"的直觉相反但安全。
- **关闭日期**: 2026-07-04

### RBTREE-006: 两子节点删除分支中 `child` 的 parent 更新路径有微妙差异

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 作者决定保持现状，与 Linux 内核 rbtree 实现对齐。
- **关闭日期**: 2026-07-04

### RBTREE-007: `ns_rb_set_color` 与 `original_color` 的关系需要 inline 注释

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: `ns_rbtree_remove` 中 `original_color` 旁添加注释，
  说明其是被移除槽位的原始颜色、决定是否需要修复，以及 successor 继承
  node 颜色的不变量含义。
- **关闭日期**: 2026-07-04

### RBTREE-008: 删除修复中兄弟节点 `sibling` 的访问模式可读性差

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 作者决定保持现状（已被 RBTREE-004 部分缓解——通过 helper 函数
  封装带 NULL 检查的 sibling child 访问）。
- **关闭日期**: 2026-07-04