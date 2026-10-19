# rbtree review 归档 2026-07

<a id="RBTREE-002"></a>
### RBTREE-002: `ns_rbtree_node_init` 自指针哨兵依赖指针 ≥2 字节对齐

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: `ns_rbtree_node_t` 添加 `NS_ALIGNED(sizeof(long))`，
  编译期保证结构体按 long 对齐，地址低 bit 可安全用作颜色编码。
  头文件文档说明 packed 嵌入约束。
- **关闭日期**: 2026-07-04

---

<a id="RBTREE-003"></a>

---

### RBTREE-003: `ns_rbtree_find_new_add` 中 `match` 与 `cmp` 不一致会损坏 leftmost 缓存

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `nanosig_rbtree.h` 头文件已有 `@attention` 说明约束；
  本轮修复在 `ns_rbtree.c` 顶部 `@section match_vs_cmp` 中重申语义约束
  与典型做法（直接传递 cmp 的包装作为 match）。
- **关闭日期**: 2026-07-04

---

<a id="RBTREE-004"></a>

---

### RBTREE-004: `ns_rb_delete_fixup` 中冗余的 NULL 三元表达式与可读性问题

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 提取 `ns_rb_sibling_left` / `ns_rb_sibling_right` helper 函数，
  删除函数体中所有 `sibling == NULL ? NULL : sibling->left` 三元模式，
  改为 `ns_rb_sibling_left(sibling)` 调用。
- **关闭日期**: 2026-07-04

---

<a id="RBTREE-005"></a>

---

### RBTREE-005: `ns_rbtree_node_is_empty` 中 RED/BLACK 编码不直观

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 头文件 `@section encoding` 注释解释空节点 sentinel
  `parent_and_color == self` 自指针的最低位为 0（因 sizeof(long) 对齐），
  因此 sentinel 自然编码为 RED，并说明这与"空 = BLACK"的直觉相反但安全。
- **关闭日期**: 2026-07-04

---

<a id="RBTREE-007"></a>

---

### RBTREE-007: `ns_rb_set_color` 与 `original_color` 的关系需要 inline 注释

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: `ns_rbtree_remove` 中 `original_color` 旁添加注释，
  说明其是被移除槽位的原始颜色、决定是否需要修复，以及 successor 继承
  node 颜色的不变量含义。
- **关闭日期**: 2026-07-04

---
