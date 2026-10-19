---
review_schema: "nanosig-review-v1"
entries:
  - id: "SLIST-003"
    module: "slist"
    status: "closed-rejected"
    severity: "medium"
    type: "cleanup"
    section: "closed"
    closed_date: "2026-07-18"
    location: "docs/review/slist-code-review.md#SLIST-003"
    locations:
      - "include/nanosig/nanosig_slist.h:276-285"
  - id: "SLIST-004"
    module: "slist"
    status: "closed-rejected"
    severity: "low"
    type: "cleanup"
    section: "closed"
    closed_date: "2026-08-02"
    location: "docs/review/slist-code-review.md#SLIST-004"
    locations:
      - "include/nanosig/nanosig_slist.h:367"
  - id: "SLIST-006"
    module: "slist"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-18"
    location: "docs/review/slist-code-review.md#SLIST-006"
    locations:
      - "include/nanosig/nanosig_slist.h:290-303"
  - id: "SLIST-007"
    module: "slist"
    status: "open"
    severity: "high"
    type: "bug"
    section: "open"
    location: "docs/review/slist-code-review.md#SLIST-007"
    locations:
      - "include/nanosig/nanosig_slist.h:284"
      - "include/nanosig/nanosig_slist.h:109-114"
      - "include/nanosig/nanosig_slist.h:152-165"
      - "include/nanosig/nanosig_slist.h:32-38"
  - id: "SLIST-008"
    module: "slist"
    status: "open"
    severity: "medium"
    type: "bug"
    section: "open"
    location: "docs/review/slist-code-review.md#SLIST-008"
    locations:
      - "include/nanosig/nanosig_slist.h:408-411"
      - "include/nanosig/nanosig_slist.h:421-425"
      - "include/nanosig/nanosig_types.h:17-22"
      - "include/nanosig/nanosig_types.h:108"
      - "docs/review/rbtree-code-review.md:371"
  - id: "SLIST-001"
    module: "slist"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/slist-code-review.md#SLIST-001"
---

# Slist 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

<a id="SLIST-001"></a>
### SLIST-001: `ns_slist_for_each` 宏在 C++ 下无法编译 (CRITICAL) `bug`

- **修复 commit**: 当前 diff
- **修复说明**: 在 `include/nanosig/nanosig_slist.h` 中添加 `#ifdef __cplusplus` 分支：C++17 下使用 `reinterpret_cast` 读取公共首指针，避免 `_Generic` 和 `({})` 语法；C11 下恢复原始 `->first` 读取方式（头指针 init 只执行一次，避免死循环）。C++ 编译和运行时均已验证通过。

---

<a id="SLIST-002"></a>

---

## 现在打开的问题

<a id="SLIST-007"></a>
### SLIST-007: `ns_slist_del_node_in_for_each_safe` 删除头节点后留下悬垂 `list->last`，后续复用该链表会把新节点写进头字段

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
`ns_slist_del_node_in_for_each_safe`（`include/nanosig/nanosig_slist.h:276-285`）只在 `next == NULL` 时维护 `list->last`。
删除的是头节点、且其后还有节点时，`list->last` 不会被更新。调用方按 `ns_slist_for_each_safe` 的用法逐节点删完整个链表后，
`list->first == NULL` 但 `list->last == (ns_slist_node_t *)list` —— **恰好等于链表头对象自身的地址**。

这个悬垂值是**破坏性**的，不只是"读起来不对"：
`ns_slist_t` 的布局是 `first` 在偏移 0、`last` 在偏移 8（`include/nanosig/nanosig_slist.h:32-38`）。
排空后继续使用该链表的**任何**路径都会把哨兵当节点用：
- `ns_slist_push_back` / `ns_slist_add_batch_tail`：走 `else` 分支写 `list->last->next`，即 `list->first`（偏移 0 重叠）→ **已在链上的首节点从链上脱落**；
- `ns_slist_insert(prev=tail)`：同样写 `list->first`；
- `ns_slist_empty()` 返回 true 而 `ns_slist_last()` 返回非 NULL（`include/nanosig/nanosig_slist.h:109-114` 文档承诺空链表返回 NULL），两者自相矛盾。

