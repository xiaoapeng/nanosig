# nanosig-core review 归档 2026-08

<a id="CORE-011"></a>
### CORE-011: `ns_signal_connect` 缺少"禁止重复连接"前置条件文档

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
`ns_signal_connect` 未文档化"connection 必须处于未连接状态"这一前置条件。如果调用方对一个已经连接到某个 signal 的 `ns_connection_t` 再次调用 `ns_signal_connect`（未先 disconnect），`ns_list_init` 将 `signal_node` 重置为自引用，但不更新原链表中前后节点的指针，导致原 signal 的 slot_list 断链。

#### review 建议
在 `nanosig_signal.h` 的 `ns_signal_connect` docstring 中添加 `@pre` 前置条件。可在实现中添加防御性检查：如果 `connection->signal_node` 不是自引用状态，返回 `NS_E_INVAL`。

#### 作者建议
已采纳 review 建议，在 docstring 中添加 `@pre connection 必须处于未连接状态` 和 `@warning 重复连接会导致原 signal 的 slot_list 损坏`。
→ 关闭-已修复

#### 关闭原因
`include/nanosig/nanosig_signal.h:290-313` 已添加 `@pre` 和 `@warning` 文档。

- **关闭日期**: 2026-08-02
- **状态**: 关闭-已修复

#### 可重现的失败场景
1. `ns_signal_connect(&sig_a, ...)` 将 conn 连接到 sig_a。
2. `ns_signal_connect(&sig_b, ...)` 将 conn 连接到 sig_b（未先 disconnect）。
3. sig_a 的 slot_list 断链：遍历 sig_a 时无法到达原后继节点。

#### 定位
src/nanosig.c:337-342
include/nanosig/nanosig_signal.h:290-313

---

<a id="CORE-012"></a>
<a id="CORE-015"></a>

---
