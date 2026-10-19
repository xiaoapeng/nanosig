---
review_schema: "nanosig-review-v1"
entries:
  - id: "RBTREE-009"
    module: "rbtree"
    status: "closed-rejected"
    severity: "medium"
    type: "doc"
    section: "closed"
    closed_date: "2026-07-18"
    location: "docs/review/rbtree-code-review.md#RBTREE-009"
  - id: "RBTREE-010"
    module: "rbtree"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-11"
    location: "docs/review/rbtree-code-review.md#RBTREE-010"
  - id: "RBTREE-012"
    module: "rbtree"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "open"
    location: "docs/review/rbtree-code-review.md#RBTREE-012"
  - id: "RBTREE-014"
    module: "rbtree"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/rbtree-code-review.md#RBTREE-014"
  - id: "RBTREE-015"
    module: "rbtree"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/rbtree-code-review.md#RBTREE-015"
  - id: "RBTREE-016"
    module: "rbtree"
    status: "open"
    severity: "high"
    type: "bug"
    section: "open"
    location: "docs/review/rbtree-code-review.md#RBTREE-016"
  - id: "RBTREE-013"
    module: "rbtree"
    status: "closed-fixed"
    severity: "medium"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/rbtree-code-review.md#RBTREE-013"
  - id: "RBTREE-017"
    module: "rbtree"
    status: "closed-fixed"
    severity: "medium"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/rbtree-code-review.md#RBTREE-017"
  - id: "RBTREE-018"
    module: "rbtree"
    status: "open"
    severity: "medium"
    type: "bug"
    section: "open"
    location: "docs/review/rbtree-code-review.md#RBTREE-018"
  - id: "RBTREE-011"
    module: "rbtree"
    status: "closed-rejected"
    severity: "medium"
    type: "design"
    section: "closed"
    closed_date: "2026-08-09"
    location: "docs/review/rbtree-code-review.md#RBTREE-011"
  - id: "RBTREE-001"
    module: "rbtree"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    location: "docs/review/rbtree-code-review.md#RBTREE-001"
  - id: "RBTREE-002"
    module: "rbtree"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    location: "docs/review/rbtree-code-review.md#RBTREE-002"
  - id: "RBTREE-003"
    module: "rbtree"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    location: "docs/review/rbtree-code-review.md#RBTREE-003"
  - id: "RBTREE-004"
    module: "rbtree"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    location: "docs/review/rbtree-code-review.md#RBTREE-004"
  - id: "RBTREE-005"
    module: "rbtree"
    status: "closed-fixed"
    severity: "low"
    type: "bug"
    section: "closed"
    location: "docs/review/rbtree-code-review.md#RBTREE-005"
  - id: "RBTREE-006"
    module: "rbtree"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    location: "docs/review/rbtree-code-review.md#RBTREE-006"
  - id: "RBTREE-007"
    module: "rbtree"
    status: "closed-fixed"
    severity: "low"
    type: "bug"
    section: "closed"
    location: "docs/review/rbtree-code-review.md#RBTREE-007"
  - id: "RBTREE-008"
    module: "rbtree"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    location: "docs/review/rbtree-code-review.md#RBTREE-008"
---
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

<a id="RBTREE-016"></a>
### RBTREE-016: `ns_rbtree_add` / `ns_rbtree_find_add` 缺少"节点已链接"防御性检查（重复 add 产生自环并可致遍历死循环）

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
`ns_rbtree_find_new_add` 在插入前做了 `if(!ns_rbtree_node_is_empty(node)) return NULL;`（src/ds/ns_rbtree.c:684），但同一族的 `ns_rbtree_add`（src/ds/ns_rbtree.c:607）与 `ns_rbtree_find_add`（src/ds/ns_rbtree.c:632）**没有任何等价检查**（grep `ns_rbtree_node_is_empty` 在两函数体内 0 命中）。