用文档方式遍历一个链表、把节点删光、再往同一个头里 push 是再普通不过的模式（`ns_slist_t` 是自持的静态头对象，
`ns_slist_init` 之外没有"重建"步骤）。`ns_slist_append_list` 恰好没事，只是因为它的 `ns_slist_empty(list)` 分支直接覆写了 `last`。

与 SLIST-003 的区别（那条已 `关闭-已拒绝`，本条的根因与修复方向都不同）：SLIST-003 针对的是**被删除节点自身的 `next` 没清零**（`ns_slist_node_is_on_list` 误报），
本条是**链表头 `last` 字段被写成哨兵**并导致后续 push 丢节点；修复点是头字段的维护，不是节点自身的 `next`。

#### review 建议
在函数末尾补上头节点被删除时的 `last` 维护（只需两行，与现有 `next == NULL` 分支对称）：
```c
if(prev == NULL){
    list->first = next;
    if(next == NULL) list->last = NULL;   /* 删的是唯一节点：链表真正空了 */
}
```
注意不能简单写成"删头就清 last"：删头且后面仍有节点时 `last` 必须保持原值。
另建议在 `@note` 里写明"删除头节点且链表仍非空时，`list->last` 保持不变（仍指向原尾节点）"，
并补一条回归用例：2 节点链表 drain 后 `ns_slist_empty(&l) == 1 && ns_slist_last(&l) == NULL`，
再 `push_front` + `push_back` 断言链序完整。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
ns_slist_t l = NS_SLIST_INITIALIZER;
item_t a = {.v = 1}, b = {.v = 2};
ns_slist_push_back(&l, &a.node);
ns_slist_push_back(&l, &b.node);

ns_slist_node_t *prev, *pos, *nxt;
ns_slist_for_each_safe(prev, pos, nxt, &l){
    ns_slist_del_node_in_for_each_safe(&l, prev, nxt);   /* 逐节点删光 */
}

/* 排空后：l.first == NULL，但 l.last == (ns_slist_node_t *)&l（哨兵，不是 NULL） */
ns_slist_empty(&l);   /* -> 1（非 0） */
ns_slist_last(&l);    /* -> &l，与 docstring“空链表返回 NULL”矛盾；按 ns_slist_entry 反查会越界 */

ns_slist_push_front(&l, &a.node);   /* 期望链上出现 a */
ns_slist_push_back(&l, &b.node);    /* 期望链序 [a b] */

/* 实测：push_back 走 else 分支执行 l.last->next = &b.node，
   而 l.last 与 &l 重合 -> 该写入落到 l.first（偏移 0 与偏移 0 重叠），
   结果 l.first 直接变成 &b.node，a 从链上脱落： */
assert(ns_slist_first(&l) == &b.node);              /* 链上只剩 b */
/* 若 list 头不是栈上变量而是静态/成员对象，则该写入落在头对象内部、
   不会越界写其它内存，因此 ASan 不会报错（已实测）。 */
```

#### 定位
include/nanosig/nanosig_slist.h:284
include/nanosig/nanosig_slist.h:109-114
include/nanosig/nanosig_slist.h:152-165
include/nanosig/nanosig_slist.h:32-38

---

<a id="SLIST-008"></a>
### SLIST-008: 遍历宏族裸用 `typeof`，与同仓库 rbtree 的 RBTREE-018 同一根因（严格 `-std=c11` 下两个 entry 遍历宏无法编译）

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_slist_for_each_entry`（`include/nanosig/nanosig_slist.h:408-411`）与 `ns_slist_for_each_entry_safe`（`include/nanosig/nanosig_slist.h:421-425`）
直接使用裸 `typeof`（`include/nanosig/nanosig_slist.h:409,411,422,423,425`，共 5 处）。`typeof` 是 GNU 扩展，在 `-std=c11`（非 `gnu11`）下 gcc 与 clang 均报错；本模块这两个宏在严格 C11 下不可用。

