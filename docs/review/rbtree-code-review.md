# Rbtree 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### 2026-09-21 — 第二轮修复（cmp 字段 const-correctness）

- **RBTREE-012**: `ns_rbtree_root.cmp` 字段类型从
  `int (*)(ns_rbtree_node_t *, ns_rbtree_node_t *)` 改为
  `int (*)(const ns_rbtree_node_t *, const ns_rbtree_node_t *)`，
  允许调用方按 const-correctness 习惯写 cmp 函数（与 Linux 内核 rbtree 风格一致）。
  src/ns_timer.c:49 的 `ns_timer_cmp` 与 test/unit/test_ds_rbtree.c:17 的
  `tree_item_cmp` 同步加 const；内部调用点 src/ds/ns_rbtree.c:609/615/641
  因实参到 const 形参的隐式转换无需修改。

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

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: doc

#### 问题描述
`nanosig_rbtree.h` 中 `ns_rbtree_insert` 的文档声称"如果插入后该节点成为最左节点，返回值即为该节点；否则返回 NULL"。但实际实现（`ns_rbtree.c:374`）在成功插入时**始终**返回 `node`，无论是否成为 leftmost。

#### review 建议
将 `@brief` 和 `@return` 改为一致描述："成功插入返回该节点；输入无效或节点已链接时返回 NULL。"删除矛盾描述。

#### 作者建议
reviewer 误读代码。被审查的函数 `ns_rbtree_insert` 在此代码库中不存在。实际的两个插入函数 `ns_rbtree_add` 和 `ns_rbtree_find_add` 都与各自的文档一致：

- **`ns_rbtree_add`**：`return leftmost ? node : NULL;`（第 628 行） ✅ 与文档"正常返回 NULL，返回 node 说明插入最左节点"一致
- **`ns_rbtree_find_add`**：`return node;`（第 656 行） ✅ 与文档"返回找到的节点，如果没有找到返回插入的节点"一致

Timer 的依赖方（`ns_timer.c:136-140`）调用的是 `ns_rbtree_add`，其 leftmost 语义确认正确。代码自初始移植提交 `c8b4f58` 至今未变。→ 关闭-已拒绝

#### 关闭原因
reviewer 误读代码。`ns_rbtree_add` 的返回值为 `leftmost ? node : NULL`，与文档完全一致。

- 关闭日期: 2026-07-18
- 状态: 关闭-已拒绝

#### 定位
`include/nanosig/nanosig_rbtree.h:106-114`

---

### RBTREE-010: `ns_rbtree_find_new_add` 缺少对 `new_node` 返回值的已链接检查

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 在 `new_node` 返回值检查之后添加 `if(!ns_rbtree_node_is_empty(node)) return NULL;` 防御性检查，与 `ns_rbtree_find_add` 的一致。已在 `src/ds/ns_rbtree.c:684` 实施。
- **关闭日期**: 2026-07-11

#### 问题描述
`ns_rbtree_find_new_add` 在调用 `new_node(user_data)` 回调后，直接将返回的节点插入树中，未检查该节点是否已链接到某棵树。对比 `ns_rbtree_insert` 有 `ns_rbtree_node_is_linked` 防御性检查。如果 `new_node` 返回已链接节点，会同时损坏两棵树。

#### review 建议
在 `new_node` 返回值检查之后添加 `if(ns_rbtree_node_is_linked(node)) return NULL;`。

#### 作者建议
（已采纳）使用 `ns_rbtree_node_is_empty` 检查，与 `ns_rbtree_find_add` 的防御性检查保持一致。

#### 可重现的失败场景
```c
static ns_rbtree_node_t *bad_new_node(void *user_data) {
    return &global_existing_node;  /* 该节点已链接到 global_tree */
}
ns_rbtree_find_new_add(key, &tree, match_fn, NULL, bad_new_node);
/* 结果：两棵树同时损坏 */
```

#### 定位
src/ds/ns_rbtree.c:572-579

---

### RBTREE-012: `ns_rbtree_root.cmp` 字段类型未 const-correct

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: nanosig_rbtree.h:40 字段类型改为
  `int (*)(const ns_rbtree_node_t *, const ns_rbtree_node_t *)`；
  src/ns_timer.c:49 的 `ns_timer_cmp` 与 test/unit/test_ds_rbtree.c:17 的
  `tree_item_cmp` 同步加 const；内部调用点 src/ds/ns_rbtree.c:609/615/641
  因实参到 const 形参的隐式转换无需修改。A1 api-compile-check 由 1 条
  `-Wincompatible-pointer-types` warning 归零；`ctest -LE long-stability` 47/47 通过。
- **关闭日期**: 2026-09-21

#### 问题描述
include/nanosig/nanosig_rbtree.h:40 字段类型
`int (*cmp)(ns_rbtree_node_t *a, ns_rbtree_node_t *b)` 未带 const，
导致调用方按 const-correctness 写的 cmp 函数（如
test/unit/test_data_structures_contract_compile.c:100 的 `ds_contract_rbtree_cmp`）
无法直接赋值给字段，必须强转或放弃 const。GCC 报告 `-Wincompatible-pointer-types`。
该问题由 `sanitize-all` A1 api-compile-check 暴露（2026-09-21 audit）。

