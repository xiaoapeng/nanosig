---
review_schema: "nanosig-review-v1"
entries:
  - id: "LIST-005"
    module: "list"
    status: "open"
    severity: "medium"
    type: "bug"
    section: "open"
    location: "docs/review/list-code-review.md#LIST-005"
    locations:
      - "include/nanosig/nanosig_list.h:282-285"
      - "include/nanosig/nanosig_list.h:307-311"
      - "include/nanosig/nanosig_list.h:359-363"
      - "include/nanosig/nanosig_types.h:17-22"
      - "include/nanosig/nanosig_types.h:108"
      - "docs/review/rbtree-code-review.md:371"
  - id: "LIST-001"
    module: "list"
    status: "closed-rejected"
    severity: "critical"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-05"
    location: "docs/review/list-code-review.md#LIST-001"
  - id: "LIST-002"
    module: "list"
    status: "closed-rejected"
    severity: "critical"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-05"
    location: "docs/review/list-code-review.md#LIST-002"
---

# List (双向链表) 代码审查报告

**日期**: 2026-07-05
**审查者**: claude-code-review

---

## 修复历史

(无)

---

## 现在打开的问题

<a id="LIST-005"></a>
### LIST-005: 遍历宏族裸用 `typeof`，与 RBTREE-018 同一根因（严格 `-std=c11` 下八个 entry 遍历宏全部无法编译）

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`include/nanosig/nanosig_list.h` 的 8 个 entry 遍历宏（`ns_list_for_each_entry` :282、`for_each_prev_entry` :294、
`for_each_entry_safe` :307、`for_each_prev_entry_safe` :321、`for_each_entry_continue` :334、
`for_each_prev_entry_continue` :346、`for_each_entry_continue_safe` :359、`for_each_prev_entry_continue_safe` :373）
全部直接使用裸 `typeof`。`typeof` 是 GNU 扩展，在 `-std=c11`（非 `gnu11`）下 gcc 与 clang 均报错：

```
$ gcc -std=c11 -I include -fsyntax-only t.c      # t.c 含 ns_list_for_each_entry
include/nanosig/nanosig_list.h:283:45: error: call to undeclared function 'typeof'
include/nanosig/nanosig_list.h:283:17: error: expected expression
include/nanosig/nanosig_list.h:283:17: error: unknown type name 'typeof'; did you mean 'typedef'?
```
（gcc 与 clang 各复现 21 条 error；`-std=gnu11` 下全部通过。）

**与 RBTREE-018 的重复性说明（按 §3.1 例外条款 3 显式引用历史条目并说明重提理由）**：
根因已记录在 `docs/review/rbtree-code-review.md:371` 的 `RBTREE-018`（我不可跨模块复核其当前状态，按"不假定已关闭"处理）。
重新提出的理由是受影响面：该根因位于 `include/nanosig/nanosig_types.h:17-22`（`typeof` 收口只写了 C++ 分支），
凡是用 `NS_CONTAINER_OF` / `NS_CONTAINER_OF_SAFE` 推导类型的宏都中招——本模块 8 个宏、
`nanosig_slist.h` 2 个宏、`nanosig_rbtree.h` 4 个宏，以及 `nanosig_hashtbl.h:262` 对 `ns_list_for_each_entry_safe` 的间接使用。
按"每个 agent 只 review 自己模块"的分工，本模块的复核不能由 rbtree 条目代劳。**建议由维护者合并为一处修复**（见下）。

#### review 建议
1. 首选（一处收敛，各模块同时闭环）：在 `include/nanosig/nanosig_types.h` 的**纯 C** 分支补齐
   `#if !defined(typeof) && (defined(__GNUC__) || defined(__clang__))` → `#define typeof(...) __typeof__(__VA_ARGS__)`，
   或把 8 个宏里的 `typeof` 统一改写为 `__typeof__`。
2. 若明确放弃严格 C11，则在 `include/nanosig/nanosig_list.h` 文件头注明"遍历宏依赖 GNU `typeof` 扩展"，
   本条转为文档化事项。
3. 建议同时在 `test/unit/test_data_structures_contract_compile.c` 里加一行空闲的 `ns_list_for_each_entry` 展开，
   使 `api-compile-checks` 的 `-std=c11`（`cmake/ApiCompileChecks.cmake:37`）真正覆盖到遍历宏；
   该 TU 目前只调用 `ns_list_init` / `push_back` / `remove_init` / `ns_list_entry`，不触碰任何遍历宏。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
/* t.c */
#include <nanosig/nanosig_list.h>
struct e { ns_list_node_t link; int v; };
int f(void)
{
    ns_list_node_t head; struct e a, b, *pos, *n;
    ns_list_init(&head); ns_list_init(&a.link); ns_list_init(&b.link);
    ns_list_push_back(&head, &a.link); ns_list_push_back(&head, &b.link);
    ns_list_for_each_entry(pos, &head, link){ pos->v++; }        /* <- 编译失败 */
    ns_list_for_each_entry_safe(pos, n, &head, link){ pos->v++; } /* <- 编译失败 */
    return a.v + b.v;
}
```
```
gcc -std=c11 -Iinclude -fsyntax-only t.c   → 21 errors（clang 同）
gcc -std=gnu11 -Iinclude -fsyntax-only t.c → 通过
```

#### 定位
include/nanosig/nanosig_list.h:282-285
include/nanosig/nanosig_list.h:307-311
include/nanosig/nanosig_list.h:359-363
include/nanosig/nanosig_types.h:17-22
include/nanosig/nanosig_types.h:108
docs/review/rbtree-code-review.md:371

---

## 现在关闭的问题

<a id="LIST-001"></a>
### LIST-001: `ns_list_for_each_entry_continue_safe` 初始化 `n` 与 `pos` 指向同一节点，遍历中移除当前节点导致无限循环或 NULL 解引用

- **状态**: 关闭-已拒绝
- **严重度**: 🔴 关键
- **关闭原因**: 宏实际正确。update 子句 `(pos) = (n), (n) = ns_list_entry((pos)->member.next, ...)` 中逗号运算符保证从左到右执行：先 `pos = n`（pos 指向旧的 n，有效节点），再从**新的 pos**（即旧的 n）取 next 派生 `n`。`n` 始终从旧 `n` 独立推进，不依赖被删除的旧 `pos`。review 中"初始化子句 pos 和 n 被赋值为同一个节点"的描述也不成立——init 子句中 `pos` 先更新为 `pos->next`，然后 `n` 从更新后的 `pos` 取 next，两者不同。
- **关闭日期**: 2026-07-05

---

<a id="LIST-002"></a>
### LIST-002: `ns_list_for_each_prev_entry_continue_safe` 同样存在 `n` 未独立预取的 bug

- **状态**: 关闭-已拒绝
- **严重度**: 🔴 关键
- **关闭原因**: 与 LIST-001 同类误判。update 子句先 `pos = n` 再从新 `pos`（旧 `n`）取 prev 派生 `n`，`n` 始终从有效节点推进。
- **关闭日期**: 2026-07-05