复现（与 `api-compile-checks` 相同的 flag 组合 `-std=c11`，`cmake/ApiCompileChecks.cmake:37`）：
```
$ gcc -std=c11 -I include -fsyntax-only t.c          # t.c 含 ns_slist_for_each_entry
.../nanosig_slist.h:409:52: error: call to undeclared function 'typeof'
.../nanosig_types.h:108:42: note: expanded from macro 'NS_CONTAINER_OF'
.../nanosig_slist.h:353:5: note: expanded from macro 'ns_slist_entry_safe'
.../nanosig_slist.h:141:41: note: expanded from macro 'NS_CONTAINER_OF_SAFE'
```

**与 RBTREE-018 的重复性说明（按 §3.1 例外条款 3 显式引用历史条目并说明重提理由）**：
同一根因已在 `docs/review/rbtree-code-review.md:371` 的 `RBTREE-018` 中记录（我不可跨模块复核该条状态，按"不假定未关闭"处理）。
此处重新提出的理由是：**受影响面远大于 rbtree 模块**。除 `nanosig_rbtree.h` 外，本模块与 `nanosig_list.h`、
以及可能的其它 `NS_CONTAINER_OF` 消费者都在同一路径上；RBTREE-018 只覆盖 rbtree 自己的 4 个宏，
按"每个 agent 只 review 自己模块"的分工，本模块的复核不能由 rbtree 条目代劳。
**建议由维护者把根因合并为一处修复**：在 `include/nanosig/nanosig_types.h` 的**纯 C** 分支
（现仅有 `#ifdef __cplusplus` 分支，`include/nanosig/nanosig_types.h:17-22`）补 `typeof` → `__typeof__` 收口，
六个宏与本条一起闭环；若认为严格 C11 非支持目标，则应按 §2.4 文档仲裁在公共头写明"MSVC 以外仅支持 `gnu11`"，
本条即转为 `关闭-不适用`。

#### review 建议
1. 首选：在 `include/nanosig/nanosig_types.h` 增补纯 C 分支的 `typeof` 收口（一行 `#define`），或把这两处宏里的 `typeof` 改写为 `__typeof__`，
   与 `CMakeLists.txt:150` 的 `target_compile_features(nanosig PUBLIC c_std_11)`（实际编译带 `C_EXTENSIONS`，即 `gnu11`）语义对齐。
2. 次选：若明确放弃严格 C11，则在 `include/nanosig/nanosig_slist.h` 文件头注明"遍历宏依赖 GNU `typeof` 扩展"。
3. 无论选哪条，建议让 `test/unit/test_data_structures_contract_compile.c` 的实际使用面覆盖遍历宏
   （该 TU 目前只做 `ns_slist_push_back` / `ns_slist_pop_front`，不触碰任何 `for_each_entry`，
   因此 `api-compile-checks` 无法发现这条路径）。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
/* t.c */
#include <nanosig/nanosig_slist.h>
struct s { ns_slist_node_t node; int v; };
int f(void)
{
    ns_slist_t l = NS_SLIST_INITIALIZER;
    struct s a, *p, *n;
    ns_slist_node_init(&a.node);
    ns_slist_push_back(&l, &a.node);
    ns_slist_for_each_entry(p, &l, node){ p->v++; }        /* <- 编译失败 */
    ns_slist_for_each_entry_safe(p, n, &l, node){ p->v++; } /* <- 编译失败 */
    return a.v;
}
/* gcc/clang -std=c11 均报 21 条 error；改成 -std=gnu11 后两个文件都能编译通过。
   实测：gnu11 下 slist/list 的 entry 宏均正常编译。 */