#### review 建议
将字段类型改为 `int (*)(const ns_rbtree_node_t *, const ns_rbtree_node_t *)`，
与 Linux 内核 `rb_root_cmp_t` 风格一致；同步更新所有 cmp 函数签名加 const。
src/ds/ns_rbtree.c 内部调用点（`tree->cmp(node, parent)`）无需修改——
非 const 实参到 const 形参的转换是 C 标准允许的隐式转换。

#### 作者建议
（已采纳）字段改 const；`ns_timer_cmp` / `tree_item_cmp` 同步加 const；
内部调用点因 const-correctness 方向的隐式转换无需修改。

#### 可重现的失败场景
```c
static int cmp(const ns_rbtree_node_t *a, const ns_rbtree_node_t *b)
{
    (void)a; (void)b;
    return 0;
}
ns_rbtree_root_init(&tree, cmp);
/* gcc -Wincompatible-pointer-types: 不兼容指针类型赋值 */
```

#### 定位
include/nanosig/nanosig_rbtree.h:40
src/ns_timer.c:49
test/unit/test_ds_rbtree.c:17
test/unit/test_data_structures_contract_compile.c:100
src/ds/ns_rbtree.c:609
src/ds/ns_rbtree.c:615
src/ds/ns_rbtree.c:641

---

### RBTREE-013: `nanosig_rbtree.h` 缺少 `@thread-safety` 标注

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: doc

#### 问题描述
include/nanosig/nanosig_rbtree.h 完全没有 thread-safety 相关注释。
对比 include/nanosig/nanosig_list.h:6 与 include/nanosig/nanosig_slist.h:6、
include/nanosig/nanosig_hashtbl.h:9 均在文件顶部声明"非线程安全"，rbtree
头文件未声明，等同于文档缺失。该问题由 `sanitize-all` A3 header audit 暴露
（2026-09-21 WARN）。

#### review 建议
在 `nanosig_rbtree.h` 文件顶部 `@file` 块内增加 `@thread-safety` 注释，
遵循 `nanosig_list.h:6` / `nanosig_slist.h:6` 风格：声明 rbtree 为单调用方线程
使用，并发访问需调用方自行同步（基于 `ns_rbtree_for_each_entry_safe` 等
遍历宏对裸节点指针的非原子操作）。可选：参考
include/nanosig/nanosig_mpsc_record_ring.h:135-186 为 `find_add` /
`find_new_add` / `match_find` 等带回调的函数补充 `@warning callback`
说明（说明 callback 在调用方线程上下文中执行、非 broker 线程）。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
`grep -L 'thread-safety\|MPM-safe\|非线程安全\|原子操作\|单读单写\|并发\|串行' include/nanosig/nanosig_rbtree.h`
→ 0 命中。其他 8 个 required header 均有命中。

#### 定位
include/nanosig/nanosig_rbtree.h

---

## 现在关闭的问题

### RBTREE-011: `ns_rb_replace_node` 非 static 定义但公共头无声明（API 缺口）

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: design

#### 问题描述

`ns_rb_replace_node`（src/ds/ns_rbtree.c:590）为非 static 全局定义，会被静态库导出，
但 include/nanosig/nanosig_rbtree.h 中没有任何声明。对比同文件其余全局函数
（ns_rbtree_prev / ns_rbtree_match_find / ns_rbtree_find_new_add 等）均已在头文件声明。
由此产生两种可能：该函数本应是公共 API 却缺原型（调用方跨 TU 使用需自声明或产生
隐式声明告警），或本应是 static 内部函数却意外导出（头文件与实现的公共符号面不一致）。

测试暴露：本测试套件新增用例（test/unit/test_ds_rbtree.c）为覆盖该函数需要
`extern void ns_rb_replace_node(...)` 自声明，发现头文件无对应原型。

#### review 建议

由作者决策：若该函数属于公共 API，在 include/nanosig/nanosig_rbtree.h 补充声明与
docstring；若仅内部使用，改为 `static` 并评估是否可删除。二选一消除"导出但未声明"
的符号面不一致。

#### 作者建议

（2026-08-09 作者决定：保持现状。该函数作为内部低层原语暂不暴露到公共头文件、也不改为 static；不引入 -Wunused-function 告警。后续若出现真实生产调用者再评估：公开 API 则补声明，仅内部使用则改 static 并视用途保留/删除。测试侧已通过 extern 自声明覆盖 5 种形态，见 test/unit/test_ds_rbtree.c:554-598。）

#### 关闭原因

作者决策：内部低层原语暂不暴露公共 API、也不改 static（避免 -Wunused-function 告警），
保持现状，待未来出现真实生产调用者再评估。测试侧已通过 extern 自声明覆盖 5 种形态
（test/unit/test_ds_rbtree.c:554-598）。关闭-已拒绝 永久保留。
- 关闭日期: 2026-08-09
- 状态: 关闭-已拒绝

#### 可重现的失败场景

```c
#include <nanosig/nanosig_rbtree.h>
/* 编译报错：ns_rb_replace_node 未声明（无原型） */
ns_rb_replace_node(victim, new_node, &tree);
/* 需在调用方手动 extern 声明才能通过编译 */
```

#### 定位
src/ds/ns_rbtree.c:590

---


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