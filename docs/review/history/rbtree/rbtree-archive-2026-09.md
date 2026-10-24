# rbtree review 归档 2026-09

<a id="RBTREE-012"></a>
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