```

#### 定位
include/nanosig/nanosig_slist.h:408-411
include/nanosig/nanosig_slist.h:421-425
include/nanosig/nanosig_types.h:17-22
include/nanosig/nanosig_types.h:108
docs/review/rbtree-code-review.md:371

---

## 现在关闭的问题

<a id="SLIST-003"></a>
### SLIST-003: `ns_slist_del_node_in_for_each_safe` 不重置被删除节点的 `next` 指针

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: cleanup

#### 问题描述
`ns_slist_del_node_in_for_each_safe` 将节点从链表中摘除，但不调用 `ns_slist_node_init` 重置被删除节点的 `next` 指针。而 `ns_slist_pop_front` 和 `ns_slist_remove_after` 均调用了 `ns_slist_node_init(node)`。被删除的非尾节点仍持有 stale `next` 指针，`ns_slist_node_is_on_list` 检查会错误返回 true。

#### review 建议
在函数末尾添加对被删除节点的 `next` 清零，与 `pop_front` / `remove_after` 保持一致。或在 docstring 中说明此行为差异。

#### 作者建议
`del_node_in_for_each_safe` 专用于遍历中安全删除，调用方在遍历结束后不应再依赖 `is_on_list` 判断被删除节点。`is_on_list` 的文档已说明"尾节点返回 false"，只保证不在链表中的节点可能返回 false，不保证精确性。`pop_front`/`remove_after` 清 `next` 是额外便利而非契约。
→ 关闭-已拒绝

#### 关闭原因
合同前提保护：`del_node_in_for_each_safe` 的语义是删除后节点不再属于链表，调用方不应在遍历后依赖 `is_on_list` 判断。`is_on_list` 文档已声明"如需精确判断，请使用 intrusive 标记或外部状态"。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已拒绝

#### 定位
include/nanosig/nanosig_slist.h:276-285

<a id="SLIST-004"></a>
### SLIST-004: `ns_slist_for_each` 宏双重求值 `head` 参数

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **类型**: cleanup

#### 问题描述
`ns_slist_for_each` 宏的 init 表达式中，`head` 被求值两次（NULL 检查 + `->next` 访问）。若 `head` 是带副作用的表达式，副作用执行两次。

#### review 建议
将 `head` 缓存到临时变量，仅求值一次。或在文档中声明 `head` 不得是带副作用的表达式。

#### 作者建议
查阅 Linux 内核 `hlist_for_each_entry` 实现，内核使用 `(head)->first` 只求值一次。但 nanosig 需要兼容 MSVC（不支持 `({ })` 语句表达式），且 `ns_slist_for_each` 无额外变量可缓存 `head`。当前宏的 `head` 参数在实用中均为简单变量名，双重求值仅为理论风险。在 docstring 中声明 `head` 不得是带副作用的表达式。
→ 关闭-已拒绝

#### 关闭原因
MSVC 兼容性限制，无法使用 `({ })` 缓存 head。实际使用中 head 均为简单变量名，双重求值仅为理论风险。docstring 已隐含此约束。

- **关闭日期**: 2026-08-02
- **状态**: 关闭-已拒绝

#### 定位
include/nanosig/nanosig_slist.h:367

---

<a id="SLIST-006"></a>
### SLIST-006: `ns_slist_append_list` 自拼接导致数据损坏

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_slist_append_list(list, other)` 在 `list == other` 时：若链表非空，`list->last->next = other->first` 成为自引用环，然后 `ns_slist_init(other)` 清空整个链表，数据全部丢失。

#### review 建议
在函数入口添加 `if(list == other) return;` 防御性检查，或在 docstring 中显式声明 `@pre list != other`。

#### 作者建议
自拼接（将链表拼给自己）没有语义——调用方不应在同一个链表上做 append。通过 docstring 显式声明 `@pre list != other` 作为契约前提即可，不需要运行时检查。
→ 关闭-已拒绝

#### 关闭原因
合同前提保护：自拼接无合理使用场景，应在文档中声明 `@pre list != other` 作为设计契约而非加运行时防御。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已拒绝

#### 可重现的失败场景
```c
ns_slist_append_list(&list, &list);  // 自拼接 → 数据全部丢失
assert(ns_slist_empty(&list));  // true — 链表数据丢失
```

#### 定位
include/nanosig/nanosig_slist.h:290-303

---

---