请注意 RBTREE-010 关闭时给出的理由是"与 `ns_rbtree_find_add` 的防御性检查保持一致"——该描述与当前源码不符：find_add 从未有这条检查。按 §3.1 例外 2（代码状态与历史关闭理由不再一致），此处以 RBTREE-010 的原 ID 家族重新提出，并说明重开理由。

对同一节点重复 `ns_rbtree_add` 会把它挂成自己的左孩子，形成自环，`ns_rb_left_deepest_node` 因此在自环上无限自转；`ns_rbtree_find_add` 传入已链接节点则直接把节点二次挂入，BST 序与黑高同时被破坏。

#### review 建议
把 `ns_rbtree_find_new_add` 已有的一行守卫复制到两个函数入口（或统一成同一个内部 helper，避免第三处再漏）：

```c
if (!ns_rbtree_node_is_empty(node))
    return NULL;     /* ns_rbtree_add 契约里 NULL 已有"未成为 leftmost"的含义，
                        需在头文件 docstring 中补充"节点已链接时返回 NULL" */
```

`ns_rbtree_add` 的返回值已被 leftmost 语义占用，建议同时更新 include/nanosig/nanosig_rbtree.h:103-109 的 `@return` 文档说明该错误返回，避免与 `ns_rbtree_find_new_add` 的守卫行为再次漂移。若作者认为重复 add 属调用方错误，则需先把该前提写进公共头 docstring（当前头文件对三个插入函数均无任何前置条件说明）。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
/* 场景 A：重复 add 形成自环 → 后序遍历死循环 */
ns_rbtree_add(&items[3].node, &tree);
ns_rbtree_add(&items[3].node, &tree);         /* 无守卫，二次挂入 */
/* 实测：items[3].node.rb_left == &items[3].node（自环），且 node_is_empty 变为 1 */
ns_rbtree_first_postorder(&tree);              /* ns_rb_left_deepest_node 自转，进程挂死
                                                  （实测 5 秒不返回，强制 kill） */

/* 场景 B：find_add 传入已链接节点 → 树损坏 */
/* 树内已有 key 0..19；把已链接的 items[0].key 改成 100 后再 find_add */
ns_rbtree_find_add(&items[0].node, &tree);
/* 实测：返回该节点（key 100），随后校验报 "BST order bad at key 1: left child 100"，
   且 rb_leftmost 变成 100（真实最小键为 1）—— 树同时违反 BST 序与 leftmost 缓存 */
```

#### 定位
src/ds/ns_rbtree.c:607
src/ds/ns_rbtree.c:623
src/ds/ns_rbtree.c:632
src/ds/ns_rbtree.c:652
src/ds/ns_rbtree.c:684
include/nanosig/nanosig_rbtree.h:103
include/nanosig/nanosig_rbtree.h:111
include/nanosig/nanosig_rbtree.h:119



<a id="RBTREE-018"></a>
### RBTREE-018: 遍历宏裸用 `typeof`，严格 `-std=c11` 下无法编译

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
include/nanosig/nanosig_rbtree.h:207-223 的 4 个遍历宏（`for_each_entry` / `for_each_entry_prev` / `for_each_entry_match` / `for_each_entry_postorder_safe`）直接使用裸 `typeof`。`typeof` 是 GNU 扩展，在 `-std=c11`（非 gnu11）下 clang 与 gcc 都报错：

```
error: call to undeclared function 'typeof'; ISO C99 and later do not support
       implicit function declarations [-Wimplicit-function-declaration]
```

include/nanosig/nanosig_types.h:17-22 只在 `#ifdef __cplusplus` 分支里做了 `typeof` → `__typeof__` 的兼容收口，纯 C 分支没有对应处理；而项目自身的 api-compile-checks 恰恰使用 `-std=c11`（cmake/ApiCompileChecks.cmake:38）。当前没有暴露，只是因为 CMake 的 `target_compile_features(nanosig PUBLIC c_std_11)` 默认带 C_EXTENSIONS，实际编译走的是 `-std=gnu11`；并且 `nanosig_api_check_data_structures` 的契约 TU（test/unit/test_data_structures_contract_compile.c）内部没有用到任何遍历宏，检查无法覆盖到这条路径。

