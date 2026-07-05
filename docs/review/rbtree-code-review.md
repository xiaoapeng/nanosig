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

### RBTREE-009: `ns_rbtree_insert` 返回值文档与实现不一致

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: doc

#### 问题描述
`nanosig_rbtree.h` 中 `ns_rbtree_insert` 的文档声称"如果插入后该节点成为最左节点，返回值即为该节点；否则返回 NULL"。但实际实现（`ns_rbtree.c:374`）在成功插入时**始终**返回 `node`，无论是否成为 leftmost。

#### review 建议
将 `@brief` 和 `@return` 改为一致描述："成功插入返回该节点；输入无效或节点已链接时返回 NULL。"删除矛盾描述。

#### 作者建议
（待作者补充）

#### 定位
`include/nanosig/nanosig_rbtree.h:106-114`

---

### RBTREE-010: `ns_rbtree_find_new_add` 缺少对 `new_node` 返回值的已链接检查

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
`ns_rbtree_find_new_add` 在调用 `new_node(user_data)` 回调后，直接将返回的节点插入树中，未检查该节点是否已链接到某棵树。对比 `ns_rbtree_insert` 有 `ns_rbtree_node_is_linked` 防御性检查。如果 `new_node` 返回已链接节点，会同时损坏两棵树。

#### review 建议
在 `new_node` 返回值检查之后添加 `if(ns_rbtree_node_is_linked(node)) return NULL;`。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
static ns_rbtree_node_t *bad_new_node(void *user_data) {
    return &global_existing_node;  /* 该节点已链接到 global_tree */
}
ns_rbtree_find_new_add(key, &tree, match_fn, NULL, bad_new_node);
/* 结果：两棵树同时损坏 */
```

#### 定位
`src/ds/ns_rbtree.c:572-579`

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