#### review 建议
二选一，推荐后者（一处收敛，其他数据结构头文件同样受益）：

1. 把 include/nanosig/nanosig_rbtree.h 四个宏里的裸 `typeof` 全部改为 `__typeof__`；
2. 在 include/nanosig/nanosig_types.h 的纯 C 分支补齐与 C++ 分支对称的收口：
   `#if !defined(typeof) && (defined(__GNUC__) || defined(__clang__))` → `#define typeof(...) __typeof__(__VA_ARGS__)`。

同时建议在 test/unit/test_data_structures_contract_compile.c 里加一行 rbtree 遍历宏的展开（例如用 `ns_rbtree_for_each_entry` 空转一圈），让 api-compile-checks 在 `-std=c11` 下真正覆盖遍历宏。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
/* t.c */
#include <nanosig/nanosig_rbtree.h>
typedef struct { ns_rbtree_node_t node; } it_t;
int walk(ns_rbtree_t *t){ it_t *p; int n = 0; ns_rbtree_for_each_entry(p, t, node) { n++; } return n; }
```
```
cc -std=c11 -fsyntax-only -Wall -Wextra -Iinclude t.c
→ error: call to undeclared function 'typeof'（clang 与 gcc 均复现）
cc -std=gnu11 -fsyntax-only ... t.c   → 通过
```

#### 定位
include/nanosig/nanosig_rbtree.h:207
include/nanosig/nanosig_rbtree.h:213
include/nanosig/nanosig_rbtree.h:219
include/nanosig/nanosig_rbtree.h:200
include/nanosig/nanosig_types.h:17
cmake/ApiCompileChecks.cmake:38

## 现在关闭的问题

<a id="RBTREE-014"></a>
### RBTREE-014: `ns_rb_replace_node` 替换 leftmost 节点后 `rb_leftmost` 悬垂

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: `ns_rb_replace_node` 在生产代码中零调用，仅测试通过 `extern` 引用，已整体删除该函数及对应测试用例；leftmost 缓存维护缺失与 victim 未复位问题随函数消失，代码中不再存在该内部原语。
- **关闭日期**: 2026-10-19

---

<a id="RBTREE-015"></a>
### RBTREE-015: `ns_rb_replace_node` 未把 victim 置回空节点状态，残留链接导致后续删除丢失真实节点

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 合同前提保护：`ns_rb_replace_node` 是内部非公共 API，不在 `include/nanosig/nanosig_rbtree.h` 声明，仅供测试通过 `extern` 手动引用。调用方需自行理解函数语义，在替换 leftmost 节点后自行维护 `rb_leftmost` 缓存及 victim 状态复位。库内部其他代码不调用此函数，正常构建路径不会触发。
- **关闭日期**: 2026-10-19
- **加注（2026-10-19）**: 该函数已整体删除，本条随函数消失而失效。

---

<a id="RBTREE-017"></a>
### RBTREE-017: `ns_rbtree_find_new_add` 的"cmp 与 match 偏序必须一致"约束文档在移植中丢失（RBTREE-003 回归）

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: doc
- **关闭原因**: 已在 `include/nanosig/nanosig_rbtree.h` 的 `ns_rbtree_find_new_add` docstring 中恢复 `@attention` 说明 cmp 与 match 偏序必须一致的约束，并在 `src/ds/ns_rbtree.c` 文件头恢复 `@section match_vs_cmp`。
- **关闭日期**: 2026-10-19

---
### RBTREE-013: `nanosig_rbtree.h` 缺少 `@thread-safety` 标注

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: doc
- **关闭原因**: 已在 `include/nanosig/nanosig_rbtree.h` 文件顶部 `@file` 块内增加 `@warning`，声明 rbtree 为单调用方线程使用，并发访问需调用方自行同步。
- **关闭日期**: 2026-10-19

---
### RBTREE-010: `ns_rbtree_find_new_add` 缺少对 `new_node` 返回值的已链接检查

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 合同前提保护：`ns_rbtree_find_new_add` 的 `new_node` 回调必须返回未链接的节点，属调用方文档化前提。违反该前提属调用方责任，不属库缺陷。
- **关闭日期**: 2026-10-19

---
### RBTREE-009: `ns_rbtree_insert` 返回值文档与实现不一致

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: doc
- **关闭原因**: 审核确认 `ns_rbtree_add` 的返回值为 `leftmost ? node : NULL`，与文档完全一致，reviewer 误读代码。非库缺陷。
- **关闭日期**: 2026-10-19

---

<a id="RBTREE-011"></a>
### RBTREE-011: `ns_rb_replace_node` 非 static 定义但公共头无声明（API 缺口）

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: design
- **加注（2026-10-19）**: 该函数已整体删除，符号面不一致问题不复存在。

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


<a id="RBTREE-001"></a>
### RBTREE-001: `ns_rbtree_remove` 中 `size` 下溢防护静默掩盖错误

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 作者决定保持现状。`ns_rb_node_can_remove` + `ns_rbtree_node_init`
  已能阻止重复移除，size 下溢在正确使用时不会触发。
- **关闭日期**: 2026-07-04

<a id="RBTREE-002"></a>
### RBTREE-002: `ns_rbtree_node_init` 自指针哨兵依赖指针 ≥2 字节对齐

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: `ns_rbtree_node_t` 添加 `NS_ALIGNED(sizeof(long))`，
  编译期保证结构体按 long 对齐，地址低 bit 可安全用作颜色编码。
  头文件文档说明 packed 嵌入约束。
- **关闭日期**: 2026-07-04

<a id="RBTREE-003"></a>
### RBTREE-003: `ns_rbtree_find_new_add` 中 `match` 与 `cmp` 不一致会损坏 leftmost 缓存

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `nanosig_rbtree.h` 头文件已有 `@attention` 说明约束；
  本轮修复在 `ns_rbtree.c` 顶部 `@section match_vs_cmp` 中重申语义约束
  与典型做法（直接传递 cmp 的包装作为 match）。
- **关闭日期**: 2026-07-04

<a id="RBTREE-004"></a>
### RBTREE-004: `ns_rb_delete_fixup` 中冗余的 NULL 三元表达式与可读性问题

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 提取 `ns_rb_sibling_left` / `ns_rb_sibling_right` helper 函数，
  删除函数体中所有 `sibling == NULL ? NULL : sibling->left` 三元模式，
  改为 `ns_rb_sibling_left(sibling)` 调用。
- **关闭日期**: 2026-07-04

<a id="RBTREE-005"></a>
### RBTREE-005: `ns_rbtree_node_is_empty` 中 RED/BLACK 编码不直观

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 头文件 `@section encoding` 注释解释空节点 sentinel
  `parent_and_color == self` 自指针的最低位为 0（因 sizeof(long) 对齐），
  因此 sentinel 自然编码为 RED，并说明这与"空 = BLACK"的直觉相反但安全。
- **关闭日期**: 2026-07-04

<a id="RBTREE-006"></a>
### RBTREE-006: 两子节点删除分支中 `child` 的 parent 更新路径有微妙差异

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 作者决定保持现状，与 Linux 内核 rbtree 实现对齐。
- **关闭日期**: 2026-07-04

<a id="RBTREE-007"></a>
### RBTREE-007: `ns_rb_set_color` 与 `original_color` 的关系需要 inline 注释

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: `ns_rbtree_remove` 中 `original_color` 旁添加注释，
  说明其是被移除槽位的原始颜色、决定是否需要修复，以及 successor 继承
  node 颜色的不变量含义。
- **关闭日期**: 2026-07-04

<a id="RBTREE-008"></a>
### RBTREE-008: 删除修复中兄弟节点 `sibling` 的访问模式可读性差

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 作者决定保持现状（已被 RBTREE-004 部分缓解——通过 helper 函数
  封装带 NULL 检查的 sibling child 访问）。
- **关闭日期**: 2026-07-04